/// @file difference.h
/// @brief A stage written outside the library (guide: Writing a stage): the first difference.
// Copyright 2026 Timothy Place and the catenae contributors
//
// y[n] = x[n] - x[n - 1], per channel: the two-tap FIR with coefficients 1 and -1, whose
// antisymmetric impulse response gives it linear phase and a constant group delay of half a
// frame (Oppenheim and Schafer, Discrete-Time Signal Processing, 3rd ed., sec. 5.7.3).
#ifndef CATENAE_EXAMPLES_DIFFERENCE_H
#define CATENAE_EXAMPLES_DIFFERENCE_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace example {

    // ANCHOR: card
    /// @brief The first difference, y[n] = x[n] - x[n - 1], as a 1:1 stage.
    ///
    /// | Contract   |                                                                         |
    /// |------------|-------------------------------------------------------------------------|
    /// | Ports      | `port<float, Ch>` in and out                                            |
    /// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)     |
    /// | Configured | by nothing                                                              |
    /// | State      | the last frame; at rest all zero (`rests_at_zero`)                      |
    /// | Workspace  | none                                                                    |
    /// | Latency    | exactly 1/2 frame: linear phase, antisymmetric                          |
    /// | Tail       | 1 frame; no residue                                                     |
    /// | Formats    | `float`                                                                 |
    /// | Limits     | out peaks at twice in (gain 2 at Nyquist); a NaN or inf spoils 2 frames |
    /// | Method     | a two-tap FIR (Oppenheim and Schafer, sec. 5.7.3)                       |
    /// | Rationale  | D10, D34, D35, D49                                                      |
    // ANCHOR_END: card
    // ANCHOR: stage
    template <std::size_t Ch>
    class difference {
      public:
        // The ports, and the ratio's arrival numbers: frame by frame, never behind or ahead.
        using input_port  = catenae::port<float, Ch>;
        using output_port = catenae::port<float, Ch>;
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum = 1;
        static constexpr bool           is_in_place    = true;

        // Linear phase: every frequency is delayed alike, by half a frame.
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{1, 2};

        // What one instance remembers: the last frame. All zero is its rest.
        struct state {
            std::array<float, Ch> previous;
        };
        static constexpr bool rests_at_zero = true;
        using workspace                     = catenae::no_workspace;

        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    const float x       = in[frame * Ch + c]; // read before writing: out may be in
                    out[frame * Ch + c] = x - st.previous[c];
                    st.previous[c]      = x;
                }
            }
            return frames;
        }

        // The counts: n in, n out, from any state.
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }

        // Latency and the tail: one zero frame and the history is silent.
        catenae::stage_latency latency() const noexcept { return {0.5f, 0.0f}; }
        std::size_t            tail_frames() const noexcept { return 1; }
        std::size_t            practical_tail_frames() const noexcept { return 1; }
        float                  flush_residue() const noexcept { return 0.0f; }
    };
    // ANCHOR_END: stage

} // namespace example

// ANCHOR: opt_in
namespace catenae {
    template <std::size_t Ch>
    struct is_stage<example::difference<Ch>> : std::true_type {};
} // namespace catenae
// ANCHOR_END: opt_in

#endif // CATENAE_EXAMPLES_DIFFERENCE_H
