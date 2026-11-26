/// @file fir_kernel.h
/// @brief The direct-form FIR, a frame and a block at a time: the kernel under `fir` (PLAN.md
/// 7.1: a header template, sized by its tap and channel counts).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Response   | y[n] = the sum over k from 0 to Taps - 1 of h[k] x[n - k]                    |
/// | History    | per channel a line of 2 Taps values: each input is written at w and at      |
/// |            | w + Taps, so the Taps most recent are contiguous from w + 1, oldest first; |
/// |            | one write index w for every channel; all zero is at rest                     |
/// | Floating   | four partial sums in S, s_m over the k = m mod 4 in ascending order, then   |
/// |            | (s0 + s1) + (s2 + s3): D36's order, 11 dB quieter than one ascending sum in |
/// |            | float at 773 taps (PLAN.md 12)                                               |
/// | Fixed      | `sample_traits`' mac over k ascending, its one rounding and saturation in   |
/// |            | `finalize`: exact in Q15, each product pre-shifted to Q45 in Q31             |
/// | Block      | channel after channel, each over the block, the frame's expressions: the    |
/// |            | same bits for any split of a block                                           |
/// | Buffers    | `in` and `out` are one buffer or do not overlap; neither overlaps the lines |
/// | Method     | the fixed-point dot is DspTap's `dot_row` (`fir_kernels.h`) at b26a7db,     |
/// |            | NOTICE.md, without its SMLALD and Helium paths (C6); the four sums are new  |
///
/// @req{CAT-FIR-KERNEL-001} `fir_dot` is the sum the card states, in the card's order: in
/// float and double the four partial sums combined as (s0 + s1) + (s2 + s3), bit for bit at
/// contraction off; in Q15 and Q31 `sample_traits`' mac chain and its one rounding, bit for bit.
/// @req{CAT-FIR-KERNEL-002} Per channel, `fir_block` is the frame kernel frame after frame:
/// the output and the history it leaves are bit for bit the same for any split of the frames
/// into calls, in place or not, at every tap count from 1.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DETAIL_FIR_KERNEL_H
#define CATENAE_DETAIL_FIR_KERNEL_H

#include <cstddef>
#include <cstdint>

#include "catenae/detail/abi.h"
#include "catenae/sample_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// The output for the window `x` of Taps samples, x[Taps - 1] the newest, through the
            /// taps `h`: four partial sums in a floating format, the traits' mac chain in a fixed one.
            template <typename S, std::size_t Taps>
            inline S fir_dot(const typename sample_traits<S>::coeff* h, const S* x) noexcept {
                using traits = sample_traits<S>;
                static_assert(Taps >= 1, "catenae: an FIR has at least one tap");
                if constexpr (traits::is_fixed_point) {
                    typename traits::accum acc{};
                    for (std::size_t k = 0; k < Taps; ++k) {
                        acc = traits::mac(acc, x[Taps - 1 - k], h[k]);
                    }
                    return traits::finalize(acc);
                }
                else {
                    S           s0{};
                    S           s1{};
                    S           s2{};
                    S           s3{};
                    std::size_t k = 0;
                    for (; k + 4 <= Taps; k += 4) {
                        s0 += h[k] * x[Taps - 1 - k];
                        s1 += h[k + 1] * x[Taps - 2 - k];
                        s2 += h[k + 2] * x[Taps - 3 - k];
                        s3 += h[k + 3] * x[Taps - 4 - k];
                    }
                    if constexpr (Taps % 4 >= 1) {
                        s0 += h[k] * x[Taps - 1 - k];
                    }
                    if constexpr (Taps % 4 >= 2) {
                        s1 += h[k + 1] * x[Taps - 2 - k];
                    }
                    if constexpr (Taps % 4 >= 3) {
                        s2 += h[k + 2] * x[Taps - 3 - k];
                    }
                    return (s0 + s1) + (s2 + s3);
                }
            }

            /// n frames of Ch interleaved channels through the taps `h`. `lines` holds Ch lines
            /// of 2 Taps values, one after another; `write` is the slot the next frame takes.
            template <typename S, std::size_t Taps, std::size_t Ch>
            inline void fir_block(const typename sample_traits<S>::coeff* h, S* lines, std::uint32_t& write,
                                  const S* in, std::size_t n, S* out) noexcept {
                static_assert(Taps >= 1 && Ch >= 1, "catenae: an FIR has a tap and a channel");
                if (n == 0) {
                    return; // nothing to filter, and an empty buffer's pointer may be null
                }
                const std::size_t w0 = write;
                for (std::size_t c = 0; c < Ch; ++c) {
                    S*          line = lines + c * 2 * Taps;
                    std::size_t w    = w0;
                    for (std::size_t f = 0; f < n; ++f) {
                        const S x       = in[f * Ch + c];
                        line[w]         = x;
                        line[w + Taps]  = x;
                        out[f * Ch + c] = fir_dot<S, Taps>(h, line + w + 1);
                        w               = w + 1 == Taps ? 0 : w + 1;
                    }
                }
                write = static_cast<std::uint32_t>((w0 + n % Taps) % Taps);
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_FIR_KERNEL_H
