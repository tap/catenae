/// @file delay.h
/// @brief `delay`: a fixed delay of exactly `Frames` frames, as a 1:1 stage (PLAN.md 4, 9.7).
///
/// | Contract   |                                                                       |
/// |------------|-----------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                              |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)   |
/// | Configured | by `Frames`, a template argument: there is nothing else to configure |
/// | State      | `Frames x Ch` values and a write index; at rest all zero (`rests_at_zero`) |
/// | Workspace  | none                                                                  |
/// | Latency    | exactly `Frames` frames, a constant of the type                      |
/// | Tail       | `Frames` frames, exactly; no residue                                 |
/// | Formats    | float, double, q15, q31, Q4.27: a copy, bit for bit                  |
/// | Limits     | `Frames` from 1 to 2^32 - 1                                          |
/// | Method     | a circular line, one frame read and written per input frame          |
/// | Rationale  | D22, D43, D47                                                         |
///
/// @req{CAT-DELAY-001} Output frame n is input frame n - Frames, bit for bit in every format,
/// and silence (all zero) for n < Frames after reset; in place it writes the same.
/// @req{CAT-DELAY-002} `exact_latency` is Frames/1, `latency()` is {Frames, 0}, and after
/// Frames zero frames the output is silence: `tail_frames()` and `practical_tail_frames()`
/// are Frames, `flush_residue()` 0.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DELAY_H
#define CATENAE_DELAY_H

#include <array>
#include <cstddef>
#include <cstdint>
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

        /// @brief A `port<S, Ch>` delayed by exactly `Frames` frames.
        template <typename S, std::size_t Ch, std::size_t Frames>
        class basic_delay {
            static_assert(Frames >= 1 && Frames <= 0xFFFFFFFFu, "catenae: a delay is from 1 to 2^32 - 1 frames");

          public:
            using input_port  = port<S, Ch>; ///< what it reads
            using output_port = port<S, Ch>; ///< what it writes

          private:
            using value_type = storage_t<S>;

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            /// Exactly Frames frames.
            static constexpr ratio exact_latency{static_cast<std::uint32_t>(Frames), 1};
            static constexpr bool  rests_at_zero = true; ///< an empty line is silence

            /// The line and where the next frame is written.
            struct state {
                std::array<value_type, Frames * Ch> line;  ///< the last Frames frames, circular
                std::uint32_t                       write; ///< the frame the next input replaces
            };
            using workspace = no_workspace; ///< nothing

            /// Each frame out is the one Frames before it in.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                const std::size_t frames = in.size() / Ch;
                std::size_t       at     = st.write;
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        const value_type arriving = in[frame * Ch + c]; // read first: out may be in
                        out[frame * Ch + c]       = st.line[at * Ch + c];
                        st.line[at * Ch + c]      = arriving;
                    }
                    at = at + 1 == Frames ? 0 : at + 1;
                }
                st.write = static_cast<std::uint32_t>(at);
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// Frames, exactly.
            stage_latency latency() const noexcept { return {static_cast<float>(Frames), 0.0f}; }
            /// Frames: the line empties.
            std::size_t tail_frames() const noexcept { return Frames; }
            /// Frames.
            std::size_t practical_tail_frames() const noexcept { return Frames; }
            /// None: a copy leaves nothing behind.
            float flush_residue() const noexcept { return 0.0f; }
        };

        template <std::size_t Ch, std::size_t Frames>
        using delay = basic_delay<float, Ch, Frames>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Frames>
        using delay_f64 = basic_delay<double, Ch, Frames>; ///< in double
        template <std::size_t Ch, std::size_t Frames>
        using delay_q15 = basic_delay<q15, Ch, Frames>; ///< in Q15
        template <std::size_t Ch, std::size_t Frames>
        using delay_q31 = basic_delay<q31, Ch, Frames>; ///< in Q31
        template <std::size_t Ch, std::size_t Frames>
        using delay_q4_27 = basic_delay<q4_27, Ch, Frames>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::size_t Frames>
        struct is_stage<basic_delay<S, Ch, Frames>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DELAY_H
