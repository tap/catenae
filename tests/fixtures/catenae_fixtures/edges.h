/// @file edges.h
/// @brief The battery's toy junction and toy analyzer, for junction_traits and analyzer_traits.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Test code, not library code: the smallest members that meet each contract, so the traits'
// tests have something conforming to read and something to break one member at a time.
//
//   ring_junction<Ch, Capacity>  shared clocks, 1:1: a single-producer single-consumer ring of
//                                Capacity frames, its two indices plain words under the atomic
//                                operations of catenae/compiler.h (PLAN.md section 6.1).
//   peak_meter<Ch, Hop>          an analyzer: the largest magnitude of each Hop frames, one
//                                result a hop (PLAN.md section 6.2).
#ifndef CATENAE_TESTS_FIXTURES_CATENAE_FIXTURES_EDGES_H
#define CATENAE_TESTS_FIXTURES_CATENAE_FIXTURES_EDGES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/compiler.h"
#include "catenae/junction_traits.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"

namespace catenae_fixtures {

    /// A ring of Capacity frames between one pushing and one pulling thread.
    template <std::size_t Ch, std::size_t Capacity>
    class ring_junction {
      public:
        using sink_port                           = catenae::port<float, Ch>;
        using source_port                         = catenae::port<float, Ch>;
        static constexpr catenae::clocks clocking = catenae::clocks::shared;
        static constexpr catenae::ratio  rate_ratio{1, 1};
        static constexpr std::size_t     capacity_frames = Capacity;

        struct state {
            std::array<float, Capacity * Ch> ring;
            std::uint32_t                    written; // frames pushed, wrapping; the producer's word
            std::uint32_t                    read;    // frames pulled, wrapping; the consumer's word
        };
        struct report_type {
            std::uint32_t fill; // frames held when the report was taken
        };

        /// Pushes what fits; returns the frames taken.
        std::size_t push(state& st, catenae::span<const float> in) const noexcept {
            const std::uint32_t written = catenae::detail::atomic_load_relaxed(st.written);
            const std::uint32_t read    = catenae::detail::atomic_load_acquire(st.read);
            const std::size_t   room    = Capacity - (written - read);
            const std::size_t   frames  = in.size() / Ch < room ? in.size() / Ch : room;
            for (std::size_t f = 0; f < frames; ++f) {
                const std::size_t slot = (written + f) % Capacity;
                for (std::size_t c = 0; c < Ch; ++c) {
                    st.ring[slot * Ch + c] = in[f * Ch + c];
                }
            }
            catenae::detail::atomic_store_release(st.written, written + static_cast<std::uint32_t>(frames));
            return frames;
        }
        /// Pulls what is held, up to the output's size; returns the frames written.
        std::size_t pull(state& st, catenae::span<float> out) const noexcept {
            const std::uint32_t read    = catenae::detail::atomic_load_relaxed(st.read);
            const std::uint32_t written = catenae::detail::atomic_load_acquire(st.written);
            const std::size_t   held    = written - read;
            const std::size_t   frames  = out.size() / Ch < held ? out.size() / Ch : held;
            for (std::size_t f = 0; f < frames; ++f) {
                const std::size_t slot = (read + f) % Capacity;
                for (std::size_t c = 0; c < Ch; ++c) {
                    out[f * Ch + c] = st.ring[slot * Ch + c];
                }
            }
            catenae::detail::atomic_store_release(st.read, read + static_cast<std::uint32_t>(frames));
            return frames;
        }
        static constexpr bool serves(std::size_t largest_pull, std::size_t quantum) noexcept {
            return largest_pull + quantum <= Capacity;
        }
        report_type               report(const state& st) const noexcept { return {st.written - st.read}; }
        catenae::junction_latency latency(const state& st) const noexcept {
            return {static_cast<float>(st.written - st.read), 0.0f};
        }
        void reset(state& st) const noexcept {
            st.written = 0;
            st.read    = 0;
        }
        void restart(state& st) const noexcept { st.read = st.written; }
    };

    /// The largest magnitude over each Hop frames of every channel.
    template <std::size_t Ch, std::size_t Hop>
    class peak_meter {
      public:
        using input_port                           = catenae::port<float, Ch>;
        using result_type                          = float;
        static constexpr std::size_t hop_frames    = Hop;
        static constexpr std::size_t window_frames = Hop;
        static constexpr bool        rests_at_zero = true;

        struct state {
            float         running; // the largest magnitude in the hop so far
            float         last;    // the latest result
            std::uint32_t frames;  // frames into the hop
            std::uint32_t ready;   // results so far, wrapping
        };

        void analyze(state& st, catenae::span<const float> in) const noexcept {
            for (std::size_t frame = 0; frame < in.size() / Ch; ++frame) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    const float v = in[frame * Ch + c] < 0.0f ? -in[frame * Ch + c] : in[frame * Ch + c];
                    st.running    = v > st.running ? v : st.running;
                }
                if (++st.frames == Hop) {
                    st.last    = st.running;
                    st.running = 0.0f;
                    st.frames  = 0;
                    ++st.ready;
                }
            }
        }
        const float&  result(const state& st) const noexcept { return st.last; }
        std::uint32_t results_ready(const state& st) const noexcept { return st.ready; }
    };

} // namespace catenae_fixtures

namespace catenae {
    template <std::size_t Ch, std::size_t Capacity>
    struct is_junction<catenae_fixtures::ring_junction<Ch, Capacity>> : std::true_type {};
    template <std::size_t Ch, std::size_t Hop>
    struct is_analyzer<catenae_fixtures::peak_meter<Ch, Hop>> : std::true_type {};
} // namespace catenae

#endif // CATENAE_TESTS_FIXTURES_CATENAE_FIXTURES_EDGES_H
