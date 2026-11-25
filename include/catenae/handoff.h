/// @file handoff.h
/// @brief `handoff`: a ring of frames between two threads of one clock, with a fill policy
/// stated as numbers (D19, D51, PLAN.md 6.1).
///
/// | Contract   |                                                                                 |
/// |------------|---------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` pushed, the same pulled                                           |
/// | Clocks     | shared: one producer thread, one consumer thread, one clock                     |
/// | Configured | by its template arguments: `Capacity` frames, a `Target` fill, the largest `Push` |
/// | State      | the ring and five 32-bit words; at rest all zero                                |
/// | Latency    | nominal `Target` frames, within `Push` of it while it runs                     |
/// | Fill       | silence until `Target`; the oldest above it discarded when it starts paying out |
/// | Limits     | a consumer that stalls raises the latency until an overrun or `restart`        |
/// | Formats    | float, double, q15, q31, Q4.27                                                  |
/// | Method     | a single-producer single-consumer ring over two indices that wrap               |
/// | Rationale  | D19, D44, D51, D52                                                              |
///
/// @req{CAT-HANDOFF-001} A pull delivers silence until the ring holds `Target` frames; the pull
/// that finds them discards the oldest frames above `Target` and pays out. So a pull that
/// starts, or starts again, sees exactly `Target` frames, with the producer 0, 2 or 100 periods
/// ahead and with the consumer ahead; while it runs, the fill a pull sees stays within `Push`
/// of `Target`.
/// @req{CAT-HANDOFF-002} A pull the ring cannot fill is an underrun: it delivers what there is,
/// pads with silence, counts one underrun and fills to `Target` again. A push the ring cannot
/// hold is an overrun: it keeps what fits, drops the newest frames and counts one overrun. A
/// producer held up that then delivers its backlog costs one underrun and returns to `Target`.
/// @req{CAT-HANDOFF-003} `serves(largest_pull, quantum)` is true exactly when `quantum` divides
/// `Push` and `Target + quantum >= largest_pull + Push`: 64-frame pushes and pulls need a target
/// of 64; 480-frame pushes beside 64-frame pulls need 512, and at 480 the ring underruns.
/// @req{CAT-HANDOFF-004} `restart`, on the consumer's thread while the producer runs, discards
/// what is buffered and fills to `Target` again; `reset` empties it while neither side runs.
/// `report` reads the consumer's index before the producer's, so its fill can only
/// over-estimate, and clamps it to the capacity. `push` above `Push` frames, or of input that is
/// not whole frames, and `pull` above `Target` frames, are boundary failures.
/// @req{CAT-HANDOFF-005} Memory orders: the producer writes the ring and then stores its index
/// with release; the consumer loads it with acquire before reading, and stores its own index
/// with release after reading, which the producer loads with acquire before it writes over
/// those frames. A frame is never read while it is written, and two threads move every frame
/// through it untorn and in order. Each other word has one writer: a relaxed load and store.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_HANDOFF_H
#define CATENAE_HANDOFF_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/detail/abi.h"
#include "catenae/fwd.h"
#include "catenae/junction_traits.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// What any thread may read of a handoff: each field one 32-bit word, read on its own.
        struct handoff_report {
            std::uint32_t running;     ///< 1 while it pays out, 0 while it fills to its target
            std::uint32_t fill_frames; ///< frames held, never under the truth, at most the capacity
            std::uint32_t underruns;   ///< pulls it could not fill
            std::uint32_t overruns;    ///< pushes it could not hold
        };

        /// @brief A single-producer single-consumer ring of `Capacity` frames of `port<S, Ch>`
        /// that fills to `Target` frames before it pays out; `Push` is the largest push.
        template <typename S, std::size_t Ch, std::size_t Capacity, std::size_t Target, std::size_t Push>
        class handoff {
            static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0,
                          "catenae: a handoff's capacity is a power of two");
            static_assert(Capacity <= 0x80000000u, "catenae: a handoff holds at most 2^31 frames");
            static_assert(Target != 0 && Push != 0, "catenae: a handoff's target and largest push are not zero");
            static_assert(Target + Push <= Capacity, "catenae: a handoff's capacity holds its target and one push");

          public:
            using sink_port   = port<S, Ch>; ///< what push takes
            using source_port = port<S, Ch>; ///< what pull fills

          private:
            using value_type                          = storage_t<S>;
            static constexpr std::uint32_t k_capacity = static_cast<std::uint32_t>(Capacity);
            static constexpr std::uint32_t k_target   = static_cast<std::uint32_t>(Target);

          public:
            static constexpr clocks      clocking = clocks::shared;  ///< two threads, one clock
            static constexpr ratio       rate_ratio{1, 1};           ///< a frame out for a frame in
            static constexpr std::size_t capacity_frames = Capacity; ///< a power of two
            static constexpr std::size_t target_frames   = Target;   ///< the fill it pays out from
            static constexpr std::size_t largest_push    = Push;     ///< the producer's largest push

            /// The ring and the words its two sides share: plain storage, at rest all zero.
            struct state {
                std::array<value_type, Capacity * Ch> ring;      ///< frame f at f % Capacity
                std::uint32_t                         head;      ///< frames pushed, wrapping; the producer's
                std::uint32_t                         tail;      ///< frames pulled or discarded; the consumer's
                std::uint32_t                         running;   ///< 1 while it pays out; the consumer's
                std::uint32_t                         underruns; ///< the consumer's count
                std::uint32_t                         overruns;  ///< the producer's count
            };
            using report_type = handoff_report; ///< what report returns

            /// Whether pulls of up to `largest_pull` frames never run it dry, when `quantum`
            /// divides every push and every pull (1 when their sizes vary).
            static constexpr bool serves(std::size_t largest_pull, std::size_t quantum) noexcept {
                return quantum != 0 && Push % quantum == 0 && Target + quantum >= largest_pull + Push;
            }

            /// The producer's thread, and only it: up to `Push` frames. Returns the frames kept;
            /// the rest, the newest, are dropped and counted as one overrun.
            [[nodiscard]] std::size_t push(state& st, span<const value_type> in) const noexcept {
                detail::check_always(in.size() % Ch == 0, check::input_not_whole_frames);
                const std::size_t asked = in.size() / Ch;
                detail::check_always(asked <= Push, check::push_too_large);
                const std::uint32_t head  = detail::atomic_load_relaxed(st.head);
                const std::uint32_t tail  = detail::atomic_load_acquire(st.tail);
                const std::size_t   room  = Capacity - static_cast<std::size_t>(head - tail);
                const std::size_t   taken = std::min(asked, room);
                copy_in(st, head, in.data(), taken);
                detail::atomic_store_release(st.head, head + static_cast<std::uint32_t>(taken));
                if (taken < asked) {
                    detail::atomic_store_relaxed(st.overruns, detail::atomic_load_relaxed(st.overruns) + 1u);
                }
                return taken;
            }

            /// The consumer's thread, and only it: up to `Target` frames. Always fills `out`;
            /// what it could not supply is silence. Returns the real frames.
            [[nodiscard]] std::size_t pull(state& st, span<value_type> out) const noexcept {
                detail::check_always(out.size() % Ch == 0, check::input_not_whole_frames);
                const std::size_t want = out.size() / Ch;
                detail::check_always(want <= Target, check::pull_too_large);
                std::uint32_t       tail = detail::atomic_load_relaxed(st.tail);
                const std::uint32_t head = detail::atomic_load_acquire(st.head);
                std::uint32_t       fill = head - tail;
                if (detail::atomic_load_relaxed(st.running) == 0) {
                    if (fill < k_target) {
                        std::fill(out.begin(), out.end(), value_type{});
                        return 0;
                    }
                    tail = head - k_target; // the oldest frames above the target, discarded
                    fill = k_target;
                    detail::atomic_store_relaxed(st.running, 1u);
                }
                const std::size_t given = std::min(want, static_cast<std::size_t>(fill));
                copy_out(st, tail, out.data(), given);
                std::fill(out.begin() + static_cast<std::ptrdiff_t>(given * Ch), out.end(), value_type{});
                detail::atomic_store_release(st.tail, tail + static_cast<std::uint32_t>(given));
                if (given < want) {
                    detail::atomic_store_relaxed(st.underruns, detail::atomic_load_relaxed(st.underruns) + 1u);
                    detail::atomic_store_relaxed(st.running, 0u); // fill to the target again
                }
                return given;
            }

            /// The consumer's thread, while the producer runs: what is buffered is discarded,
            /// and it fills to the target again.
            void restart(state& st) const noexcept {
                detail::atomic_store_release(st.tail, detail::atomic_load_acquire(st.head));
                detail::atomic_store_relaxed(st.running, 0u);
            }

            /// Empty, and its counts zero. Only while neither side runs; a static state that was
            /// never used is at rest as it stands.
            void reset(state& st) const noexcept {
                detail::atomic_store_relaxed(st.head, 0u);
                detail::atomic_store_relaxed(st.tail, 0u);
                detail::atomic_store_relaxed(st.running, 0u);
                detail::atomic_store_relaxed(st.underruns, 0u);
                detail::atomic_store_release(st.overruns, 0u);
            }

            /// Any thread. The consumer's index is read before the producer's, so the fill can
            /// only over-estimate; it is clamped to the capacity.
            report_type report(const state& st) const noexcept {
                // The atomic loads take a word they may read-modify-write (MSVC's is an
                // interlocked OR with 0), so the shared state is reached through a non-const view.
                state&              shared = const_cast<state&>(st);
                const std::uint32_t tail   = detail::atomic_load_acquire(shared.tail);
                const std::uint32_t head   = detail::atomic_load_acquire(shared.head);
                const std::uint32_t fill   = head - tail;
                return report_type{detail::atomic_load_relaxed(shared.running), fill < k_capacity ? fill : k_capacity,
                                   detail::atomic_load_relaxed(shared.underruns),
                                   detail::atomic_load_relaxed(shared.overruns)};
            }

            /// `Target` frames, within `Push` of it while it runs.
            junction_latency latency(const state&) const noexcept {
                return {static_cast<float>(Target), static_cast<float>(Push)};
            }

          private:
            /// `frames` frames from `values` into the ring from frame `at`, in at most two runs.
            static void copy_in(state& st, std::uint32_t at, const value_type* values, std::size_t frames) noexcept {
                const std::size_t start = (at & (k_capacity - 1u)) * Ch;
                const std::size_t first = std::min(frames * Ch, Capacity * Ch - start);
                std::copy(values, values + first, st.ring.begin() + static_cast<std::ptrdiff_t>(start));
                std::copy(values + first, values + frames * Ch, st.ring.begin());
            }
            /// `frames` frames from the ring from frame `at` into `values`, in at most two runs.
            static void copy_out(const state& st, std::uint32_t at, value_type* values, std::size_t frames) noexcept {
                const std::size_t start = (at & (k_capacity - 1u)) * Ch;
                const std::size_t first = std::min(frames * Ch, Capacity * Ch - start);
                std::copy(st.ring.begin() + static_cast<std::ptrdiff_t>(start),
                          st.ring.begin() + static_cast<std::ptrdiff_t>(start + first), values);
                std::copy(st.ring.begin(), st.ring.begin() + static_cast<std::ptrdiff_t>(frames * Ch - first),
                          values + first);
            }
        };

        template <typename S, std::size_t Ch, std::size_t Capacity, std::size_t Target, std::size_t Push>
        struct is_junction<handoff<S, Ch, Capacity, Target, Push>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_HANDOFF_H
