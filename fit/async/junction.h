/// @file junction.h
/// @brief The async skeleton: SampleRateTap's asynchronous converter as a catenae junction of
/// independent clocks (D19, PLAN.md 6.1, CF).
///
/// | Contract   |                                                                                |
/// |------------|--------------------------------------------------------------------------------|
/// | Ports      | `port<float, Ch>` pushed at the input clock, the same pulled at the output clock |
/// | Clocks     | independent: the servo absorbs up to `tolerance_ppm()` (1,000 by default)     |
/// | Configured | template arguments: `Capacity`, the largest `Push`, the `Bank`; a `config` value: |
/// |            | the rate, the target fill, the servo's constants (D54: 160 bytes)             |
/// | State      | the ring and its words, the servo's and the resampler's memory; at rest all zero |
/// | Fill       | silence until the target plus the bank's taps; then primed, the servo seeded  |
/// | Target     | the configured one, raised by the largest pull seen to pull + max(pull/2, 16),   |
/// |            | at most `max_target` (what `Capacity` holds); `serves` is that arithmetic      |
/// | Underrun   | what was made, then silence; one underrun; fills again, the estimate kept      |
/// | Resync     | a backlog above the high water drops the excess above the target              |
/// | Latency    | nominal target + group delay in source frames; once locked the backlog a pull  |
/// |            | sees stays within a push of the target; the range published is Push + 16      |
/// | Numbers    | the frame loop in `float`; the servo in `double` with `exp` (PLAN.md 12)       |
///
/// Carried from SampleRateTap 4496888, async/include/tap/sr/async/converter.h: `basic_converter`'s
/// `push` and `pull` (the adaptive raise, filling, priming, the resync, the servo update, the
/// fade-in, the underrun), `reset_from_consumer` as `restart`, `publish_status` and `status` as
/// `report`, and `config_rejection`'s checks as `make`; spsc_ring.h's `write`, `read`,
/// `read_available` and `discard` over two plain words, as catenae's `handoff` holds its ring.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_ASYNC_JUNCTION_H
#define CATENAE_FIT_ASYNC_JUNCTION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <type_traits>

#include "async/bank.h"
#include "async/resampler.h"
#include "async/servo.h"
#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/expected.h"
#include "catenae/junction_traits.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"

namespace catenae_fit {
    namespace async {

        /// Where the converter is: SampleRateTap's `converter_state`, a 32-bit word.
        enum class lock_state : std::uint32_t {
            filling,   ///< buffering input until the ring reaches its threshold (zero: at rest)
            acquiring, ///< the servo at its wide acquisition bandwidth
            locked,    ///< the servo narrowed: tracking
        };

        /// What any thread may read: each field one 32-bit word, read on its own.
        struct async_report {
            lock_state    state;         ///< filling, acquiring or locked
            float         ppm;           ///< the estimate of f_in / f_out - 1, in ppm
            float         fill_frames;   ///< the smoothed backlog the servo reads
            std::uint32_t target_frames; ///< the setpoint in force: configured, or raised by a pull
            std::uint32_t underruns;     ///< pulls it could not fill
            std::uint32_t overruns;      ///< pushes it could not hold
            std::uint32_t resyncs;       ///< hard resyncs at the high water
        };

        namespace detail {
            // CF finding: catenae/compiler.h's atomic operations act on a std::uint32_t only, and a
            // report field may be a float (the estimate, the smoothed fill): it is published as its
            // bits, through a memcpy each way.
            inline std::uint32_t bits_of(float v) noexcept {
                std::uint32_t b = 0;
                std::memcpy(&b, &v, sizeof b);
                return b;
            }
            inline float float_of(std::uint32_t b) noexcept {
                float v = 0.0f;
                std::memcpy(&v, &b, sizeof v);
                return v;
            }

            /// SampleRateTap's feasibility rule: the setpoint a pull of `pull` frames needs is the
            /// pull plus a margin for the block beat's sawtooth, half the pull and at least a chunk.
            constexpr std::size_t target_for(std::size_t pull, std::size_t chunk) noexcept {
                return pull + std::max(pull / 2, chunk);
            }
            /// The largest pull whose setpoint is within `most`.
            constexpr std::size_t largest_pull_within(std::size_t most, std::size_t chunk) noexcept {
                std::size_t p = std::min(most, (2 * most + 2) / 3);
                while (p > 0 && target_for(p, chunk) > most) {
                    --p;
                }
                return p;
            }
        } // namespace detail

        /// @brief An asynchronous converter of `Ch` channels of float between two clocks: a ring of
        /// `Capacity` frames, pushes of up to `Push`, a fractional resampler over `Bank` and a servo.
        template <std::size_t Ch, std::size_t Capacity, std::size_t Push, typename Bank = bank_32x16>
        class junction {
            static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0, "the ring's capacity is a power of two");
            static_assert(Capacity <= 0x80000000u, "the ring holds at most 2^31 frames");
            static_assert(Push != 0 && Push <= Capacity, "a push fits the ring");

          public:
            using sink_port   = catenae::port<float, Ch>; ///< what push takes
            using source_port = catenae::port<float, Ch>; ///< what pull fills

            static constexpr catenae::clocks clocking = catenae::clocks::independent; ///< two clocks
            static constexpr catenae::ratio  rate_ratio{1, 1};                        ///< near unity, nominally 1:1
            static constexpr std::size_t     capacity_frames = Capacity;              ///< a power of two
            static constexpr std::size_t     largest_push    = Push;                  ///< the producer's largest push
            static constexpr std::size_t     taps            = Bank::taps;
            static constexpr std::size_t     chunk_frames    = 16; ///< SampleRateTap's pop chunk
            static constexpr std::size_t     fade_frames     = 64; ///< the fade-in after a fill
            static_assert(Capacity > taps + 1, "the ring holds more than the bank's window");
            /// The largest setpoint the ring holds with its high water: SampleRateTap's bound.
            static constexpr std::size_t max_target = std::min((Capacity - 1) / 3, (Capacity - taps - 1) / 2);

            /// The largest pull a setpoint within `max_target` serves.
            static constexpr std::size_t largest_pull = detail::largest_pull_within(max_target, chunk_frames);
            static_assert(largest_pull != 0, "the ring is too small for any pull");

            /// The setpoint a pull of `pull` frames needs: the pull plus a margin for the block
            /// beat's sawtooth (half the pull, at least a chunk), SampleRateTap's rule.
            static constexpr std::size_t target_for(std::size_t pull) noexcept {
                return detail::target_for(pull, chunk_frames);
            }

            /// Whether pulls of up to `pull` frames are served: the setpoint they raise fits the
            /// ring. The quantum does not enter: the resampler takes fractional input.
            // CF finding: serves() answers only whether the ring holds the setpoint the largest pull
            // raises. Whether the servo locks depends on the pattern of pulls, which serves() is not
            // told: 256, 128, 128, 128 is served (no underrun) and never locks (test_async.cpp).
            static constexpr bool serves(std::size_t pull, std::size_t quantum) noexcept {
                return quantum != 0 && pull != 0 && detail::target_for(pull, chunk_frames) <= max_target;
            }

            /// The configuration's numbers (D54): the rate, the target fill, the servo's tuning.
            struct config {
                double        sample_rate_hz        = 48000.0; ///< nominal rate of both clock domains
                std::uint32_t target_latency_frames = 48;      ///< the configured setpoint
                servo_config  servo{};                         ///< the servo's tuning
            };

            using resampler_t = resampler<Ch, Bank, chunk_frames>;

            /// The ring and the words its sides share, and the consumer's own memory: plain
            /// storage, at rest all zero.
            struct state {
                std::array<float, Capacity * Ch> ring;      ///< frame f at f % Capacity
                std::uint32_t                    head;      ///< frames pushed, wrapping; the producer's
                std::uint32_t                    tail;      ///< frames read or dropped; the consumer's
                std::uint32_t                    overruns;  ///< the producer's count
                std::uint32_t                    underruns; ///< the consumer's count
                std::uint32_t                    resyncs;   ///< the consumer's count
                std::uint32_t                    phase;     ///< published lock_state; the consumer's
                std::uint32_t                    ppm_bits;  ///< published float; the consumer's
                std::uint32_t                    fill_bits; ///< published float; the consumer's
                // CF finding: section 12 has async start from its configured targets, and D19 has
                // all zero be a junction's rest. Both hold because the setpoint in the state is what
                // a pull raised it to, 0 for none, and the configured one is the configuration's.
                std::uint32_t raised; ///< published setpoint raised by pulls, 0 if none
                // The consumer's own, read by no other thread.
                std::uint32_t               running;   ///< 1 once primed, 0 while filling
                std::uint32_t               fade_left; ///< frames of the fade-in still to apply
                std::uint32_t               pull_seen; ///< the largest pull so far
                servo_state                 servo;     ///< the loop's memory
                typename resampler_t::state resampler; ///< the history, the chunk, the phase
            };
            using report_type = async_report; ///< what report returns

            /// The default configuration: SampleRateTap's defaults at 48 kHz.
            constexpr junction() noexcept
                : junction(config{}) {}

            // CF finding: the junction contract says nothing of a configuration made from run-time
            // numbers. D37's make() is used as a design's: the constructor from a config is private.
            /// A configuration from run-time numbers, checked: SampleRateTap's `config_rejection`
            /// for what a type does not already fix. `invalid_argument` for a number that is not
            /// finite, a zero target or a deviation clamp outside (0, 100000] ppm;
            /// `out_of_range` for a target above `max_target`.
            static catenae::expected<junction, catenae::status> make(const config& c) noexcept {
                const servo_config& s     = c.servo;
                const double        all[] = {c.sample_rate_hz,
                                             s.acquire_bandwidth_hz,
                                             s.track_bandwidth_hz,
                                             s.quiet_bandwidth_hz,
                                             s.damping,
                                             s.acquire_smoother_hz,
                                             s.track_smoother_hz,
                                             s.quiet_smoother_hz,
                                             s.lock_threshold_frames,
                                             s.lock_hold_seconds,
                                             s.quiet_hold_seconds,
                                             s.unlock_threshold_frames,
                                             s.max_deviation_ppm};
                const bool          finite =
                    std::all_of(std::begin(all), std::end(all), [](double v) { return std::isfinite(v); });
                if (!finite || c.sample_rate_hz <= 0.0 || c.target_latency_frames == 0 || s.max_deviation_ppm <= 0.0
                    || s.max_deviation_ppm > 100000.0) {
                    return catenae::unexpected<catenae::status>(catenae::status::invalid_argument);
                }
                if (c.target_latency_frames > max_target) {
                    return catenae::unexpected<catenae::status>(catenae::status::out_of_range);
                }
                return junction(c);
            }

            /// The offset and drift it absorbs: the servo's deviation, before its 1.5x clamp.
            float tolerance_ppm() const noexcept { return static_cast<float>(m_servo.max_deviation_ppm()); }
            /// The configured setpoint, before any raise.
            std::size_t configured_target() const noexcept { return m_target; }

            /// The producer's thread: up to `Push` frames. Returns the frames kept; the rest, the
            /// newest, are dropped and counted as one overrun.
            [[nodiscard]] std::size_t push(state& st, catenae::span<const float> in) const noexcept {
                catenae::detail::check_always(in.size() % Ch == 0, catenae::check::input_not_whole_frames);
                const std::size_t asked = in.size() / Ch;
                catenae::detail::check_always(asked <= Push, catenae::check::push_too_large);
                const std::uint32_t head  = catenae::detail::atomic_load_relaxed(st.head);
                const std::uint32_t tail  = catenae::detail::atomic_load_acquire(st.tail);
                const std::size_t   room  = Capacity - static_cast<std::size_t>(head - tail);
                const std::size_t   taken = std::min(asked, room);
                copy_in(st, head, in.data(), taken);
                catenae::detail::atomic_store_release(st.head, head + static_cast<std::uint32_t>(taken));
                if (taken < asked) {
                    catenae::detail::atomic_store_relaxed(st.overruns,
                                                          catenae::detail::atomic_load_relaxed(st.overruns) + 1u);
                }
                return taken;
            }

            /// The consumer's thread: exactly `out`, at the output clock, up to `largest_pull`
            /// frames. Silence while filling and after an underrun; the first frames after a fill
            /// fade in. Returns the frames made from real input.
            [[nodiscard]] std::size_t pull(state& st, catenae::span<float> out) const noexcept {
                catenae::detail::check_always(out.size() % Ch == 0, catenae::check::input_not_whole_frames);
                const std::size_t frames = out.size() / Ch;
                catenae::detail::check_always(frames <= largest_pull, catenae::check::pull_too_large);
                raise_for(st, frames);
                const std::size_t target = effective_target(st);
                double            occ    = backlog(st);
                if (st.running == 0) {
                    if (occ < static_cast<double>(target + taps)) {
                        std::fill(out.begin(), out.end(), 0.0f);
                        publish(st);
                        return 0;
                    }
                    start_paying_out(st, target);
                    occ = backlog(st);
                }
                if (occ > static_cast<double>(high_water(target))) {
                    occ = resync(st, target, occ);
                }
                const double dt = static_cast<double>(frames) / m_sample_rate_hz;
                const double eps_hat =
                    m_servo.update(st.servo, static_cast<double>(target), occ, resampler_t::mu(st.resampler), dt);
                auto pop = [&st](float* dst, std::size_t most) noexcept { return pop_frames(st, dst, most); };
                const std::size_t made = resampler_t::process(st.resampler, out.data(), frames, eps_hat, pop);
                fade_in(st, out.data(), made);
                if (made < frames) { // an underrun: pad, and fill again keeping the estimate
                    std::fill(out.begin() + static_cast<std::ptrdiff_t>(made * Ch), out.end(), 0.0f);
                    catenae::detail::atomic_store_relaxed(st.underruns,
                                                          catenae::detail::atomic_load_relaxed(st.underruns) + 1u);
                    st.running = 0;
                    m_servo.reset(st.servo, true, static_cast<double>(target));
                }
                publish(st);
                return made;
            }

            /// The consumer's thread, while the producer runs: what is buffered is discarded, the
            /// estimate forgotten, and it fills again. The raised setpoint stays.
            void restart(state& st) const noexcept {
                resampler_t::reset(st.resampler);
                catenae::detail::atomic_store_release(st.tail, catenae::detail::atomic_load_acquire(st.head));
                m_servo.reset(st.servo, false, static_cast<double>(effective_target(st)));
                st.running   = 0;
                st.fade_left = 0;
                publish(st);
            }

            /// Empty, its counts zero, its setpoint the configured one, the servo and the resampler at
            /// rest: as a static state is before first use. Only while neither side runs.
            void reset(state& st) const noexcept {
                catenae::detail::atomic_store_relaxed(st.head, 0u);
                catenae::detail::atomic_store_relaxed(st.tail, 0u);
                catenae::detail::atomic_store_relaxed(st.overruns, 0u);
                catenae::detail::atomic_store_relaxed(st.underruns, 0u);
                catenae::detail::atomic_store_relaxed(st.resyncs, 0u);
                catenae::detail::atomic_store_relaxed(st.phase, 0u);
                catenae::detail::atomic_store_relaxed(st.ppm_bits, 0u);
                catenae::detail::atomic_store_relaxed(st.fill_bits, 0u);
                catenae::detail::atomic_store_release(st.raised, 0u);
                st.running   = 0;
                st.fade_left = 0;
                st.pull_seen = 0;
                st.servo     = servo_state{};
                st.resampler = typename resampler_t::state{};
            }

            /// Any thread: each field read on its own.
            report_type report(const state& st) const noexcept {
                // The loads take a word they may read-modify-write (MSVC's), as handoff's report does.
                state& shared = const_cast<state&>(st);
                return report_type{static_cast<lock_state>(catenae::detail::atomic_load_relaxed(shared.phase)),
                                   detail::float_of(catenae::detail::atomic_load_relaxed(shared.ppm_bits)),
                                   detail::float_of(catenae::detail::atomic_load_relaxed(shared.fill_bits)),
                                   static_cast<std::uint32_t>(effective_target(shared)),
                                   catenae::detail::atomic_load_relaxed(shared.underruns),
                                   catenae::detail::atomic_load_relaxed(shared.overruns),
                                   catenae::detail::atomic_load_relaxed(shared.resyncs)};
            }

            /// Any thread: the setpoint in force plus the bank's group delay, in source frames,
            /// breathing by up to a push and a chunk either side as the fill a pull sees moves.
            catenae::junction_latency latency(const state& st) const noexcept {
                const std::size_t target = effective_target(const_cast<state&>(st));
                return {static_cast<float>(target) + Bank::group_delay_frames, static_cast<float>(Push + chunk_frames)};
            }

          private:
            constexpr explicit junction(const config& c) noexcept
                : m_servo(c.servo, c.sample_rate_hz)
                , m_sample_rate_hz(c.sample_rate_hz)
                , m_target(c.target_latency_frames) {}

            static constexpr std::size_t high_water(std::size_t target) noexcept {
                return std::max(3 * target, 2 * target + taps);
            }

            /// The configured setpoint, or the one a pull raised it to.
            std::size_t effective_target(state& st) const noexcept {
                return std::max<std::size_t>(m_target, catenae::detail::atomic_load_relaxed(st.raised));
            }

            /// SampleRateTap's adaptive raise: a pull larger than any before moves the setpoint to
            /// what it needs, bounded by what the ring holds, never down. The servo keeps its
            /// integrator and slews to it.
            void raise_for(state& st, std::size_t frames) const noexcept {
                if (frames <= st.pull_seen) {
                    return;
                }
                st.pull_seen             = static_cast<std::uint32_t>(frames);
                const std::size_t wanted = std::min(std::max<std::size_t>(target_for(frames), m_target), max_target);
                if (wanted > effective_target(st)) {
                    catenae::detail::atomic_store_relaxed(st.raised, static_cast<std::uint32_t>(wanted));
                }
            }

            /// The ring's fill as the consumer sees it, plus the frames staged in the resampler.
            static double backlog(state& st) noexcept {
                const std::uint32_t head = catenae::detail::atomic_load_acquire(st.head);
                const std::uint32_t tail = catenae::detail::atomic_load_relaxed(st.tail);
                return static_cast<double>(static_cast<std::size_t>(head - tail)
                                           + resampler_t::buffered_frames(st.resampler));
            }

            /// The fill is reached: prime the window, keep the estimate, seed the smoothers.
            void start_paying_out(state& st, std::size_t target) const noexcept {
                resampler_t::reset(st.resampler);
                auto pop = [&st](float* dst, std::size_t most) noexcept { return pop_frames(st, dst, most); };
                static_cast<void>(resampler_t::prime(st.resampler, pop)); // the fill holds target + taps
                m_servo.reset(st.servo, true, static_cast<double>(target));
                pi_servo::seed(st.servo, backlog(st));
                st.running   = 1;
                st.fade_left = static_cast<std::uint32_t>(fade_frames);
            }

            /// A backlog above the high water: the excess above the target dropped from the ring
            /// (frames staged in the resampler cannot be), the smoothers seeded. Returns the backlog.
            static double resync(state& st, std::size_t target, double occ) noexcept {
                const std::uint32_t tail   = catenae::detail::atomic_load_relaxed(st.tail);
                const std::uint32_t head   = catenae::detail::atomic_load_acquire(st.head);
                const double        excess = occ - static_cast<double>(target);
                const std::size_t   drop   = std::min(static_cast<std::size_t>(head - tail),
                                                  excess > 0.0 ? static_cast<std::size_t>(excess) : std::size_t{0});
                catenae::detail::atomic_store_release(st.tail, tail + static_cast<std::uint32_t>(drop));
                catenae::detail::atomic_store_relaxed(st.resyncs,
                                                      catenae::detail::atomic_load_relaxed(st.resyncs) + 1u);
                const double now = backlog(st);
                pi_servo::seed(st.servo, now + resampler_t::mu(st.resampler));
                return now;
            }

            /// A linear ramp over the first `fade_frames` frames after a fill.
            static void fade_in(state& st, float* out, std::size_t made) noexcept {
                const std::size_t n    = std::min<std::size_t>(made, st.fade_left);
                const std::size_t done = fade_frames - st.fade_left;
                for (std::size_t f = 0; f < n; ++f) {
                    const float g = static_cast<float>(done + f + 1) / static_cast<float>(fade_frames);
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[f * Ch + c] *= g;
                    }
                }
                st.fade_left -= static_cast<std::uint32_t>(n);
            }

            /// SampleRateTap's `publish_status`: the state word, the estimate and the smoothed fill.
            static void publish(state& st) noexcept {
                const lock_state now = st.running == 0              ? lock_state::filling
                                       : pi_servo::locked(st.servo) ? lock_state::locked
                                                                    : lock_state::acquiring;
                catenae::detail::atomic_store_relaxed(st.phase, static_cast<std::uint32_t>(now));
                catenae::detail::atomic_store_relaxed(st.ppm_bits,
                                                      detail::bits_of(static_cast<float>(st.servo.eps_hat * 1e6)));
                catenae::detail::atomic_store_relaxed(
                    st.fill_bits, detail::bits_of(static_cast<float>(pi_servo::smoothed_occupancy(st.servo))));
            }

            /// The consumer's read of up to `most` frames from the ring into `dst`.
            static std::size_t pop_frames(state& st, float* dst, std::size_t most) noexcept {
                const std::uint32_t tail  = catenae::detail::atomic_load_relaxed(st.tail);
                const std::uint32_t head  = catenae::detail::atomic_load_acquire(st.head);
                const std::size_t   given = std::min(most, static_cast<std::size_t>(head - tail));
                const std::size_t   start = (tail & (Capacity - 1u)) * Ch;
                const std::size_t   first = std::min(given * Ch, Capacity * Ch - start);
                std::copy(st.ring.begin() + static_cast<std::ptrdiff_t>(start),
                          st.ring.begin() + static_cast<std::ptrdiff_t>(start + first), dst);
                std::copy(st.ring.begin(), st.ring.begin() + static_cast<std::ptrdiff_t>(given * Ch - first),
                          dst + first);
                catenae::detail::atomic_store_release(st.tail, tail + static_cast<std::uint32_t>(given));
                return given;
            }

            /// `frames` frames from `values` into the ring from frame `at`, in at most two runs.
            static void copy_in(state& st, std::uint32_t at, const float* values, std::size_t frames) noexcept {
                const std::size_t start = (at & (Capacity - 1u)) * Ch;
                const std::size_t first = std::min(frames * Ch, Capacity * Ch - start);
                std::copy(values, values + first, st.ring.begin() + static_cast<std::ptrdiff_t>(start));
                std::copy(values + first, values + frames * Ch, st.ring.begin());
            }

            pi_servo      m_servo;
            double        m_sample_rate_hz;
            std::uint32_t m_target;
        };

    } // namespace async
} // namespace catenae_fit

namespace catenae {
    template <std::size_t Ch, std::size_t Capacity, std::size_t Push, typename Bank>
    struct is_junction<catenae_fit::async::junction<Ch, Capacity, Push, Bank>> : std::true_type {};
} // namespace catenae

#endif // CATENAE_FIT_ASYNC_JUNCTION_H
