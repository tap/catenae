/// @file fft_srdif.h
/// @brief The floating real FFT engine: a split-radix DIF kernel of length N/2 and the real post-pass.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Ported from DspTap's detail::srdif_rdft (include/tap/dsp/fft/srdif.h, NOTICE.md). A private
// header of the compiled core, instantiated for float and double in fft.cpp.
//
// What changed in the port. The buffer is N/2 + 1 values of complex<T>, D25's frame (PLAN.md
// section 5.2), not N scalars: the forward reads N real samples as the N/2 complex values
// z[n] = x[2n] + i x[2n + 1] in entries 0 to N/2 - 1 and writes every bin from DC to Nyquist;
// the inverse reads the bins and leaves the samples there. The sign is exp(-i 2 pi k n / N).
// The arithmetic is DspTap's exp(+i) engine's, with the conjugation folded in where it is
// exact: a subtraction written with its operands exchanged (y - x for -(x - y)), which agrees
// with the conjugate of DspTap's output in every bit but the sign of a zero, and one negation
// of bin N/4, which the post-pass does not touch. The Nyquist bin is written to entry N/2
// instead of being packed into bin 0's imaginary part. The tables are a constant of the
// caller's (a view, never an allocation); the compile-time blocks and levels are function
// templates over an index sequence where DspTap used C++20 template lambdas.
//
// Implemented from the published literature only, by part:
//
//   The real transform through a half-length complex one, and its inverse:
//     J. W. Cooley, P. A. W. Lewis and P. D. Welch, "The fast Fourier transform algorithm:
//       Programming considerations in the calculation of sine, cosine and Laplace
//       transforms," J. Sound Vib. 12(3), 315-337, 1970.
//     H. V. Sorensen, D. L. Jones, M. T. Heideman and C. S. Burrus, "Real-valued fast Fourier
//       transform algorithms," IEEE Trans. ASSP 35(6), 849-863, 1987.
//   The complex kernel, split-radix decimation in frequency:
//     P. Duhamel and H. Hollmann, "Split radix FFT algorithm," Electron. Lett. 20(1), 14-16,
//       1984.
//     H. V. Sorensen, M. T. Heideman and C. S. Burrus, "On computing the split-radix FFT,"
//       IEEE Trans. ASSP 34(1), 152-156, 1986 (the L-shaped butterfly, the special
//       butterflies, and the count 4M log2 M - 6M + 8 this kernel attains).
//     P. Duhamel and M. Vetterli, "Fast Fourier transforms: a tutorial review and a state of
//       the art," Signal Processing 19, 259-299, 1990 (the index maps of the fused pass).
//   The permutation: B. Gold and C. M. Rader, Digital Processing of Signals, McGraw-Hill,
//     1969 (the reverse-carry counter); A. H. Karp, "Bit reversal on uniprocessors," SIAM
//     Review 38(1), 1-26, 1996 (one counter serving several swaps).
//   The tables: fft_trig.h.
//
// The kernel. A split-radix step on a block of l values computes, for each j < l/4, the
// butterfly on x[j], x[j + l/4], x[j + l/2], x[j + 3l/4]: the sums feed the half-length block,
// and with t1 = x[j] - x[j + l/2], t2 = x[j + l/4] - x[j + 3l/4] the values (t1 + i t2) W^j and
// (t1 - i t2) W^3j feed the two quarter-length blocks, W = exp(+2 pi i / l) (the conjugates in
// the inverse). The fused pass runs two levels per load and store on groups of eight values;
// the special butterflies at j = 0 and j = l/8 take no general product; groups j and l/8 - j
// share their six twiddles by exact symmetry; blocks of 32 and 64 are compile-time, and blocks
// of 16 and fewer run one level at a time in memory down to pairs. The kernel leaves its
// outputs in bit-reversed order, which the permutation undoes.
//
// The tables, in T, in this order (table_size): post, C_k = (1 + i W_N^k) / 2 for k < N/4 as
// ((1 - sin theta_k) / 2, cos theta_k / 2), N/2 values; small, the fused pass's twiddles at
// M = 32 (12 values) and M = 64 (36); kernel, twelve values per jj in [1, M/16) for the
// run-time fused pass (M >= 128). small and kernel are read out of post's imaginary parts.
#ifndef CATENAE_SRC_FFT_SRDIF_H
#define CATENAE_SRC_FFT_SRDIF_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "catenae/detail/abi.h"
#include "catenae/sample_format.h"
#include "fft_trig.h"

// Under Clang, and not when optimizing for size, the kernel's building blocks are forced inline
// into the pass that calls them (a performance attribute only: the arithmetic and its order are
// the same either way). DspTap measured it on Hexagon (clang 19) and a Clang Cortex-M33 build;
// GCC is left to its own choices, which were cheaper on the GCC Cortex-M keys.
#if defined(__clang__) && !defined(__OPTIMIZE_SIZE__)
#define CATENAE_SRDIF_INLINE __attribute__((always_inline))
#else
#define CATENAE_SRDIF_INLINE
#endif

// Under GCC each compile-time block the kernel's switch reaches is a function of its own. Inlined
// into the switch, GCC gives the blocks' locals separate stack slots, so the kernel's frame is
// their sum: 944 bytes on the Cortex-M33 at -O3, beyond the library's 512 (tools/warnings). The
// kernel is a function of its own for the same reason: inlined into a transform, its list of
// pending blocks and the fused pass made a 520-byte frame on the soft-float Cortex-M4.
#if defined(__GNUC__) && !defined(__clang__)
#define CATENAE_SRDIF_BLOCK __attribute__((noinline))
#else
#define CATENAE_SRDIF_BLOCK
#endif

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// The floating engine over D25's frame, for T = float or double.
            template <typename T>
            struct srdif {
                static_assert(std::is_same<T, float>::value || std::is_same<T, double>::value,
                              "catenae: the floating engine is float or double");

                using cx = complex<T>;

                /// The values of T a length-n table holds.
                static constexpr std::size_t small_size(std::size_t m) noexcept {
                    return m >= 64u ? 48u : (m == 32u ? 12u : 0u);
                }
                static constexpr std::size_t kernel_size(std::size_t m) noexcept {
                    return m >= 128u ? 12u * (m / 16u - 1u) : 0u;
                }
                static constexpr std::size_t table_size(std::size_t n) noexcept {
                    return n / 2u + small_size(n / 2u) + kernel_size(n / 2u);
                }

                static constexpr int log2_of(std::size_t n) noexcept {
                    int l = 0;
                    while ((std::size_t{1} << static_cast<unsigned>(l)) < n) {
                        ++l;
                    }
                    return l;
                }

                /// Builds the length-n table at t (table_size(n) values). Constexpr: the same
                /// statements make a constant and a table in a caller's storage.
                static constexpr void fill_tables(T* t, std::size_t n) noexcept {
                    const std::size_t m = n / 2u;
                    fill_post(t, n);
                    T* const small_tables = t + n / 2u;
                    if (m >= 32u) {
                        fill_kernel(small_tables, 32u, t, n);
                    }
                    if (m >= 64u) {
                        fill_kernel(small_tables + 12, 64u, t, n);
                    }
                    if (m >= 128u) {
                        fill_kernel(small_tables + 48, m, t, n);
                    }
                }

                /// C_k = (1 + i W_n^k) / 2 for k in [0, n/4), interleaved: n/2 values at t.
                static constexpr void fill_post(T* t, std::size_t n) noexcept {
                    const std::size_t quarter = n / 4u;
                    const std::size_t octant  = n / 8u;
                    const int         lg      = log2_of(n);
                    t[0]                      = T(0.5);
                    t[1]                      = T(0.5);
                    for (std::size_t b = 1u; b <= octant; ++b) {
                        const fft_trig::octant_values v = fft_trig::first_octant(b, lg);
                        // k = b: re = (1 - sin) / 2, im = cos / 2.
                        t[2u * b]      = fft_trig::to_sample<T>(fft_trig::halved(v.one_minus_sin));
                        t[2u * b + 1u] = fft_trig::to_sample<T>(fft_trig::halved(v.cos));
                        // k = n/4 - b: sin theta_k = cos theta_b, cos theta_k = sin theta_b.
                        const std::size_t k = quarter - b;
                        if (k != b && k > 0u) {
                            t[2u * k]      = fft_trig::to_sample<T>(fft_trig::halved(v.one_minus_cos));
                            t[2u * k + 1u] = fft_trig::to_sample<T>(fft_trig::halved(v.sin));
                        }
                    }
                }

                /// cos and sin of 2 pi angle / res, read from the post table (cos theta_r =
                /// 2 im_r; sin theta_r = 2 im_(n/4 - r)) and turned by quarter turns exactly.
                static constexpr cx unit(std::size_t angle, std::size_t res, const T* post, std::size_t n) noexcept {
                    const std::size_t quarter = n / 4u;
                    const std::size_t k       = (angle % res) * (n / res);
                    const std::size_t qd      = k / quarter;
                    const std::size_t r       = k % quarter;
                    T                 c       = T(2) * post[2u * r + 1u];
                    T                 sn      = r == 0u ? T(0) : T(2) * post[2u * (quarter - r) + 1u];
                    for (std::size_t q = 0u; q < qd; ++q) { // times i per quarter turn
                        const T t0 = c;
                        c          = -sn;
                        sn         = t0;
                    }
                    return {c, sn};
                }

                /// The fused pass's twiddles at resolution res, twelve values per jj in
                /// [1, res/16): W^jj, W^3jj, W^(jj + res/8), W^(3jj + 3res/8), W^2jj, W^6jj.
                static constexpr void fill_kernel(T* t, std::size_t res, const T* post, std::size_t n) noexcept {
                    for (std::size_t jj = 1u; jj < res / 16u; ++jj) {
                        T* const e    = t + 12u * (jj - 1u);
                        const cx w[6] = {unit(jj, res, post, n),
                                         unit(3u * jj, res, post, n),
                                         unit(jj + res / 8u, res, post, n),
                                         unit(3u * jj + 3u * (res / 8u), res, post, n),
                                         unit(2u * jj, res, post, n),
                                         unit(6u * jj, res, post, n)};
                        for (std::size_t i = 0u; i < 6u; ++i) {
                            e[2u * i]      = w[i].re;
                            e[2u * i + 1u] = w[i].im;
                        }
                    }
                }

                /// The forward transform of the n real samples held as a[0 .. n/2) into the
                /// n/2 + 1 bins a[0 .. n/2].
                static void forward(const T* tables, std::size_t n, cx* a) noexcept {
                    kernel<false>(tables, n, a, n / 2u);
                    permute(n, a);
                    post_pass<false>(tables, n, a);
                }

                /// The inverse: the bins a[0 .. n/2] into (n/2) x the samples, as a[0 .. n/2).
                static void inverse(const T* tables, std::size_t n, cx* a) noexcept {
                    post_pass<true>(tables, n, a);
                    kernel<true>(tables, n, a, n / 2u);
                    permute(n, a);
                }

                // ---- the permutation ----

                /// The bit-reversal permutation of the m = n/2 complex values, with no index
                /// table: a reversed counter beside the forward index. With m = 2^b >= 16,
                /// i = A m/4 + 4y + B (A, B < 4, y < m/16) has rev(i) = rev2(B) m/4 + 4 rev'(y)
                /// + rev2(A), so one counter over y serves sixteen indices: six swap always,
                /// four when y < rev'(y), and six are the partners.
                static void permute(std::size_t n, cx* a) noexcept {
                    const std::size_t m = n / 2u;
                    if (m < 16u) {
                        std::size_t j = 0u; // rev(i)
                        for (std::size_t i = 0u; i < m; ++i) {
                            if (i < j) {
                                swap(a + i, a + j);
                            }
                            j ^= m - (m >> (trailing_ones(i) + 1u));
                        }
                        return;
                    }
                    const std::size_t q = m / 4u; // a quarter (A)
                    cx*               p = a;      // row 0 at i; rows 1 to 3 at p + q, p + 2q, p + 3q
                    std::size_t       j = 0u;
                    for (std::size_t i = 0u; i < q; i += 4u, p += 4) {
                        cx* const s0 = a + j;
                        cx* const s1 = s0 + q;
                        cx* const s2 = s1 + q;
                        cx* const s3 = s2 + q;
                        cx* const p1 = p + q;
                        cx* const p2 = p1 + q;
                        // (A, B) <-> (rev2 B, rev2 A), A < rev2 B.
                        swap(p + 1, s2);      // (0, 1) <-> (2, 0)
                        swap(p1 + 1, s2 + 2); // (1, 1) <-> (2, 2)
                        swap(p + 2, s1);      // (0, 2) <-> (1, 0)
                        swap(p + 3, s3);      // (0, 3) <-> (3, 0)
                        swap(p1 + 3, s3 + 2); // (1, 3) <-> (3, 2)
                        swap(p2 + 3, s3 + 1); // (2, 3) <-> (3, 1)
                        if (i < j) {          // A = rev2 B: (0, 0), (2, 1), (1, 2), (3, 3)
                            swap(p, s0);
                            swap(p2 + 1, s2 + 1);
                            swap(p1 + 2, s1 + 2);
                            swap(p2 + q + 3, s3 + 3);
                        }
                        j ^= q - (q >> (trailing_ones(i >> 2u) + 1u));
                    }
                }

                static constexpr unsigned trailing_ones(std::size_t v) noexcept {
                    unsigned c = 0u;
                    while ((v & 1u) != 0u) {
                        v >>= 1u;
                        ++c;
                    }
                    return c;
                }

                CATENAE_SRDIF_INLINE static void swap(cx* p, cx* q) noexcept {
                    const cx t = *p;
                    *p         = *q;
                    *q         = t;
                }

                // ---- the butterflies ----

                static constexpr T k_sqrt_half =
                    std::is_same<T, float>::value ? T(0x1.6a09e6p-1) : T(0x1.6a09e667f3bcdp-1);
                static constexpr T k_cos_pi8 =
                    std::is_same<T, float>::value ? T(0x1.d906bcp-1) : T(0x1.d906bcf328d46p-1);
                static constexpr T k_sin_pi8 =
                    std::is_same<T, float>::value ? T(0x1.87de2ap-2) : T(0x1.87de2a6aea963p-2);

                /// a b - c d, the first product in a statement of its own for float (with
                /// contraction off it is the same arithmetic; under Clang's contraction it is one
                /// multiply-subtract on Hexagon, as DspTap measured).
                CATENAE_SRDIF_INLINE static T mul_sub(T a, T b, T c, T d) noexcept {
                    const T ab = a * b;
                    return ab - c * d;
                }

                /// The three kinds of split-radix butterfly, by twiddle.
                enum class tw { one, eighth, general };

                /// One split-radix DIF butterfly at j, j + l/4, j + l/2, j + 3l/4 in place:
                /// x0 <- x0 + x2, x1 <- x1 + x3, x2 <- (t1 + i t2) W^j, x3 <- (t1 - i t2) W^3j
                /// with t1 = x0 - x2, t2 = x1 - x3 (the inverse conjugates i and both twiddles).
                template <bool Inverse, tw Kind>
                CATENAE_SRDIF_INLINE static void bf(cx& x0, cx& x1, cx& x2, cx& x3, cx w1, cx w3) noexcept {
                    const T t1r = x0.re - x2.re;
                    const T t1i = x0.im - x2.im;
                    const T t2r = x1.re - x3.re;
                    const T t2i = x1.im - x3.im;
                    x0          = {x0.re + x2.re, x0.im + x2.im};
                    x1          = {x1.re + x3.re, x1.im + x3.im};
                    // u = t1 + i t2, v = t1 - i t2 (forward); exchanged for the inverse.
                    const T ur = Inverse ? t1r + t2i : t1r - t2i;
                    const T ui = Inverse ? t1i - t2r : t1i + t2r;
                    const T vr = Inverse ? t1r - t2i : t1r + t2i;
                    const T vi = Inverse ? t1i + t2r : t1i - t2r;
                    if constexpr (Kind == tw::one) {
                        static_cast<void>(w1); // read by the general kind only
                        static_cast<void>(w3);
                        x2 = {ur, ui};
                        x3 = {vr, vi};
                    }
                    else if constexpr (Kind == tw::eighth) {
                        static_cast<void>(w1);
                        static_cast<void>(w3);
                        const T h = k_sqrt_half;
                        if constexpr (Inverse) { // u (1 - i) h, v (-1 - i) h
                            x2 = {h * (ur + ui), h * (ui - ur)};
                            x3 = {h * (vi - vr), -h * (vr + vi)};
                        }
                        else { // u (1 + i) h, v (-1 + i) h
                            x2 = {h * (ur - ui), h * (ur + ui)};
                            x3 = {-h * (vr + vi), h * (vr - vi)};
                        }
                    }
                    else if constexpr (Inverse) {
                        x2 = {ur * w1.re + ui * w1.im, mul_sub(ui, w1.re, ur, w1.im)};
                        x3 = {vr * w3.re + vi * w3.im, mul_sub(vi, w3.re, vr, w3.im)};
                    }
                    else {
                        x2 = {mul_sub(ur, w1.re, ui, w1.im), ur * w1.im + ui * w1.re};
                        x3 = {mul_sub(vr, w3.re, vi, w3.im), vr * w3.im + vi * w3.re};
                    }
                }

                /// One group of the fused pass: the eight values p[r e], r = 0 to 7, of a block
                /// of length l = 8e. The level-l butterflies at j and j + e, then the level-l/2
                /// butterfly at j of the first half, whose inputs are their first-half outputs.
                template <bool Inverse, tw KA, tw KB, tw KC>
                CATENAE_SRDIF_INLINE static void group(cx* p, std::size_t e, cx a1, cx a3, cx b1, cx b3, cx c1,
                                                       cx c3) noexcept {
                    cx x0 = p[0];
                    cx x2 = p[2u * e];
                    cx x4 = p[4u * e];
                    cx x6 = p[6u * e];
                    bf<Inverse, KA>(x0, x2, x4, x6, a1, a3);
                    p[4u * e] = x4;
                    p[6u * e] = x6;
                    cx x1     = p[e];
                    cx x3     = p[3u * e];
                    cx x5     = p[5u * e];
                    cx x7     = p[7u * e];
                    bf<Inverse, KB>(x1, x3, x5, x7, b1, b3);
                    p[5u * e] = x5;
                    p[7u * e] = x7;
                    bf<Inverse, KC>(x0, x1, x2, x3, c1, c3);
                    p[0]      = x0;
                    p[e]      = x1;
                    p[2u * e] = x2;
                    p[3u * e] = x3;
                }

                /// j = 0: W = 1, the eighth turn at j + e, W = 1 at level l/2.
                template <bool Inverse>
                CATENAE_SRDIF_INLINE static void group_first(cx* a, std::size_t e) noexcept {
                    group<Inverse, tw::one, tw::eighth, tw::one>(a, e, {}, {}, {}, {}, {}, {});
                }

                /// j in [1, e/2), twelve twiddles at w.
                template <bool Inverse>
                CATENAE_SRDIF_INLINE static void group_low(cx* a, std::size_t e, const T* w) noexcept {
                    group<Inverse, tw::general, tw::general, tw::general>(
                        a, e, {w[0], w[1]}, {w[2], w[3]}, {w[4], w[5]}, {w[6], w[7]}, {w[8], w[9]}, {w[10], w[11]});
                }

                /// j = e/2 = l/16: W_16^1, W_16^3; W_16^3, W_16^9 = -W_16^1; the eighth turn at l/2.
                template <bool Inverse>
                CATENAE_SRDIF_INLINE static void group_mid(cx* a, std::size_t e) noexcept {
                    const cx c16{k_cos_pi8, k_sin_pi8};
                    const cx s16{k_sin_pi8, k_cos_pi8};
                    group<Inverse, tw::general, tw::general, tw::eighth>(a, e, c16, s16, s16, {-k_cos_pi8, -k_sin_pi8},
                                                                         {}, {});
                }

                /// j in (e/2, e), from the entry of j' = e - j by exact symmetry.
                template <bool Inverse>
                CATENAE_SRDIF_INLINE static void group_high(cx* a, std::size_t e, const T* w) noexcept {
                    group<Inverse, tw::general, tw::general, tw::general>(a, e, {w[5], w[4]}, {-w[7], -w[6]},
                                                                          {w[1], w[0]}, {-w[3], -w[2]}, {w[9], w[8]},
                                                                          {-w[11], -w[10]});
                }

                /// One group per loop step in double, two in float (DspTap's Hexagon measurement).
                static constexpr bool k_one_group_per_step = sizeof(T) == 8u;

                /// The fused pass over a block of l >= 128 values; the entry of j is j (m/l) steps.
                template <bool Inverse>
                static void fused_pass(const T* tables, std::size_t n, cx* a, std::size_t l) noexcept {
                    const std::size_t e      = l / 8u;
                    const std::size_t h      = e / 2u;
                    const std::size_t stride = 12u * ((n / 2u) / l);
                    group_first<Inverse>(a, e);
                    const T* w = tables + n / 2u + 48u + stride - 12u;
                    if constexpr (k_one_group_per_step) {
                        for (std::size_t j = 1u; j < h; ++j, w += stride) {
                            group_low<Inverse>(a + j, e, w);
                        }
                        group_mid<Inverse>(a + h, e);
                        for (std::size_t j = h + 1u; j < e; ++j) {
                            w -= stride;
                            group_high<Inverse>(a + j, e, w);
                        }
                    }
                    else {
                        group_low<Inverse>(a + 1, e, w);
                        w += stride;
                        for (std::size_t j = 2u; j < h; j += 2u, w += 2u * stride) {
                            group_low<Inverse>(a + j, e, w);
                            group_low<Inverse>(a + j + 1u, e, w + stride);
                        }
                        group_mid<Inverse>(a + h, e);
                        w -= stride;
                        group_high<Inverse>(a + h + 1u, e, w);
                        for (std::size_t j = h + 2u; j < e; j += 2u) {
                            w -= 2u * stride;
                            group_high<Inverse>(a + j, e, w + stride);
                            group_high<Inverse>(a + j + 1u, e, w);
                        }
                    }
                }

                /// The groups of a compile-time block, low and high, from the small table.
                template <bool Inverse, std::size_t E, std::size_t... J>
                CATENAE_SRDIF_INLINE static void block_groups(cx* a, const T* w, std::index_sequence<J...>) noexcept {
                    using expand = int[];
                    static_cast<void>(expand{0, (group_low<Inverse>(a + (J + 1u), E, w + 12u * J), 0)...});
                    static_cast<void>(expand{0, (group_high<Inverse>(a + (E - 1u - J), E, w + 12u * J), 0)...});
                }

                /// A block of L <= 64 values, everything known at compile time: the fused pass
                /// over the small tables (L = 32, 64), one level in memory (L = 4, 8, 16), or a
                /// final pair.
                template <bool Inverse, std::size_t L>
                CATENAE_SRDIF_INLINE static void block(cx* a, const T* small_tables) noexcept {
                    if constexpr (L >= 32u) {
                        constexpr std::size_t e = L / 8u;
                        constexpr std::size_t h = e / 2u;
                        const T* const        w = small_tables + (L == 32u ? 0u : 12u);
                        group_first<Inverse>(a, e);
                        block_groups<Inverse, e>(a, w, std::make_index_sequence<h - 1u>{});
                        group_mid<Inverse>(a + h, e);
                        block<Inverse, L / 4u>(a, small_tables);
                        block<Inverse, L / 8u>(a + L / 4u, small_tables);
                        block<Inverse, L / 8u>(a + L / 4u + L / 8u, small_tables);
                        block<Inverse, L / 4u>(a + L / 2u, small_tables);
                        block<Inverse, L / 4u>(a + L / 2u + L / 4u, small_tables);
                    }
                    else if constexpr (L >= 4u) {
                        level<Inverse, L>(a, std::make_index_sequence<L / 4u>{});
                        block<Inverse, L / 2u>(a, small_tables);
                        if constexpr (L >= 8u) { // blocks of one value are their own transforms
                            block<Inverse, L / 4u>(a + L / 2u, small_tables);
                            block<Inverse, L / 4u>(a + L / 2u + L / 4u, small_tables);
                        }
                    }
                    else {
                        static_assert(L == 2u, "catenae: blocks of 2 to 64 values");
                        static_cast<void>(small_tables); // the pairs need no table
                        const cx x0 = a[0];
                        const cx x1 = a[1];
                        a[0]        = {x0.re + x1.re, x0.im + x1.im};
                        a[1]        = {x0.re - x1.re, x0.im - x1.im};
                    }
                }

                /// The first split-radix level of a block of L = 4, 8 or 16 values in memory.
                template <bool Inverse, std::size_t L, std::size_t... J>
                CATENAE_SRDIF_INLINE static void level(cx* a, std::index_sequence<J...>) noexcept {
                    using expand = int[];
                    static_cast<void>(expand{0, (level_bf<Inverse, L, J>(a), 0)...});
                }

                template <bool Inverse, std::size_t L, std::size_t J>
                CATENAE_SRDIF_INLINE static void level_bf(cx* a) noexcept {
                    constexpr std::size_t q    = L / 4u;
                    cx                    x0   = a[J];
                    cx                    x1   = a[J + q];
                    cx                    x2   = a[J + 2u * q];
                    cx                    x3   = a[J + 3u * q];
                    constexpr tw          kind = J == 0u ? tw::one : (J == L / 8u ? tw::eighth : tw::general);
                    // L = 16: J = 1 takes W_16^1, W_16^3; J = 3 takes W_16^3, W_16^9 = -W_16^1.
                    const cx w1 = J == 1u ? cx{k_cos_pi8, k_sin_pi8} : cx{k_sin_pi8, k_cos_pi8};
                    const cx w3 = J == 1u ? cx{k_sin_pi8, k_cos_pi8} : cx{-k_cos_pi8, -k_sin_pi8};
                    bf<Inverse, kind>(x0, x1, x2, x3, w1, w3);
                    a[J]          = x0;
                    a[J + q]      = x1;
                    a[J + 2u * q] = x2;
                    a[J + 3u * q] = x3;
                }

                /// A compile-time block as the kernel's switch reaches it (CATENAE_SRDIF_BLOCK).
                template <bool Inverse, std::size_t L>
                CATENAE_SRDIF_BLOCK static void root_block(cx* a, const T* small_tables) noexcept {
                    block<Inverse, L>(a, small_tables);
                }

                /// The most blocks the kernel holds pending: a block of l >= 128 is replaced by its
                /// five, l/4 to l/8, and l <= 32768 halves at least twice per level, so at most five
                /// levels fuse and the list grows by four at each.
                static constexpr std::size_t k_pending = 1u + 4u * 5u;

                /// Split-radix DIF over l values at a, output in bit-reversed order. The recursion
                /// on the five blocks a fused pass leaves is a loop over a list of the blocks still
                /// to do, taken depth first in the recursion's order (the blocks are disjoint, so the
                /// order moves no bit): no function calls itself, so the stack bound reads every frame
                /// (scripts/ceilings.py), and the fused pass has one call site, so GCC keeps it inline.
                template <bool Inverse>
                CATENAE_SRDIF_BLOCK static void kernel(const T* tables, std::size_t n, cx* a, std::size_t l) noexcept {
                    // A pending block in one word: its offset is a multiple of its length (a power of
                    // two from 2 to 32768), so offset + length / 2 keeps both, the lowest set bit
                    // being half the length.
                    const auto entry = [](std::size_t at, std::size_t len) noexcept {
                        return static_cast<std::uint16_t>(at + len / 2u);
                    };
                    const T* const small_tables = tables + n / 2u;
                    std::uint16_t  pending[k_pending];
                    std::size_t    count = 1u;
                    pending[0]           = entry(0u, l);
                    while (count > 0u) {
                        --count;
                        const std::size_t code = pending[count];
                        const std::size_t half = code & (~code + 1u);
                        const std::size_t len  = 2u * half;
                        const std::size_t at   = code - half;
                        cx* const         p    = a + at;
                        switch (len) {
                        case 64u:
                            root_block<Inverse, 64u>(p, small_tables);
                            continue;
                        case 32u:
                            root_block<Inverse, 32u>(p, small_tables);
                            continue;
                        case 16u:
                            root_block<Inverse, 16u>(p, small_tables);
                            continue;
                        case 8u:
                            root_block<Inverse, 8u>(p, small_tables);
                            continue;
                        case 4u:
                            root_block<Inverse, 4u>(p, small_tables);
                            continue;
                        case 2u:
                            root_block<Inverse, 2u>(p, small_tables);
                            continue;
                        default:
                            break;
                        }
                        if (len >= 128u) {
                            const std::size_t m = len;
                            fused_pass<Inverse>(tables, n, p, m);
                            // Pushed last first, so the first is taken next, as the recursion did.
                            pending[count]      = entry(at + m / 2u + m / 4u, m / 4u);
                            pending[count + 1u] = entry(at + m / 2u, m / 4u);
                            pending[count + 2u] = entry(at + m / 4u + m / 8u, m / 8u);
                            pending[count + 3u] = entry(at + m / 4u, m / 8u);
                            pending[count + 4u] = entry(at, m / 4u);
                            count += 5u;
                        }
                    }
                }

                // ---- the real post-pass ----

                /// The four outputs of one bin pair.
                struct post_out {
                    T kr;
                    T ki;
                    T jr;
                    T ji;
                };

                /// One bin pair (k, m - k) in exp(-i) terms. The forward: with u = Z_k and the
                /// conjugate partner as DspTap reads it, G = C_k (u - v), and the outputs are
                /// conj(u - G) and v + G; each negated imaginary part is the subtraction with its
                /// operands exchanged. The inverse reads X and its partner, s = Im X_k + Im X_(m-k)
                /// standing for -(DspTap's sum), so g' = -(DspTap's g) and the outputs follow.
                template <bool Inverse>
                CATENAE_SRDIF_INLINE static post_out post_math(T ur, T ui, T vr, T vi, T cr, T ci) noexcept {
                    const T dr = ur - vr;
                    if constexpr (Inverse) {
                        const T s  = ui + vi;
                        const T gr = mul_sub(dr, cr, s, ci);
                        const T gp = s * cr + dr * ci;
                        return {ur - gr, gp - ui, vr + gr, gp - vi};
                    }
                    else {
                        const T di = ui + vi;
                        const T gr = mul_sub(dr, cr, di, ci);
                        const T gi = dr * ci + di * cr;
                        return {ur - gr, gi - ui, vr + gr, gi - vi};
                    }
                }

                template <bool Inverse>
                CATENAE_SRDIF_INLINE static void post_pair(cx* pk, cx* pj, const T* c) noexcept {
                    const post_out o = post_math<Inverse>(pk->re, pk->im, pj->re, pj->im, c[0], c[1]);
                    *pk              = {o.kr, o.ki};
                    *pj              = {o.jr, o.ji};
                }

                /// Two bin pairs, every load before any store (DspTap measured it on Hexagon).
                template <bool Inverse>
                CATENAE_SRDIF_INLINE static void post_two(cx* pk, cx* pj, const T* c) noexcept {
                    const cx       u0 = pk[0];
                    const cx       u1 = pk[1];
                    const cx       v1 = pj[-1];
                    const cx       v0 = pj[0];
                    const post_out o0 = post_math<Inverse>(u0.re, u0.im, v0.re, v0.im, c[0], c[1]);
                    const post_out o1 = post_math<Inverse>(u1.re, u1.im, v1.re, v1.im, c[2], c[3]);
                    pk[0]             = {o0.kr, o0.ki};
                    pk[1]             = {o1.kr, o1.ki};
                    pj[-1]            = {o1.jr, o1.ji};
                    pj[0]             = {o0.jr, o0.ji};
                }

                /// The real post-pass (forward) or pre-pass (Inverse), with D25's DC, Nyquist
                /// and N/4 bins.
                template <bool Inverse>
                static void post_pass(const T* tables, std::size_t n, cx* a) noexcept {
                    const std::size_t m = n / 2u;
                    if constexpr (Inverse) {
                        const T r = a[0].re;
                        const T i = a[m].re;
                        a[0]      = {T(0.5) * (r + i), T(0.5) * (r - i)};
                    }
                    else {
                        const T r = a[0].re;
                        const T i = a[0].im;
                        a[0]      = {r + i, T(0)};
                        a[m]      = {r - i, T(0)};
                    }
                    // Bin m/2 (N/4) is its own partner: its coefficient is zero, so it is only
                    // conjugated.
                    a[m / 2u].im = -a[m / 2u].im;
                    if (m < 4u) {
                        return;
                    }
                    // Pairs (k, m - k) for k in [1, m/2): k = 1 alone, then two per step.
                    cx*             pk  = a + 1;
                    cx*             pj  = a + (m - 1u);
                    const T*        c   = tables + 2;
                    const cx* const end = a + m / 2u;
                    post_pair<Inverse>(pk, pj, c);
                    for (pk += 1, pj -= 1, c += 2; pk < end; pk += 2, pj -= 2, c += 4) {
                        post_two<Inverse>(pk, pj, c);
                    }
                }
            };

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SRC_FFT_SRDIF_H
