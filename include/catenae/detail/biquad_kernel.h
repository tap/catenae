/// @file biquad_kernel.h
/// @brief The floating biquad cascade, one frame at a time and over a block: the kernel under
/// `biquad` (PLAN.md 7.1: a header template, sized by its section and channel counts).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Structure  | K sections of (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), in order, |
/// |            | transposed direct form II: y = b0 v + z1; z1 = b1 v - a1 y + z2;             |
/// |            | z2 = b2 v - a2 y                                                             |
/// | State      | per channel z1[K] then z2[K], 2 K values; all zero is at rest               |
/// | Formats    | float and double: states, coefficients and products in the sample's type   |
/// | Block      | per channel, the per-frame expressions in the same order on the same state: |
/// |            | with contraction off the two kernels give the same bits for any split of a |
/// |            | block, and may be mixed on one state                                         |
/// | Grouping   | float through one or two sections runs channels two at a time, every other  |
/// |            | case one at a time, the coefficients and states in locals for the block     |
/// | Buffers    | `in` and `out` are one buffer or do not overlap; neither overlaps the state |
/// | Method     | DspTap's `iir_kernels.h` (`biquad_cascade`, `biquad_cascade_block`) at      |
/// |            | b26a7db, NOTICE.md: its expressions with the denominator's sign written in  |
/// |            | the subtraction, which IEEE arithmetic makes the same bits                  |
///
/// @req{CAT-BIQUAD-KERNEL-001} Per channel, `biquad_cascade_block` is
/// `biquad_cascade_frame` frame after frame: the output and the state it leaves are bit for
/// bit the same for any split of the frames into calls, any channel count and either format,
/// at contraction off.
/// @req{CAT-BIQUAD-KERNEL-002} The cascade is the direct evaluation of its sections' transfer
/// functions: each section's impulse response is its difference equation's, and a cascade's
/// output is its sections' applied one after another, within the format's rounding.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DETAIL_BIQUAD_KERNEL_H
#define CATENAE_DETAIL_BIQUAD_KERNEL_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/compiler.h"
#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// One section's coefficients, b0, b1, b2, a1, a2, of
            /// (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
            template <typename C>
            using biquad_row = std::array<C, 5>;

            /// The channels the block kernel runs together: two for float through one or two
            /// sections, whose states and coefficients a core's floating registers hold; one
            /// otherwise (DspTap's measurement, its bench/README.md "The block kernel"). The
            /// grouping changes no arithmetic.
            template <typename S, std::size_t K>
            inline constexpr std::size_t k_biquad_group = (std::is_same<S, float>::value && K <= 2) ? 2 : 1;

            /// One frame of one channel through the cascade: `z` holds its 2 K state values.
            template <typename S, std::size_t K>
            inline S biquad_cascade_frame(const std::array<biquad_row<S>, K>& sections, S* z, S x) noexcept {
                S v = x;
                for (std::size_t i = 0; i < K; ++i) {
                    const biquad_row<S>& c = sections[i];
                    const S              y = c[0] * v + z[i];
                    z[i]                   = c[1] * v - c[3] * y + z[K + i];
                    z[K + i]               = c[2] * v - c[4] * y;
                    v                      = y;
                }
                return v;
            }

            /// G channels from channel c0, n frames of Ch, their states and the coefficients in
            /// locals for the block. Out of line on purpose: inlined into a caller's loop it took
            /// its shape from the caller (DspTap measured GCC vectorising across sections on the
            /// Cortex-M55 there, at 1.5 times the instructions); out of line it costs a call and
            /// a copy of the coefficients per group.
            template <typename S, std::size_t K, std::size_t Ch, std::size_t G>
            CATENAE_NOINLINE void biquad_cascade_group(const std::array<biquad_row<S>, K>& sections, S* state,
                                                       std::size_t c0, const S* in, std::size_t n, S* out) noexcept {
                S cf[K][5];
                for (std::size_t s = 0; s < K; ++s) {
                    for (std::size_t j = 0; j < 5; ++j) {
                        cf[s][j] = sections[s][j];
                    }
                }
                S z[G][2 * K];
                for (std::size_t j = 0; j < G; ++j) {
                    for (std::size_t i = 0; i < 2 * K; ++i) {
                        z[j][i] = state[(c0 + j) * 2 * K + i];
                    }
                }
                const S* x = in + c0;
                S*       o = out + c0;
                for (std::size_t f = 0; f < n; ++f, x += Ch, o += Ch) {
                    for (std::size_t j = 0; j < G; ++j) {
                        S v = x[j];
                        for (std::size_t s = 0; s < K; ++s) {
                            const S y   = cf[s][0] * v + z[j][s];
                            z[j][s]     = cf[s][1] * v - cf[s][3] * y + z[j][K + s];
                            z[j][K + s] = cf[s][2] * v - cf[s][4] * y;
                            v           = y;
                        }
                        o[j] = v;
                    }
                }
                for (std::size_t j = 0; j < G; ++j) {
                    for (std::size_t i = 0; i < 2 * K; ++i) {
                        state[(c0 + j) * 2 * K + i] = z[j][i];
                    }
                }
            }

            /// n frames of Ch interleaved channels through the cascade. `state` holds Ch
            /// channels' 2 K values each, one after another. A single frame through four or more
            /// sections takes the per-frame kernel, which the block's copies would cost more than
            /// (DspTap measured 1.5 times on Hexagon at K = 4 before this rule).
            template <typename S, std::size_t K, std::size_t Ch>
            inline void biquad_cascade_block(const std::array<biquad_row<S>, K>& sections, S* state, const S* in,
                                             std::size_t n, S* out) noexcept {
                static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value,
                              "catenae: the biquad kernel is float or double");
                static_assert(K >= 1 && Ch >= 1, "catenae: a cascade has a section and a channel");
                if constexpr (K >= 4) {
                    if (n == 1) {
                        for (std::size_t c = 0; c < Ch; ++c) {
                            out[c] = biquad_cascade_frame<S, K>(sections, state + c * 2 * K, in[c]);
                        }
                        return;
                    }
                }
                constexpr std::size_t k_group = k_biquad_group<S, K>;
                std::size_t           c0      = 0;
                if constexpr (Ch >= k_group) {
                    for (; c0 + k_group <= Ch; c0 += k_group) {
                        biquad_cascade_group<S, K, Ch, k_group>(sections, state, c0, in, n, out);
                    }
                }
                if constexpr (Ch % k_group != 0) {
                    biquad_cascade_group<S, K, Ch, Ch % k_group>(sections, state, c0, in, n, out);
                }
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_BIQUAD_KERNEL_H
