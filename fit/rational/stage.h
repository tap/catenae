/// @file stage.h
/// @brief `stage<S, Ch, Ratio, Design>`: one Nyquist stage at a compile-time L/M (CF skeleton).
///
/// | Contract   |                                                                                |
/// |------------|--------------------------------------------------------------------------------|
/// | Ports      | `port<float, Ch>` in and out (CF instantiates float only)                      |
/// | Ratio      | L/M of `Ratio`, a `ratio<L, M>` of the charter                                 |
/// | Machines   | L >= 2: the L-phase polyphase machine on bridge's (phase, advance) schedule;  |
/// |            | L = 1: the M-branch commutator                                                 |
/// | Configured | by its type alone: `Design` names the table, so the object is empty (D54)      |
/// | State      | the history lines, where each ends, the schedule position, the pending gap and |
/// |            | the decimator's phase; reset to the row length with one pre-advance pending   |
/// | Arrival    | input group M, no shortfall, lead (M - 1)/M, output quantum L when M = 1 else 1 |
/// | Counts     | n frames from reset write ceil(n L / M): one frame in from rest writes one out |
/// | Latency    | exactly (N - 1) / (2 M) output frames, a constant of the type (D39)            |
/// | Tail       | SampleRateTap's window: N - 1 frames decimating, the row length otherwise; no residue |
/// | Method     | y[n] = sum_k x[floor(n M / L) - k] h[(n M mod L) + k L], x[< 0] = 0: scipy's     |
/// |            | upfirdn(h, x, up = L, down = M); rows tap-reversed and trimmed to their nonzero span |
/// | Accumulator| float, in the carried tap order (D36; SampleRateTap's float accumulated in double) |
///
// Copyright 2026 Timothy Place and the catenae contributors
//
// Ported from SampleRateTap's rational/include/tap/sr/rational/stage.h at 4496888: the
// schedule (schedule_entry with a 16-bit advance, make_schedule, and schedule_frames_needed
// folded into frames_needed), stage_base's outputs_for,
// frames_needed, reset, latency and window_frames, build_table and trim_row, and basic_stage's
// process (here run), cursor, open / close, append_frame, compact and emit. Not carried: the
// stage's own pull() (fed_by pulls for a chain, PLAN.md 6.1) and flush() (the chain's, PLAN.md
// 4), validate() and the profile object (a design is a type), the fixed-point table gains
// (float only), the out-of-line and forced-inline attributes (a codegen lever, measured at C7).
//
// The table is built at compile time from the design's double prototype as SampleRateTap's
// constructor builds it at run time: a decimator's taps scaled by 1/M and scattered to its M
// branches, each stored oldest first and right-aligned; an interpolator's or a mixed ratio's L
// phases, each tap-reversed, normalized to unit sum when going down; every row trimmed to its
// nonzero span; each tap rounded to float once. The rows are a constant of the stage's type,
// in ROM, shared by every stage of that type.
#ifndef CATENAE_FIT_RATIONAL_STAGE_H
#define CATENAE_FIT_RATIONAL_STAGE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "rational/design.h"
#include "rational/ratio.h"

namespace catenae_fit::rational {

    namespace detail {

        /// One output's step of the L-phase machine: the phase to dot and the inputs to consume
        /// after it (bridge's schedule_entry).
        struct schedule_entry {
            std::uint16_t phase;   ///< polyphase branch in [0, L)
            std::uint16_t advance; ///< input frames consumed after this output
        };

        /// The superblock of L outputs: phase(n) = n M mod L, advances summing to M.
        template <typename R>
        constexpr std::array<schedule_entry, R::up> make_schedule() noexcept {
            std::array<schedule_entry, R::up> s{};
            for (std::uint32_t n = 0; n < R::up; ++n) {
                s[n].phase   = static_cast<std::uint16_t>((n * R::down) % R::up);
                s[n].advance = static_cast<std::uint16_t>(((n + 1) * R::down) / R::up - (n * R::down) / R::up);
            }
            return s;
        }

        /// The schedule of a ratio, a constant of its type.
        template <typename R>
        struct schedule {
            static constexpr std::array<schedule_entry, R::up> k_entries = make_schedule<R>();
        };

        /// Whether a tap is zero, without comparing floats for equality.
        constexpr bool is_zero(float v) noexcept {
            return !(v < 0.0f) && !(v > 0.0f);
        }

        /// The geometry of a stage's table: rows of row_len taps, tap-reversed.
        template <typename R, typename Design>
        struct geometry {
            static constexpr bool        k_is_decimator = R::up == 1;
            static constexpr std::size_t k_taps         = Design::taps;
            /// Rows: the M branches of a decimator, else the L phases.
            static constexpr std::size_t k_rows = k_is_decimator ? R::down : R::up;
            /// T: taps per row, ceil(N / M) decimating, ceil(N / L) otherwise.
            static constexpr std::size_t k_row_len = (k_taps + k_rows - 1) / k_rows;
        };

        /// A stage's rows and each row's nonzero span.
        template <typename R, typename Design>
        struct table {
            using geo = geometry<R, Design>;
            std::array<float, geo::k_rows * geo::k_row_len> rows;  ///< k_rows x T, tap-reversed
            std::array<std::uint32_t, geo::k_rows>          first; ///< nonzero span start per row
            std::array<std::uint32_t, geo::k_rows>          count; ///< nonzero span length per row
        };

        /// Records the nonzero span of row r.
        template <typename R, typename Design>
        constexpr void trim_row(table<R, Design>& t, std::size_t r) noexcept {
            constexpr std::size_t len   = geometry<R, Design>::k_row_len;
            std::size_t           first = 0;
            while (first < len && is_zero(t.rows[r * len + first])) {
                ++first;
            }
            std::size_t last = len;
            while (last > first && is_zero(t.rows[r * len + last - 1])) {
                --last;
            }
            t.first[r] = static_cast<std::uint32_t>(first);
            t.count[r] = static_cast<std::uint32_t>(last - first);
        }

        /// SampleRateTap's build_table, at compile time (the file header).
        template <typename R, typename Design>
        constexpr table<R, Design> make_table() noexcept {
            using geo                 = geometry<R, Design>;
            constexpr std::size_t n   = geo::k_taps;
            constexpr std::size_t len = geo::k_row_len;
            const auto&           h   = Design::prototype;
            table<R, Design>      t{};
            if constexpr (geo::k_is_decimator) {
                constexpr std::size_t m = R::down;
                for (std::size_t j = 0; j < m; ++j) {
                    const std::size_t taps = (n - j + m - 1) / m; // taps j, j + M, ...
                    for (std::size_t s = 0; s < taps; ++s) {
                        t.rows[j * len + (len - 1 - s)] = static_cast<float>(h[j + s * m] / static_cast<double>(m));
                    }
                    trim_row(t, j);
                }
            }
            else {
                constexpr std::size_t l = R::up;
                for (std::size_t p = 0; p < l; ++p) {
                    std::array<double, len> row{};
                    double                  sum = 0.0;
                    for (std::size_t k = 0; k < len; ++k) {
                        const std::size_t i = p + k * l;
                        const double      v = i < n ? h[i] : 0.0;
                        row[len - 1 - k]    = v;
                        sum += v;
                    }
                    for (std::size_t k = 0; k < len; ++k) {
                        // A mixed ratio going down: its L phases are not the design's branches, so
                        // each is normalized to unit DC gain.
                        t.rows[p * len + k] = static_cast<float>(ratio_traits<R>::is_up ? row[k] : row[k] / sum);
                    }
                    trim_row(t, p);
                }
            }
            return t;
        }

        /// A stage's table, a constant of its type: in ROM once per (ratio, design).
        template <typename R, typename Design>
        struct rows_of {
            static constexpr table<R, Design> k_value = make_table<R, Design>();
        };

    } // namespace detail

    /// @brief One Nyquist stage at ratio `Ratio` over `port<S, Ch>`, its filter `Design`.
    template <typename S, std::size_t Ch, typename Ratio, typename Design>
    class stage {
        static_assert(std::is_same<S, float>::value, "catenae_fit::rational: the CF skeleton is float only");
        static_assert(Ch >= 1, "catenae_fit::rational: a stage has at least one channel");
        static_assert(is_ratio<Ratio>::value, "catenae_fit::rational: a stage's Ratio is a ratio<L, M>");
        static_assert(Design::band == ratio_traits<Ratio>::band,
                      "catenae_fit::rational: a stage's design is a max(L, M)-th-band filter");
        static_assert(is_nyquist_table<Design>(),
                      "catenae_fit::rational: a design's table has the Nyquist structure: centre 1, every band-th "
                      "tap from it 0, symmetric");

        using geo                                   = detail::geometry<Ratio, Design>;
        using rows                                  = detail::rows_of<Ratio, Design>;
        static constexpr std::size_t k_up           = Ratio::up;
        static constexpr std::size_t k_down         = Ratio::down;
        static constexpr bool        k_is_decimator = geo::k_is_decimator;
        static constexpr std::size_t k_rows         = geo::k_rows;
        static constexpr std::size_t k_row_len      = geo::k_row_len;
        // SampleRateTap's slack: appends between compactions, a speed lever that is RAM here. CF
        // finding 9: a state is sized by its type (D22), so these 64 frames a line are .bss per
        // instance: the short designs here read 2 to 4 frames a line and keep 66 to 68 (1,112
        // bytes for the stereo up2_ratio3over2, 2,724 for down3_down2). A template argument
        // would let C7 choose.
        static constexpr std::size_t   k_hist_slack = 64;
        static constexpr std::size_t   k_cap        = k_row_len + k_hist_slack;
        static constexpr std::size_t   k_lines      = k_is_decimator ? k_down : 1; ///< lines per channel
        static constexpr std::uint32_t k_taps       = static_cast<std::uint32_t>(Design::taps);

      public:
        using input_port  = catenae::port<float, Ch>; ///< what it reads
        using output_port = catenae::port<float, Ch>; ///< what it writes
        using design      = Design;                   ///< the filter: a type, its table a constant

        static constexpr catenae::ratio rate_ratio  = Ratio::value; ///< L/M
        static constexpr std::size_t    input_group = k_down;       ///< M frames write exactly L
        /// Never behind: a group's output is written when its first frame arrives (PLAN.md 12).
        static constexpr catenae::ratio max_shortfall{0, 1};
        /// Up to (M - 1) / M ahead: one frame in from rest writes one out.
        static constexpr catenae::ratio max_lead{static_cast<std::uint32_t>(k_down - 1),
                                                 static_cast<std::uint32_t>(k_down)};
        /// An interpolator writes L per frame; any other stage single frames.
        static constexpr std::size_t output_quantum    = k_down == 1 ? k_up : 1;
        static constexpr bool        is_in_place       = false;
        static constexpr bool        has_exact_latency = true;
        /// (N - 1) / 2 samples at the composite rate L f_in: (N - 1) / (2 M) output frames.
        static constexpr catenae::ratio exact_latency =
            catenae::reduce(catenae::ratio{(k_taps - 1) / 2, static_cast<std::uint32_t>(k_down)});

        /// Everything one instance remembers. Planar inside, interleaved at the ports: channel c's
        /// sub-line `sub` is hist[(c k_lines + sub) k_cap ...].
        struct state {
            std::array<float, Ch * k_lines * k_cap> hist;    ///< the history lines
            std::array<std::uint32_t, k_lines>      end;     ///< newest sample + 1, per sub-line
            std::uint32_t                           pos;     ///< superblock position in [0, L)
            std::uint32_t                           pending; ///< inputs before the next output
            std::uint32_t                           dphase;  ///< inputs since the last output (decimator)
        };
        using workspace = catenae::no_workspace; ///< nothing

        /// Each line holds a row of silence, the schedule at its start, the pre-advance pending:
        /// the next input frame writes an output (PLAN.md 12, "Reset values").
        void reset(state& st) const noexcept {
            st.hist.fill(0.0f);
            st.end.fill(static_cast<std::uint32_t>(k_row_len));
            st.pos     = 0;
            st.pending = 1;
            st.dphase  = 0;
        }

        /// Consumes every input frame and writes outputs_for(frames) frames.
        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            const std::size_t frames   = in.size() / Ch;
            std::size_t       produced = 0;
            cursor            b        = open(st);
            if constexpr (k_is_decimator) {
                for (std::size_t i = 0; i < frames; ++i) {
                    append_frame(b, in.data() + i * Ch);
                    if (b.dphase == 0) {
                        emit(b, out.data() + produced * Ch);
                        ++produced;
                    }
                    b.dphase = b.dphase + 1 == k_down ? 0 : b.dphase + 1;
                }
            }
            else {
                const std::size_t total = outputs_for(st, frames);
                std::size_t       fed   = 0;
                for (; produced < total; ++produced) {
                    while (b.pending != 0) { // never outruns the input: total is exact
                        append_frame(b, in.data() + fed * Ch);
                        ++fed;
                        --b.pending;
                    }
                    emit(b, out.data() + produced * Ch);
                }
                while (fed < frames) { // what is left is fewer than the next gap
                    append_frame(b, in.data() + fed * Ch);
                    ++fed;
                    --b.pending;
                }
            }
            close(b, st);
            return produced;
        }

        /// Exactly what the next n frames write, from the schedule position or decimation phase.
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            if constexpr (k_is_decimator) {
                const std::size_t first = 1 + (k_down - st.dphase) % k_down;
                return n < first ? 0 : 1 + (n - first) / k_down;
            }
            else {
                if (n < st.pending) {
                    return 0;
                }
                const std::size_t base = st.pos * k_down / k_up;
                const std::size_t a    = n - st.pending;
                return (k_up * (base + a + 1) - 1) / k_down - st.pos + 1;
            }
        }
        /// The smallest n with outputs_for(n) >= k.
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            if (k == 0) {
                return 0;
            }
            if constexpr (k_is_decimator) {
                return 1 + (k_down - st.dphase) % k_down + (k - 1) * k_down;
            }
            else {
                // floor((pos + k - 1) M / L) - floor(pos M / L) after the pending gap.
                const std::size_t p = st.pos;
                return st.pending + (p + k - 1) * k_down / k_up - p * k_down / k_up;
            }
        }
        /// ceil(n L / M): from any state run reaches, n frames write no more.
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept {
            return (n * k_up + k_down - 1) / k_down;
        }

        /// The exact latency as a float, deviation 0.
        catenae::stage_latency latency() const noexcept {
            return {static_cast<float>(exact_latency.up) / static_cast<float>(exact_latency.down), 0.0f};
        }
        /// SampleRateTap's window_frames: the longest history any row reads.
        ///
        /// CF finding 4: the contract asks for the zero frames "after which" the output is silence
        /// (docs/contracts/stage.md) and not the fewest. The L-phase machine's window, T, is one
        /// more than it needs (T - 1, measured on every stage here); the decimator's, N - 1, is
        /// the least. SampleRateTap's is kept so that a one-stage chain's flush writes what its
        /// flush() writes; a chain of them does not (the composed tail is fed at the input,
        /// where DspTap's chain feeds each stage its window at its own input).
        std::size_t tail_frames() const noexcept { return k_is_decimator ? k_taps - 1u : k_row_len; }
        /// The same: an FIR's output is exactly silence after its tail.
        std::size_t practical_tail_frames() const noexcept { return tail_frames(); }
        /// None.
        float flush_residue() const noexcept { return 0.0f; }

        /// The table's row r at tap t in storage order (t = 0 the oldest sample's), for tests.
        static constexpr float coefficient(std::size_t r, std::size_t t) noexcept {
            return rows::k_value.rows[r * k_row_len + t];
        }
        static constexpr std::size_t row_length() noexcept { return k_row_len; } ///< T
        static constexpr std::size_t taps() noexcept { return k_taps; }          ///< N

      private:
        /// The loops' view of the state for one call: SampleRateTap's cursor, the stream state
        /// as locals, loaded by open and stored back by close.
        struct cursor {
            float*                             hist;
            std::array<std::uint32_t, k_lines> end;
            std::uint32_t                      pos;
            std::uint32_t                      pending;
            std::uint32_t                      dphase;
        };

        static cursor open(state& st) noexcept { return cursor{st.hist.data(), st.end, st.pos, st.pending, st.dphase}; }
        static void   close(const cursor& b, state& st) noexcept {
            st.end     = b.end;
            st.pos     = b.pos;
            st.pending = b.pending;
            st.dphase  = b.dphase;
        }

        /// Channel c's sub-line sub.
        static float* line(const cursor& b, std::size_t c, std::size_t sub) noexcept {
            return b.hist + (c * k_lines + sub) * k_cap;
        }

        /// Slides the newest T - 1 samples of sub-line sub to the front of every channel's line.
        static void compact(cursor& b, std::size_t sub) noexcept {
            constexpr std::size_t keep = k_row_len - 1;
            for (std::size_t c = 0; c < Ch; ++c) {
                float* l = line(b, c, sub);
                std::memmove(l, l + (b.end[sub] - keep), keep * sizeof(float));
            }
            b.end[sub] = static_cast<std::uint32_t>(keep);
        }

        /// Appends one interleaved frame to the history (the decimator's current sub-line).
        static void append_frame(cursor& b, const float* frame) noexcept {
            const std::size_t sub = k_is_decimator ? b.dphase : 0;
            if (b.end[sub] == k_cap) {
                compact(b, sub);
            }
            for (std::size_t c = 0; c < Ch; ++c) {
                line(b, c, sub)[b.end[sub]] = frame[c];
            }
            ++b.end[sub];
        }

        /// acc plus the tap-order dot of a row against a window.
        static float accumulate(float acc, const float* row, const float* window, std::size_t count) noexcept {
            for (std::size_t t = 0; t < count; ++t) {
                acc += window[t] * row[t];
            }
            return acc;
        }

        /// One output frame at the current position; the L-phase machine advances its schedule.
        static void emit(cursor& b, float* out) noexcept {
            constexpr const auto& table = rows::k_value;
            if constexpr (k_is_decimator) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    float acc = 0.0f;
                    // Every branch through one accumulator: y[k] = sum_j dot(branch j, sub-line
                    // (M - j) mod M).
                    for (std::size_t j = 0; j < k_rows; ++j) {
                        const std::size_t sub    = (k_down - j) % k_down;
                        const float*      window = line(b, c, sub) + b.end[sub] - k_row_len + table.first[j];
                        acc =
                            accumulate(acc, table.rows.data() + j * k_row_len + table.first[j], window, table.count[j]);
                    }
                    out[c] = acc;
                }
            }
            else {
                const detail::schedule_entry step = detail::schedule<Ratio>::k_entries[b.pos];
                const float*                 row = table.rows.data() + step.phase * k_row_len + table.first[step.phase];
                const std::size_t            count  = table.count[step.phase];
                const float*                 window = line(b, 0, 0) + b.end[0] - k_row_len + table.first[step.phase];
                for (std::size_t c = 0; c < Ch; ++c, window += k_cap) {
                    out[c] = accumulate(0.0f, row, window, count);
                }
                b.pending = step.advance;
                b.pos     = b.pos + 1 == k_up ? 0 : b.pos + 1;
            }
        }
    };

} // namespace catenae_fit::rational

namespace catenae {
    template <typename S, std::size_t Ch, typename Ratio, typename Design>
    struct is_stage<catenae_fit::rational::stage<S, Ch, Ratio, Design>> : std::true_type {};
} // namespace catenae

#endif // CATENAE_FIT_RATIONAL_STAGE_H
