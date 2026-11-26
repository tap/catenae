/// @file fft_trig.h
/// @brief The floating engine's trigonometry: cos and sin of 2 pi k / 2^L in integer arithmetic.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Ported from DspTap's srdif_trig (include/tap/dsp/fft/srdif.h, NOTICE.md). A private header of
// the compiled core. Every function is constexpr in C++17, so a table can be a constant; the
// same functions build a table at run time into a caller's storage, with the same bits.
//
// Why not libm. A table built from std::cos and std::sin is only as portable as the last bit
// of each C library, so every output bit of a transform would be pinned per library. Here the
// table is a function of (k, L) alone: unsigned 64-bit integer arithmetic, then one exact
// conversion of an at most 53-bit integer and an exact power-of-two scaling (to_sample). It
// also keeps libm out of an image that uses the engine.
//
// Method (first_octant). theta = 2 pi k / 2^L, 0 < k <= 2^L / 8, is formed exactly as the
// 128-bit product P = k floor(2 pi 2^61) (a relative error of 2^-63 from the truncated
// constant), read as Q0.64 (absolute) for y = theta^2 and normalized (relative) for the
// products that keep precision at small angles. Horner's rule in Q0.64 gives
// tc = (1 - cos) / y and ts = (1 - sin / theta) / y from the Maclaurin coefficients 1/(2j)!
// (j <= 10) and 1/(2j + 1)! (j <= 9), each floor(2^64 / n!); every intermediate is positive
// and below 1. Then cos = 1 - y tc and 1 - sin = 1 - theta (1 - y ts) in Q0.64, and
// sin = theta (1 - y ts) and 1 - cos = theta^2 tc with the normalized theta.
// D. E. Knuth, The Art of Computer Programming vol. 2, 3rd ed., sec. 4.3.1 (the
// multiple-precision products) and sec. 4.6.4 (Horner's rule).
//
// Accuracy, as DspTap measured it against __float128 over every k of every 2^L, L = 3 to 30:
// the 64-bit mantissa is within 13.44 units of its last place; rounded to float every value
// is the correctly rounded one; a double is within 0.5052 ulp of the exact value.
#ifndef CATENAE_SRC_FFT_TRIG_H
#define CATENAE_SRC_FFT_TRIG_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {
            namespace fft_trig {

                /// A non-negative value m x 2^e, m normalized (bit 63 set) or zero.
                struct wide_real {
                    std::uint64_t m;
                    int           e;
                };

                /// floor(a b / 2^64), exact, from four 32 x 32 -> 64 products.
                constexpr std::uint64_t mul_hi(std::uint64_t a, std::uint64_t b) noexcept {
                    const std::uint64_t a_lo = a & 0xffffffffu;
                    const std::uint64_t a_hi = a >> 32u;
                    const std::uint64_t b_lo = b & 0xffffffffu;
                    const std::uint64_t b_hi = b >> 32u;
                    const std::uint64_t ll   = a_lo * b_lo;
                    const std::uint64_t lh   = a_lo * b_hi;
                    const std::uint64_t hl   = a_hi * b_lo;
                    const std::uint64_t hh   = a_hi * b_hi;
                    const std::uint64_t mid  = (ll >> 32u) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
                    return hh + (lh >> 32u) + (hl >> 32u) + (mid >> 32u);
                }

                /// The number of leading zero bits of m != 0, by halving the window.
                constexpr int leading_zeros(std::uint64_t m) noexcept {
                    int           z     = 0;
                    std::uint64_t probe = m;
                    for (unsigned width = 32u; width > 0u; width /= 2u) {
                        if ((probe >> (64u - width)) == 0u) {
                            z += static_cast<int>(width);
                            probe <<= width;
                        }
                    }
                    return z;
                }

                /// m x 2^e normalized so that bit 63 of m is set (or zero).
                constexpr wide_real normalized(std::uint64_t m, int e) noexcept {
                    if (m == 0u) {
                        return {0u, 0};
                    }
                    const int z = leading_zeros(m);
                    return {m << static_cast<unsigned>(z), e - z};
                }

                /// floor(2^64 / d) for d >= 2.
                constexpr std::uint64_t reciprocal_q64(std::uint64_t d) noexcept {
                    const std::uint64_t all = ~std::uint64_t{0};
                    std::uint64_t       q   = all / d;
                    if (all % d == d - 1u) {
                        ++q; // d divides 2^64
                    }
                    return q;
                }

                /// n!, for n <= 20.
                constexpr std::uint64_t factorial(unsigned n) noexcept {
                    std::uint64_t f = 1u;
                    for (unsigned i = 2u; i <= n; ++i) {
                        f *= i;
                    }
                    return f;
                }

                /// The Maclaurin coefficients in Q0.64: cos_c[j] = floor(2^64 / (2j)!) for j = 1 to 10,
                /// sin_c[j] = floor(2^64 / (2j + 1)!) for j = 1 to 9 (entry 0 unused in both).
                struct maclaurin {
                    std::uint64_t cos_c[11];
                    std::uint64_t sin_c[10];
                };

                constexpr maclaurin make_maclaurin() noexcept {
                    maclaurin c{};
                    for (unsigned j = 1u; j <= 10u; ++j) {
                        c.cos_c[j] = reciprocal_q64(factorial(2u * j));
                    }
                    for (unsigned j = 1u; j <= 9u; ++j) {
                        c.sin_c[j] = reciprocal_q64(factorial(2u * j + 1u));
                    }
                    return c;
                }

                /// 2 pi in Q3.61, truncated: floor(2 pi 2^61).
                constexpr std::uint64_t k_two_pi_q61 = 0xc90fdaa22168c234u;

                /// cos, sin, 1 - cos and 1 - sin at one first-octant angle, each with relative precision.
                struct octant_values {
                    wide_real cos;
                    wide_real sin;
                    wide_real one_minus_cos;
                    wide_real one_minus_sin;
                };

                /// The four values at theta = 2 pi k / 2^log2n.
                /// @pre 0 <= k <= 2^log2n / 8 and 3 <= log2n <= 33.
                constexpr octant_values first_octant(std::uint64_t k, int log2n) noexcept {
                    if (k == 0u) {
                        return {{std::uint64_t{1} << 63u, -63}, {0u, 0}, {0u, 0}, {std::uint64_t{1} << 63u, -63}};
                    }
                    constexpr maclaurin c = make_maclaurin();
                    // P = k (2 pi 2^61), exact in 128 bits; theta = P 2^-(61 + log2n).
                    const std::uint64_t p_hi = mul_hi(k, k_two_pi_q61);
                    const std::uint64_t p_lo = k * k_two_pi_q61;
                    // theta in Q0.64 (absolute): P >> (log2n - 3); k <= 2^(log2n - 3) keeps it below 2^64.
                    const auto          s       = static_cast<unsigned>(log2n - 3);
                    const std::uint64_t theta_q = s == 0u ? p_lo : (p_lo >> s) | (p_hi << (64u - s));
                    // theta with relative precision: P's top 64 bits.
                    wide_real theta{};
                    if (p_hi != 0u) {
                        const auto z = static_cast<unsigned>(leading_zeros(p_hi));
                        theta        = {z == 0u ? p_hi : (p_hi << z) | (p_lo >> (64u - z)),
                                 64 - static_cast<int>(z) - 61 - log2n};
                    }
                    else {
                        theta = normalized(p_lo, -61 - log2n);
                    }
                    // y = theta^2 in Q0.64 (theta <= pi/4, so y < 0.62).
                    const std::uint64_t y  = mul_hi(theta_q, theta_q);
                    std::uint64_t       tc = c.cos_c[10];
                    for (std::size_t j = 9u; j >= 1u; --j) {
                        tc = c.cos_c[j] - mul_hi(y, tc);
                    }
                    std::uint64_t ts = c.sin_c[9];
                    for (std::size_t j = 8u; j >= 1u; --j) {
                        ts = c.sin_c[j] - mul_hi(y, ts);
                    }
                    const std::uint64_t cos_q   = std::uint64_t{0} - mul_hi(y, tc); // 1 - y tc, in (0.707, 1)
                    const std::uint64_t sinc_q  = std::uint64_t{0} - mul_hi(y, ts); // sin(theta) / theta
                    const std::uint64_t sin_abs = mul_hi(theta_q, sinc_q);          // sin theta, Q0.64
                    const wide_real     y_rel   = normalized(mul_hi(theta.m, theta.m), 2 * theta.e + 64);
                    octant_values       r{};
                    r.cos           = {cos_q, -64};
                    r.sin           = normalized(mul_hi(theta.m, sinc_q), theta.e);
                    r.one_minus_cos = normalized(mul_hi(y_rel.m, tc), y_rel.e);
                    r.one_minus_sin = normalized(std::uint64_t{0} - sin_abs, -64);
                    return r;
                }

                /// v / 2, exactly.
                constexpr wide_real halved(wide_real v) noexcept {
                    return v.m == 0u ? v : wide_real{v.m, v.e - 1};
                }

                /// The T nearest m x 2^e, ties to even on the 64-bit mantissa; a normal T.
                template <typename T>
                constexpr T to_sample(wide_real v) noexcept {
                    static_assert(std::numeric_limits<T>::is_iec559, "catenae: IEEE 754 binary32 or binary64");
                    if (v.m == 0u) {
                        return T(0);
                    }
                    constexpr auto      p    = static_cast<unsigned>(std::numeric_limits<T>::digits);
                    std::uint64_t       keep = v.m >> (64u - p);
                    const std::uint64_t rest = v.m << p;
                    constexpr auto      half = std::uint64_t{1} << 63u;
                    if (rest > half || (rest == half && (keep & 1u) != 0u)) {
                        ++keep;
                    }
                    // keep has at most p + 1 bits, so the conversion is exact; each scaling step is a
                    // power of two in the normal range, so each product is exact.
                    T   x = static_cast<T>(keep);
                    int e = v.e + 64 - static_cast<int>(p);
                    while (e < 0) {
                        const int step = e < -60 ? 60 : -e;
                        x              = x / static_cast<T>(std::uint64_t{1} << static_cast<unsigned>(step));
                        e += step;
                    }
                    while (e > 0) {
                        const int step = e > 60 ? 60 : e;
                        x              = x * static_cast<T>(std::uint64_t{1} << static_cast<unsigned>(step));
                        e -= step;
                    }
                    return x;
                }

            } // namespace fft_trig
        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SRC_FFT_TRIG_H
