/// @file interleave.h
/// @brief `deinterleave` and `interleave`: the 1:1 stages between the two layouts (PLAN.md 5.1).
///
/// | Contract   |                                                                             |
/// |------------|-----------------------------------------------------------------------------|
/// | Ports      | deinterleave: `port<S, Ch>` in, `port<S, Ch, layout::planar>` out; interleave: the reverse |
/// | Ratio      | 1:1, input group 1 (frame-synchronous); not in place, the two ports differ |
/// | Configured | by nothing: the template arguments are all there is                         |
/// | State      | none (`rests_at_zero`)                                                      |
/// | Workspace  | none                                                                        |
/// | Latency    | exactly 0                                                                   |
/// | Tail       | 0 frames                                                                    |
/// | Formats    | float, double, q15, q31, Q4.27: a copy, bit for bit                        |
/// | Method     | interleaved value f x Ch + c is planar channel c's value f                 |
/// | Rationale  | D5, D7, D42, D47                                                            |
///
/// @req{CAT-LAYOUT-001} deinterleave writes interleaved value f x Ch + c as channel c's value
/// f, interleave the reverse, so one after the other gives back their input bit for bit.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_INTERLEAVE_H
#define CATENAE_INTERLEAVE_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief Interleaved frames to Ch planes.
        template <typename S, std::size_t Ch>
        class basic_deinterleave {
          public:
            using input_port  = port<S, Ch>;                    ///< interleaved
            using output_port = port<S, Ch, layout::planar>;    ///< planar
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = false;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< it remembers nothing

            /// Nothing.
            struct state {};
            using workspace = no_workspace; ///< nothing

            /// Value f x Ch + c to channel c's value f.
            std::size_t run(state&, workspace&, typename input_port::input_view in,
                            typename output_port::output_view out) const noexcept {
                const std::size_t frames = in.size() / Ch;
                for (std::size_t c = 0; c < Ch; ++c) {
                    for (std::size_t frame = 0; frame < frames; ++frame) {
                        out[c][frame] = in[frame * Ch + c];
                    }
                }
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }
        };

        /// @brief Ch planes to interleaved frames.
        template <typename S, std::size_t Ch>
        class basic_interleave {
          public:
            using input_port  = port<S, Ch, layout::planar>;    ///< planar
            using output_port = port<S, Ch>;                    ///< interleaved
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = false;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< it remembers nothing

            /// Nothing.
            struct state {};
            using workspace = no_workspace; ///< nothing

            /// Channel c's value f to value f x Ch + c.
            std::size_t run(state&, workspace&, typename input_port::input_view in,
                            typename output_port::output_view out) const noexcept {
                const std::size_t frames = in[0].size();
                for (std::size_t c = 0; c < Ch; ++c) {
                    for (std::size_t frame = 0; frame < frames; ++frame) {
                        out[frame * Ch + c] = in[c][frame];
                    }
                }
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }
        };

        template <std::size_t Ch>
        using deinterleave = basic_deinterleave<float, Ch>; ///< in float (D24)
        template <std::size_t Ch>
        using deinterleave_f64 = basic_deinterleave<double, Ch>; ///< in double
        template <std::size_t Ch>
        using deinterleave_q15 = basic_deinterleave<q15, Ch>; ///< in Q15
        template <std::size_t Ch>
        using deinterleave_q31 = basic_deinterleave<q31, Ch>; ///< in Q31
        template <std::size_t Ch>
        using deinterleave_q4_27 = basic_deinterleave<q4_27, Ch>; ///< in Q4.27
        template <std::size_t Ch>
        using interleave = basic_interleave<float, Ch>; ///< in float (D24)
        template <std::size_t Ch>
        using interleave_f64 = basic_interleave<double, Ch>; ///< in double
        template <std::size_t Ch>
        using interleave_q15 = basic_interleave<q15, Ch>; ///< in Q15
        template <std::size_t Ch>
        using interleave_q31 = basic_interleave<q31, Ch>; ///< in Q31
        template <std::size_t Ch>
        using interleave_q4_27 = basic_interleave<q4_27, Ch>; ///< in Q4.27

        template <typename S, std::size_t Ch>
        struct is_stage<basic_deinterleave<S, Ch>> : std::true_type {};
        template <typename S, std::size_t Ch>
        struct is_stage<basic_interleave<S, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_INTERLEAVE_H
