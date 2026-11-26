/// @file dither.h
/// @brief `dither`: triangular (TPDF) noise of one LSB at a target word length, added before
/// the stage that quantizes to it, so the quantization's error is independent of the signal.
///
/// | Contract   |                                                                                |
/// |------------|--------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                       |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous)                               |
/// | Configured | `Bits`, the word length the next quantization rounds to, is the type; a seed  |
/// |            | (`default_seed` unless given) is the configuration                             |
/// | Noise      | per sample the difference of two independent uniform draws of [0, 1) LSB at   |
/// |            | `Bits`, an LSB being 2^-(Bits - 1) of full scale (2^(F - Bits + 1) raw units in a |
/// |            | fixed format with F fraction bits): triangular over (-1, 1) LSB, mean 0,       |
/// |            | variance 1/6 LSB^2                                                             |
/// | Adds only  | the rounding is the next stage's (`convert` or `pack`): one rounding point     |
/// | Source     | xorshift32 (13, 17, 5; Marsaglia 2003), one generator per channel; channel c   |
/// |            | starts at seed + c x 0x9E3779B9 (0x9E3779B9 where that is 0)                    |
/// | State      | the generators; at rest, seeded (`reset`, not zero: xorshift cannot rest at 0) |
/// | Workspace  | none                                                                           |
/// | Latency    | exactly 0                                                                      |
/// | Tail       | 0 frames                                                                       |
/// | Formats    | float and double (`Bits` from 2 to 24), with the noise exact to 2^-24 LSB; q31  |
/// |            | and Q4.27 (`Bits` from 2 to F), exact, the sum saturated at the format's limits |
/// | Limits     | the noise repeats after 2^32 - 1 samples (24.9 hours at 48 kHz); xorshift32 is |
/// |            | not a cryptographic source and fails some statistical batteries (Marsaglia), |
/// |            | which TPDF's two moments do not need                                           |
/// | Method     | TPDF dither (Lipshitz, Wannamaker and Vanderkooy, "Quantization and Dither: A  |
/// |            | Theoretical Survey", JAES 40(5), 1992); xorshift (Marsaglia, "Xorshift RNGs",  |
/// |            | J. Stat. Softw. 8(14), 2003)                                                   |
/// | Rationale  | D12, D29, D47, PLAN.md 12 (written new)                                        |
///
/// @req{CAT-DITHER-001} The noise is triangular over (-1, 1) LSB at `Bits`: every value within
/// it, its mean 0 and its variance 1/6 LSB^2 over 2^20 samples, and its histogram the
/// triangle's within the statistical tolerance the test states.
/// @req{CAT-DITHER-002} Quantized after it, the output's mean follows the input below one LSB
/// (0.3 LSB in gives 0.3 LSB out on average, where without dither it gives 0), and the total
/// error's variance is 1/4 LSB^2 whatever the input.
/// @req{CAT-DITHER-003} The noise is a function of the seed alone: one seed gives one sequence
/// after every reset, in any chunking; channels run their own generators, uncorrelated.
/// @req{CAT-DITHER-004} In a fixed format the noise is whole raw units within
/// (-2^(F - Bits + 1), 2^(F - Bits + 1)) and the sum saturates at the format's limits.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DITHER_H
#define CATENAE_DITHER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/detail/shift.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/sample_traits.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// The formats dither's battery pins (D29).
            template <typename S>
            inline constexpr bool k_dithers = std::is_same<S, float>::value || std::is_same<S, double>::value
                                              || std::is_same<S, q31>::value || std::is_same<S, q4_27>::value;
        } // namespace detail

        /// @brief TPDF noise of one LSB at `Bits` bits added to every sample of a `port<S, Ch>`.
        template <typename S, std::size_t Ch, int Bits>
        class basic_dither {
            using traits     = format_traits<S>;
            using value_type = storage_t<S>;
            static_assert(Bits >= 2 && (traits::is_fixed_point ? Bits <= traits::fraction_bits : Bits <= 24),
                          "catenae: dither's target is 2 to 24 bits in float and double, 2 to F in a fixed format");

          public:
            using input_port  = port<S, Ch>; ///< what it reads
            using output_port = port<S, Ch>; ///< what it writes

            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};

            /// The seed a dither has unless it is given one.
            static constexpr std::uint32_t default_seed = 0x2545F491u;

            /// Each channel's generator.
            struct state {
                std::array<std::uint32_t, Ch> generator; ///< xorshift32's word, never 0 once reset
            };
            using workspace = no_workspace; ///< nothing

            /// A dither whose noise starts from `seed`.
            explicit constexpr basic_dither(std::uint32_t seed = default_seed) noexcept
                : m_seed(seed) {}

            /// The configured seed.
            constexpr std::uint32_t seed() const noexcept { return m_seed; }

            /// Each channel's generator at its start.
            void reset(state& st) const noexcept {
                for (std::size_t c = 0; c < Ch; ++c) {
                    const std::uint32_t start = m_seed + k_spread * static_cast<std::uint32_t>(c);
                    st.generator[c]           = start == 0 ? k_spread : start;
                }
            }

            /// Every sample plus its channel's next noise value.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                const std::size_t frames = in.size() / Ch;
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[frame * Ch + c] = add_noise(in[frame * Ch + c], st.generator[c]);
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
            /// None: its noise is not a response to its input.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            /// The spacing of the channels' starting words: 2^32 over the golden ratio.
            static constexpr std::uint32_t k_spread = 0x9E3779B9u;

            /// xorshift32's next word (Marsaglia 2003, the triple 13, 17, 5).
            static std::uint32_t next(std::uint32_t& x) noexcept {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                return x;
            }

            /// `x` plus one TPDF value: the difference of two uniform draws.
            static value_type add_noise(value_type x, std::uint32_t& generator) noexcept {
                if constexpr (traits::is_fixed_point) {
                    // Two uniforms of an LSB in raw units, 2^width of them, their difference.
                    constexpr int      width = traits::fraction_bits - Bits + 1;
                    const std::int64_t a     = next(generator) >> (32 - width);
                    const std::int64_t b     = next(generator) >> (32 - width);
                    const std::int64_t sum   = std::int64_t{x} + (a - b);
                    return detail::clamp_sat<value_type>(sum);
                }
                else {
                    // Two uniforms of 2^24 steps of an LSB; an LSB is 2^-(Bits - 1), so a step is
                    // 2^-(23 + Bits). Every value is exact: |a - b| < 2^24.
                    constexpr value_type step =
                        value_type{1} / static_cast<value_type>(detail::shift_left(std::int64_t{1}, 23 + Bits));
                    const auto a = static_cast<std::int32_t>(next(generator) >> 8);
                    const auto b = static_cast<std::int32_t>(next(generator) >> 8);
                    return x + static_cast<value_type>(a - b) * step;
                }
            }

            std::uint32_t m_seed;
        };

        template <std::size_t Ch, int Bits>
        using dither = basic_dither<float, Ch, Bits>; ///< in float (D24)
        template <std::size_t Ch, int Bits>
        using dither_f64 = basic_dither<double, Ch, Bits>; ///< in double
        template <std::size_t Ch, int Bits>
        using dither_q31 = basic_dither<q31, Ch, Bits>; ///< in Q31
        template <std::size_t Ch, int Bits>
        using dither_q4_27 = basic_dither<q4_27, Ch, Bits>; ///< in Q4.27

        template <typename S, std::size_t Ch, int Bits>
        struct is_stage<basic_dither<S, Ch, Bits>> : std::bool_constant<detail::k_dithers<S>> {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DITHER_H
