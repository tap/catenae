/// @file mix.h
/// @brief `mix`: a matrix from `In` channels to `Out`, each output a weighted sum of the
/// inputs, ramped when the matrix changes (PLAN.md 2, 9.6).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, In>` in, `port<S, Out>` out                                         |
/// | Ratio      | 1:1, input group 1 (frame-synchronous); not in place: every output reads every input |
/// | Configured | the matrix, `Out` rows of `In` coefficients (row o makes output o), and a ramp |
/// |            | length in frames; `set_matrix()` between calls                               |
/// | Held       | by value while the configuration fits its 512 bytes (8 x 8 in float is 256);  |
/// |            | a larger matrix by `table_view`, so the stage `holds_view` and a temporary   |
/// |            | matrix does not convert (D54)                                                |
/// | Sum        | output o is the sum over i ascending of m[o][i] x[i], in the format's own type |
/// |            | (float in float, D36)                                                        |
/// | Ramp       | as `gain`'s, per coefficient: frame k of R applies goal - step x (R - k), step |
/// |            | = (goal - start) / R, so frame R applies the goal exactly; R = 0 jumps        |
/// | State      | the matrix applied now, the one it ramps to, the steps and the frames left;  |
/// |            | at rest, the configured matrix with no ramp running (`reset`, not zero)      |
/// | Workspace  | none                                                                         |
/// | Latency    | exactly 0                                                                    |
/// | Tail       | 0 frames                                                                     |
/// | Formats    | float, double; q31 declares its contract with Q1.30 coefficients, and its    |
/// |            | ramped arithmetic is new design that arrives at C6 (D47)                     |
/// | Limits     | a non-finite coefficient poisons its output until a finite one is ramped to  |
/// | Contraction| pinned at contraction off; under the caller's flags a fused multiply-add can |
/// |            | move a sum by an ULP                                                         |
/// | Rationale  | D24, D36, D47, D54, PLAN.md 2 (two signals meet only as channels of one port) |
///
/// @req{CAT-MIX-001} From reset, output o of every frame is the sum over i ascending of
/// m[o][i] x[i], in the format's own type: a stereo pair to mono at one half each, a mono
/// input to two outputs, and a 3 x 2 matrix with every coefficient distinct.
/// @req{CAT-MIX-002} A matrix set between calls is reached over the ramp's R frames, every
/// coefficient as `gain`'s level (CAT-GAIN-002): frame R applies the new matrix exactly, R = 0
/// jumps, and the output is the same for any chunking, a ramp that spans calls included.
/// @req{CAT-MIX-003} A matrix whose configuration fits 512 bytes is held by value; a larger one
/// is held by `table_view`, refused from a temporary, and the stage then holds a view.
/// @req{CAT-MIX-004} In q31 the coefficients are Q1.30 and the contract is instantiated; a run
/// does not compile until the fixed-point ramp arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_MIX_H
#define CATENAE_MIX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// A mix's coefficient: the sample's own type when it floats, Q1.30 in an int32
            /// for a fixed-point format (sample_traits' FIR datapath; arithmetic at C6).
            template <typename S>
            using mix_coefficient_t = std::conditional_t<format_traits<S>::is_fixed_point, std::int32_t, storage_t<S>>;

            /// Whether a mix of these sizes holds its matrix by value: its configuration, the
            /// matrix and the ramp's length, within the 512 bytes a stage may be (D54).
            template <typename S, std::size_t In, std::size_t Out>
            struct mix_by_value {
                struct probe {
                    std::array<mix_coefficient_t<S>, Out * In> matrix;
                    std::uint32_t                              ramp;
                };
                static constexpr bool k_value = sizeof(probe) <= max_configuration_bytes;
            };
        } // namespace detail

        /// @brief `Out` weighted sums of a `port<S, In>`'s channels, ramped when they change.
        template <typename S, std::size_t In, std::size_t Out>
        class basic_mix {
            static_assert(In >= 1 && Out >= 1, "catenae: a mix has at least one input and one output");

          public:
            using input_port       = port<S, In>;                            ///< what it reads
            using output_port      = port<S, Out>;                           ///< what it writes
            using coefficient_type = detail::mix_coefficient_t<S>;           ///< one weight
            using matrix_type      = std::array<coefficient_type, Out * In>; ///< row o, then column i
            /// True when the matrix is held by value, false when by view (CAT-MIX-003).
            static constexpr bool holds_by_value = detail::mix_by_value<S, In, Out>::k_value;
            /// What the configuration holds: the matrix, or a view of the caller's.
            using matrix_holder =
                std::conditional_t<holds_by_value, matrix_type, table_view<coefficient_type, Out * In>>;

          private:
            using value_type = storage_t<S>;

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = false;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};

            /// Where the ramp has got to.
            struct state {
                matrix_type   current;   ///< the matrix applied to the last frame
                matrix_type   goal;      ///< the matrix being ramped to
                matrix_type   step;      ///< per frame, while a ramp runs
                std::uint32_t remaining; ///< frames of ramp left
            };
            using workspace = no_workspace; ///< nothing

            /// A mix through `matrix`, which ramps over `ramp_frames` frames when it is changed.
            constexpr basic_mix(matrix_holder matrix, std::uint32_t ramp_frames) noexcept
                : m_matrix(matrix)
                , m_ramp_frames(ramp_frames) {}

            /// The configured matrix's coefficient for output `o` from input `i`.
            constexpr coefficient_type at(std::size_t o, std::size_t i) const noexcept { return m_matrix[o * In + i]; }
            /// A new matrix, ramped to from the next run(). Between calls only.
            constexpr void set_matrix(matrix_holder matrix) noexcept { m_matrix = matrix; }
            /// The ramp's length in frames.
            constexpr std::uint32_t ramp_frames() const noexcept { return m_ramp_frames; }

            /// At the configured matrix, with no ramp running.
            void reset(state& st) const noexcept {
                for (std::size_t k = 0; k < Out * In; ++k) {
                    st.current[k] = m_matrix[k];
                    st.goal[k]    = m_matrix[k];
                    st.step[k]    = coefficient_type{};
                }
                st.remaining = 0;
            }

            /// Every frame's outputs, each the weighted sum of its inputs, ramping when the
            /// matrix has changed.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point mix is ramped, new design that arrives with its profile at C6 "
                              "(D47)");
                if (!same_as_goal(st)) {
                    for (std::size_t k = 0; k < Out * In; ++k) {
                        st.goal[k] = m_matrix[k];
                    }
                    if (m_ramp_frames == 0) {
                        st.current   = st.goal;
                        st.remaining = 0;
                    }
                    else {
                        const auto frames = static_cast<coefficient_type>(m_ramp_frames);
                        for (std::size_t k = 0; k < Out * In; ++k) {
                            st.step[k] = (st.goal[k] - st.current[k]) / frames;
                        }
                        st.remaining = m_ramp_frames;
                    }
                }
                const std::size_t frames = in.size() / In;
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    if (st.remaining > 0) {
                        --st.remaining;
                        const auto left = static_cast<coefficient_type>(st.remaining);
                        for (std::size_t k = 0; k < Out * In; ++k) {
                            st.current[k] = st.goal[k] - st.step[k] * left;
                        }
                    }
                    for (std::size_t o = 0; o < Out; ++o) {
                        value_type sum{};
                        for (std::size_t i = 0; i < In; ++i) {
                            sum += st.current[o * In + i] * in[frame * In + i];
                        }
                        out[frame * Out + o] = sum;
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

          private:
            /// Whether the state's goal is the configured matrix, bit for bit: a held matrix or
            /// the caller's, read in place.
            bool same_as_goal(const state& st) const noexcept {
                return std::memcmp(st.goal.data(), m_matrix.data(), sizeof(matrix_type)) == 0;
            }

            matrix_holder m_matrix;
            std::uint32_t m_ramp_frames;
        };

        template <std::size_t In, std::size_t Out>
        using mix = basic_mix<float, In, Out>; ///< in float (D24)
        template <std::size_t In, std::size_t Out>
        using mix_f64 = basic_mix<double, In, Out>; ///< in double
        template <std::size_t In, std::size_t Out>
        using mix_q31 = basic_mix<q31, In, Out>; ///< in Q31, Q1.30 coefficients: the contract now, the arithmetic at C6

        template <typename S, std::size_t In, std::size_t Out>
        struct is_stage<basic_mix<S, In, Out>> : std::true_type {};
        /// A mix whose matrix is too large to hold by value views the caller's.
        template <typename S, std::size_t In, std::size_t Out>
        struct holds_view<basic_mix<S, In, Out>> : std::bool_constant<!basic_mix<S, In, Out>::holds_by_value> {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_MIX_H
