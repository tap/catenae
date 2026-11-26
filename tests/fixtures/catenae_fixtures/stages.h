/// @file stages.h
/// @brief The battery's fixture stages: a block stage, a toy 1:2 and a toy 2:1 in both conventions.
// Copyright 2026 Timothy Place and the catenae contributors
//
// C1's named test fixtures (PLAN.md section 10), brought forward to C0b so the generic harness
// (catenae_test/harness.h) proves itself on stages with real input groups, leads and trails.
// They are test code, not library code: float, interleaved, the simplest arithmetic that shows
// each arrival pattern exactly.
//
//   block_stage<Ch, Hop>  1:1 in whole blocks: holds frames until Hop have arrived, then writes
//                         them. input_group Hop, max_shortfall Hop - 1, output_quantum Hop.
//   up2_toy<Ch>           1:2: each frame written twice. Frame-synchronous, output_quantum 2.
//   down2_first<Ch>       2:1, writing a pair's output when its first frame arrives (the carried
//                         decimators' convention): input_group 2, max_lead 1/2, no shortfall.
//   down2_last<Ch>        2:1, writing it when the pair's last frame arrives: input_group 2,
//                         max_shortfall 1/2, no lead.
//   table_gain<Ch>        1:1, channel c times level c of a table it holds by view: the
//                         smallest stage that says it holds one (holds_view).
#ifndef CATENAE_TESTS_FIXTURES_CATENAE_FIXTURES_STAGES_H
#define CATENAE_TESTS_FIXTURES_CATENAE_FIXTURES_STAGES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae_fixtures {

    /// 1:1 in blocks of Hop frames: a stand-in for a transform region (PLAN.md section 9.5).
    template <std::size_t Ch, std::size_t Hop>
    class block_stage {
      public:
        using input_port  = catenae::port<float, Ch>;
        using output_port = catenae::port<float, Ch>;
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = Hop;
        static constexpr catenae::ratio max_shortfall{static_cast<std::uint32_t>(Hop - 1), 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = Hop;
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};
        static constexpr bool           rests_at_zero = true;

        struct state {
            std::array<float, Hop * Ch> held;
            std::uint32_t               count; // frames held, below Hop
        };
        using workspace = catenae::no_workspace;

        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            std::size_t written = 0;
            for (std::size_t frame = 0; frame < in.size() / Ch; ++frame) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    st.held[st.count * Ch + c] = in[frame * Ch + c];
                }
                if (++st.count == Hop) {
                    for (std::size_t i = 0; i < Hop * Ch; ++i) {
                        out[written * Ch + i] = st.held[i];
                    }
                    written += Hop;
                    st.count = 0;
                }
            }
            return written;
        }

        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            return (st.count + n) / Hop * Hop;
        }
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            return k == 0 ? 0 : (k + Hop - 1) / Hop * Hop - st.count;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return (Hop - 1 + n) / Hop * Hop; }
        static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept { return n / Hop * Hop; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return Hop - 1; }
        std::size_t                  practical_tail_frames() const noexcept { return Hop - 1; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

    /// 1:2, each frame written twice (a zero-order hold).
    template <std::size_t Ch>
    class up2_toy {
      public:
        using input_port  = catenae::port<float, Ch>;
        using output_port = catenae::port<float, Ch>;
        static constexpr catenae::ratio rate_ratio{2, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = 2;
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{1, 2};
        static constexpr bool           rests_at_zero = true;

        struct state {};
        using workspace = catenae::no_workspace;

        std::size_t run(state&, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    out[(2 * frame) * Ch + c]     = in[frame * Ch + c];
                    out[(2 * frame + 1) * Ch + c] = in[frame * Ch + c];
                }
            }
            return 2 * frames;
        }

        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return 2 * n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return (k + 1) / 2; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return 2 * n; }
        catenae::stage_latency       latency() const noexcept { return {0.5f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

    /// 2:1, keeping one frame of each pair. `WritesOnFirst`: the output is the pair's first frame,
    /// written when it arrives; otherwise its last, written when that arrives.
    template <std::size_t Ch, bool WritesOnFirst>
    class down2_toy {
      public:
        using input_port  = catenae::port<float, Ch>;
        using output_port = catenae::port<float, Ch>;
        static constexpr catenae::ratio rate_ratio{1, 2};
        static constexpr std::size_t    input_group = 2;
        static constexpr catenae::ratio max_shortfall{WritesOnFirst ? 0u : 1u, WritesOnFirst ? 1u : 2u};
        static constexpr catenae::ratio max_lead{WritesOnFirst ? 1u : 0u, WritesOnFirst ? 2u : 1u};
        static constexpr std::size_t    output_quantum    = 1;
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{WritesOnFirst ? 0u : 1u, WritesOnFirst ? 1u : 2u};
        static constexpr bool           rests_at_zero = true;

        struct state {
            std::uint32_t phase; // 0: the next frame is a pair's first
        };
        using workspace = catenae::no_workspace;

        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            std::size_t written = 0;
            for (std::size_t frame = 0; frame < in.size() / Ch; ++frame) {
                if ((st.phase == 0) == WritesOnFirst) {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[written * Ch + c] = in[frame * Ch + c];
                    }
                    ++written;
                }
                st.phase = 1 - st.phase;
            }
            return written;
        }

        /// The frames among the next n that write: those at the writing phase.
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            const bool next_writes = (st.phase == 0) == WritesOnFirst;
            return next_writes ? (n + 1) / 2 : n / 2;
        }
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            const bool next_writes = (st.phase == 0) == WritesOnFirst;
            return k == 0 ? 0 : (next_writes ? 2 * k - 1 : 2 * k);
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return (n + 1) / 2; }
        static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept {
            return WritesOnFirst ? (n + 1) / 2 : n / 2;
        }
        catenae::stage_latency latency() const noexcept { return {WritesOnFirst ? 0.0f : 0.5f, 0.0f}; }
        std::size_t            tail_frames() const noexcept { return 0; }
        std::size_t            practical_tail_frames() const noexcept { return 0; }
        float                  flush_residue() const noexcept { return 0.0f; }
    };

    template <std::size_t Ch>
    using down2_first = down2_toy<Ch, true>;
    template <std::size_t Ch>
    using down2_last = down2_toy<Ch, false>;

    /// 1:1, channel c times level c of a table the caller owns, held by view.
    template <std::size_t Ch>
    class table_gain {
      public:
        using input_port  = catenae::port<float, Ch>;
        using output_port = catenae::port<float, Ch>;
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = 1;
        static constexpr bool           is_in_place       = true;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};
        static constexpr bool           rests_at_zero = true;

        struct state {};
        using workspace = catenae::no_workspace;

        /// Over the caller's levels, which outlive the stage: a temporary does not convert.
        explicit constexpr table_gain(catenae::table_view<float, Ch> levels) noexcept
            : m_levels(levels) {}

        std::size_t run(state&, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                out[i] = in[i] * m_levels[i % Ch];
            }
            return in.size() / Ch;
        }

        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }

      private:
        catenae::table_view<float, Ch> m_levels;
    };

} // namespace catenae_fixtures

namespace catenae {
    template <std::size_t Ch, std::size_t Hop>
    struct is_stage<catenae_fixtures::block_stage<Ch, Hop>> : std::true_type {};
    template <std::size_t Ch>
    struct is_stage<catenae_fixtures::up2_toy<Ch>> : std::true_type {};
    template <std::size_t Ch, bool WritesOnFirst>
    struct is_stage<catenae_fixtures::down2_toy<Ch, WritesOnFirst>> : std::true_type {};
    template <std::size_t Ch>
    struct is_stage<catenae_fixtures::table_gain<Ch>> : std::true_type {};
    template <std::size_t Ch>
    struct holds_view<catenae_fixtures::table_gain<Ch>> : std::true_type {};
} // namespace catenae

#endif // CATENAE_TESTS_FIXTURES_CATENAE_FIXTURES_STAGES_H
