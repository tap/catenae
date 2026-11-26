/// @file recursive_tail.h
/// @brief How many zero frames a biquad cascade takes to decay: the tails a recursive design's
/// `make()` stores (PLAN.md 3.3: a header template on the section count, its matrices on the
/// stack, run where the stack admits it and never in the audio callback).
///
/// With zero input the cascade is s' = A s, A its 2K x 2K state-transition matrix (transposed
/// direct form II, (z1, z2) per section in processing order). `decay_frames(sections, start,
/// goal)` is the smallest n with start + ln ||A^n||_inf < goal, found by binary lifting over
/// A^(2^j), each power kept normalised with a separate log scale and recomputed from a few
/// checkpoints by squaring, so the stack holds four matrices and a few more whatever the
/// answer. Carried from DspTap's `butterworth.h` at b26a7db (`scaled_matrix`,
/// `cascade_transition`, `cascade_flush_window`; NOTICE.md), with the start and the goal made
/// arguments so one search gives both tails:
///
/// | Tail      | start                    | goal                     | then           |
/// |-----------|--------------------------|--------------------------|----------------|
/// | exact     | ln of S's largest value  | ln of S's smallest normal| + K, a margin of a frame per section |
/// | practical | 0, a state at full scale | ln 10^-6, 120 dB below   | + K            |
///
/// The stack this costs, measured by DspTap for its Butterworth (Cortex-M33,
/// arm-none-eabi-gcc 13.2 -Os): 480 bytes at one section, 1,432 at two, 5,272 at four and
/// 20,640 at eight.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DETAIL_RECURSIVE_TAIL_H
#define CATENAE_DETAIL_RECURSIVE_TAIL_H

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "catenae/detail/abi.h"
#include "catenae/detail/biquad_kernel.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// A cascade's state-transition matrix or a power of it, kept as m e^log_scale with
            /// ||m||_inf = 1. Block lower triangular in 2 x 2 blocks, one per section (a section's
            /// next state depends on its own state and the sections' before it, never a later
            /// one's), and so is every product of two such: row r is zero past column
            /// row_end(r), and the loops stop there. The terms they leave out are exact zeros
            /// added to a sum that is never a negative zero, so every element has the bits the
            /// full loops give.
            template <std::size_t D>
            struct scaled_matrix {
                std::array<double, D * D> m{};
                double                    log_scale = 0.0;

                /// One past the last column of row r that can be nonzero.
                static constexpr std::size_t row_end(std::size_t r) noexcept { return 2 * (r / 2) + 2; }

                /// m divided by its norm, the norm's log added to the scale; a zero matrix has
                /// a scale of minus infinity.
                void normalize() noexcept {
                    double n = 0.0;
                    for (std::size_t r = 0; r < D; ++r) {
                        double row = 0.0;
                        for (std::size_t c = 0; c < row_end(r); ++c) {
                            row += std::fabs(m[r * D + c]);
                        }
                        n = row > n ? row : n;
                    }
                    if (n > 0.0) {
                        for (std::size_t r = 0; r < D; ++r) {
                            for (std::size_t c = 0; c < row_end(r); ++c) {
                                m[r * D + c] /= n;
                            }
                        }
                        log_scale += std::log(n);
                    }
                    else {
                        log_scale = -std::numeric_limits<double>::infinity();
                    }
                }

                /// The product, normalised.
                friend scaled_matrix operator*(const scaled_matrix& a, const scaled_matrix& b) noexcept {
                    scaled_matrix r;
                    for (std::size_t i = 0; i < D; ++i) {
                        for (std::size_t k = 0; k < row_end(i); ++k) {
                            const double aik = a.m[i * D + k];
                            for (std::size_t j = 0; j < row_end(k); ++j) {
                                r.m[i * D + j] += aik * b.m[k * D + j];
                            }
                        }
                    }
                    r.log_scale = a.log_scale + b.log_scale;
                    r.normalize();
                    return r;
                }
            };

            /// The zero-input state-transition matrix of the cascade, normalised.
            template <std::size_t K>
            scaled_matrix<2 * K> cascade_transition(const std::array<biquad_row<double>, K>& sections) noexcept {
                constexpr std::size_t D = 2 * K;
                scaled_matrix<D>      a;
                std::array<double, D> x{}; // section 0's input: the zero input, as a row over the old state
                for (std::size_t s = 0; s < K; ++s) {
                    const biquad_row<double>& c = sections[s];
                    std::array<double, D>     y = x; // y = b0 x + z1 (old)
                    for (double& v : y) {
                        v *= c[0];
                    }
                    y[2 * s] += 1.0;
                    for (std::size_t j = 0; j < D; ++j) { // z1' = b1 x - a1 y + z2; z2' = b2 x - a2 y
                        a.m[(2 * s) * D + j]     = c[1] * x[j] - c[3] * y[j];
                        a.m[(2 * s + 1) * D + j] = c[2] * x[j] - c[4] * y[j];
                    }
                    a.m[(2 * s) * D + 2 * s + 1] += 1.0;
                    x = y; // the next section's input is this one's output
                }
                a.normalize();
                return a;
            }

            /// `p` squared `times` times.
            template <std::size_t D>
            scaled_matrix<D> squared(scaled_matrix<D> p, std::size_t times) noexcept {
                for (; times > 0; --times) {
                    p = p * p;
                }
                return p;
            }

            /// The smallest n with start + ln ||A^n||_inf < goal, plus K; the largest size_t
            /// where that does not fit half of size_t's range or the cascade does not decay.
            template <std::size_t K>
            std::size_t decay_frames(const std::array<biquad_row<double>, K>& sections, double start,
                                     double goal) noexcept {
                using mat                         = scaled_matrix<2 * K>;
                constexpr std::size_t k_levels    = std::numeric_limits<std::size_t>::digits - 1;
                constexpr std::size_t k_saturated = std::numeric_limits<std::size_t>::max();
                const mat             a           = cascade_transition<K>(sections);
                const auto done = [start, goal](const mat& p) noexcept { return start + p.log_scale < goal; };

                // The powers A^(2^j), j < levels, up to the first that alone is done.
                std::size_t levels = 1;
                for (mat top = a; !done(top); ++levels) {
                    if (levels == k_levels) {
                        return k_saturated;
                    }
                    top = top * top;
                }
                // A^(2^(i stride)), i < k_kept: the checkpoints the descent squares up from.
                constexpr std::size_t   k_kept = 4;
                const std::size_t       stride = (levels + k_kept - 1) / k_kept;
                std::array<mat, k_kept> kept{};
                kept[0] = a;
                for (std::size_t i = 1; i < k_kept && i * stride < levels; ++i) {
                    kept[i] = kept[i - 1];
                    for (std::size_t q = 0; q < stride; ++q) {
                        kept[i] = kept[i] * kept[i];
                    }
                }
                const auto power = [&kept, stride](std::size_t j) noexcept { // A^(2^j), j < levels
                    return squared(kept[j / stride], j % stride);
                };
                // Binary lifting: the largest n from those powers not yet done. The top power is
                // done alone, so the descent starts below it.
                mat         acc;
                bool        have = false;
                std::size_t n    = 0;
                for (std::size_t j = levels - 1; j-- > 0;) {
                    const mat pj   = power(j);
                    const mat cand = have ? acc * pj : pj;
                    if (!done(cand)) {
                        acc  = cand;
                        have = true;
                        n += std::size_t{1} << j;
                    }
                }
                mat p = have ? acc * a : a;
                ++n;
                while (!done(p)) {
                    p = p * a;
                    ++n;
                }
                return n + K;
            }

            /// The exact tail of the cascade the kernel runs in S (its coefficients rounded to
            /// S, given here in double): zero frames after which its state is below S's smallest
            /// normal value from any state S can hold.
            template <typename S, std::size_t K>
            std::size_t exact_tail_frames(const std::array<biquad_row<double>, K>& sections) noexcept {
                return decay_frames<K>(sections, std::log(static_cast<double>(std::numeric_limits<S>::max())),
                                       std::log(static_cast<double>(std::numeric_limits<S>::min())));
            }

            /// The practical tail: zero frames after which a state at full scale has decayed by
            /// 120 dB.
            template <std::size_t K>
            std::size_t practical_tail_frames(const std::array<biquad_row<double>, K>& sections) noexcept {
                return decay_frames<K>(sections, 0.0, std::log(1e-6));
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_RECURSIVE_TAIL_H
