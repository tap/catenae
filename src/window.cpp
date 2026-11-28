/// @file window.cpp
/// @brief The STFT's windows: the five named windows' constants at every power of two from 16
/// to 8192, the same arithmetic into a caller's table, and Kaiser's (include/catenae/window.h).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The windows are Harris's ("On the use of windows for harmonic analysis with the discrete
// Fourier transform", Proc. IEEE 66(1), 1978), periodic: the cosine sums of t = 2 pi n / N, each
// written in u_j = 1 - cos(j t) so that no term cancels near n = 0, and the square-root Hann as
// sin(t / 2). The cosines are the FFT's own (fft_trig.h): integer arithmetic, so a table is a
// function of its shape and length alone, and this file's constant evaluation and a run-time
// make() write the same bits. The constants take each first-octant angle once per length from a
// table, and make() computes each where it needs it: the same values, so the same bits, at a
// sixth of the compile time a value-by-value evaluation took. Kaiser's window is Kaiser's
// (1974), its I0 the FIR designer's.
// Each table is constant-initialized, one section per shape, length and format, so an image
// keeps only the tables it names.
#include "catenae/window.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "bessel_i0.h"
#include "catenae/compiler.h"
#include "catenae/detail/convert.h"
#include "catenae/span.h"
#include "fft_trig.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                /// log2 of a power of two.
                constexpr int window_log2(std::size_t n) noexcept {
                    int l = 0;
                    while ((std::size_t{1} << static_cast<unsigned>(l)) < n) {
                        ++l;
                    }
                    return l;
                }

                /// The first octant's values computed where they are asked for: make()'s source, at
                /// any length.
                struct direct_octants {
                    static constexpr fft_trig::octant_values at(std::uint64_t k, int l) noexcept {
                        return fft_trig::first_octant(k, l);
                    }
                };

                /// The first-octant angles of 2^L: 2 pi k / 2^L, k = 0 to 2^L / 8.
                template <int L>
                constexpr std::array<fft_trig::octant_values, (std::size_t{1} << L) / 8u + 1u> make_octants() noexcept {
                    std::array<fft_trig::octant_values, (std::size_t{1} << L) / 8u + 1u> t{};
                    for (std::size_t k = 0; k < t.size(); ++k) {
                        t[k] = fft_trig::first_octant(k, L);
                    }
                    return t;
                }

                /// Every first-octant angle of 2^L, computed once.
                template <int L>
                struct octant_table {
                    static constexpr std::array<fft_trig::octant_values, (std::size_t{1} << L) / 8u + 1u> k_values =
                        make_octants<L>();
                };

                /// The constants' source for a window of 2^L samples: the same values, each angle
                /// computed once for every shape and format (the square-root Hann's at 2^(L + 1)).
                template <int L>
                struct tabled_octants {
                    static constexpr fft_trig::octant_values at(std::uint64_t k, int l) noexcept {
                        const std::size_t at =
                            detail::narrow<std::size_t>(k); // within the table, on a 32-bit target too
                        return l == L ? octant_table<L>::k_values[at] : octant_table<L + 1>::k_values[at];
                    }
                };

                /// The four values at a quarter-circle angle 2 pi r / 2^l, 0 <= r <= 2^l / 4: the first
                /// octant's directly, the second's by the complement.
                template <typename Octants>
                constexpr fft_trig::octant_values window_quarter(std::uint64_t r, int l) noexcept {
                    const std::uint64_t quarter = (std::uint64_t{1} << static_cast<unsigned>(l)) / 4u;
                    if (r <= quarter / 2u) {
                        return Octants::at(r, l);
                    }
                    const fft_trig::octant_values v = Octants::at(quarter - r, l);
                    return {v.sin, v.cos, v.one_minus_sin, v.one_minus_cos};
                }

                /// 1 - cos(2 pi k / 2^l), for any k, with relative precision where it is small.
                template <typename Octants>
                constexpr double window_one_minus_cos(std::uint64_t k, int l) noexcept {
                    const std::uint64_t           n       = std::uint64_t{1} << static_cast<unsigned>(l);
                    const std::uint64_t           quarter = n / 4u;
                    const std::uint64_t           at      = k % n;
                    const std::uint64_t           quad    = at / quarter;
                    const fft_trig::octant_values v       = window_quarter<Octants>(at % quarter, l);
                    switch (quad) {
                    case 0: // 1 - cos b
                        return fft_trig::to_sample<double>(v.one_minus_cos);
                    case 1: // 1 + sin b
                        return 1.0 + fft_trig::to_sample<double>(v.sin);
                    case 2: // 1 + cos b
                        return 1.0 + fft_trig::to_sample<double>(v.cos);
                    default: // 1 - sin b
                        return fft_trig::to_sample<double>(v.one_minus_sin);
                    }
                }

                /// A named window's w[n] for 0 <= n <= size / 2, in double.
                template <typename Octants>
                constexpr double window_value(window_shape shape, std::size_t n, std::size_t size) noexcept {
                    const int           l = window_log2(size);
                    const std::uint64_t k = n;
                    switch (shape) {
                    case window_shape::hann:
                        return 0.5 * window_one_minus_cos<Octants>(k, l);
                    case window_shape::sqrt_hann: // sin(pi n / N), a quarter circle of 2N
                        return fft_trig::to_sample<double>(window_quarter<Octants>(k, l + 1).sin);
                    case window_shape::hamming:
                        return 0.08 + 0.46 * window_one_minus_cos<Octants>(k, l);
                    case window_shape::blackman:
                        return 0.5 * window_one_minus_cos<Octants>(k, l)
                               - 0.08 * window_one_minus_cos<Octants>(2u * k, l);
                    case window_shape::blackman_harris:
                        return 0.00006 + 0.48829 * window_one_minus_cos<Octants>(k, l)
                               - 0.14128 * window_one_minus_cos<Octants>(2u * k, l)
                               + 0.01168 * window_one_minus_cos<Octants>(3u * k, l);
                    }
                    return 0.0; // no shape reaches it
                }

            } // namespace

            /// One named window's constant: w[0] to w[N/2], rounded to T once, its angles from the
            /// tables of its length.
            template <typename T, window_shape Shape, std::size_t N>
            constexpr std::array<T, N / 2 + 1> make_window_table() noexcept {
                std::array<T, N / 2 + 1> t{};
                for (std::size_t n = 0; n <= N / 2; ++n) {
                    t[n] = static_cast<T>(window_value<tabled_octants<window_log2(N)>>(Shape, n, N));
                }
                return t;
            }

            void fill_window(window_shape shape, std::size_t n, span<float> half) noexcept {
                for (std::size_t i = 0; i < half.size(); ++i) {
                    half[i] = static_cast<float>(window_value<direct_octants>(shape, i, n));
                }
            }
            void fill_window(window_shape shape, std::size_t n, span<double> half) noexcept {
                for (std::size_t i = 0; i < half.size(); ++i) {
                    half[i] = window_value<direct_octants>(shape, i, n);
                }
            }

            namespace {
                /// Kaiser's w[i] for 0 <= i <= n / 2: the argument 2i / n - 1 is exact for a power
                /// of two n.
                double kaiser_value(double beta, double i0_beta, std::size_t i, std::size_t n) noexcept {
                    const double t   = 2.0 * static_cast<double>(i) / static_cast<double>(n) - 1.0;
                    const double arg = 1.0 - t * t;
                    return bessel_i0(beta * std::sqrt(arg > 0.0 ? arg : 0.0)) / i0_beta;
                }
            } // namespace

            void fill_kaiser(double beta, std::size_t n, span<float> half) noexcept {
                const double i0_beta = bessel_i0(beta);
                for (std::size_t i = 0; i < half.size(); ++i) {
                    half[i] = static_cast<float>(kaiser_value(beta, i0_beta, i, n));
                }
            }
            void fill_kaiser(double beta, std::size_t n, span<double> half) noexcept {
                const double i0_beta = bessel_i0(beta);
                for (std::size_t i = 0; i < half.size(); ++i) {
                    half[i] = kaiser_value(beta, i0_beta, i, n);
                }
            }

        } // namespace detail

        template <typename S, window_shape Shape, std::size_t N>
        CATENAE_CONSTINIT const std::array<storage_t<S>, N / 2 + 1> window_tables<S, Shape, N>::values =
            detail::make_window_table<storage_t<S>, Shape, N>();

        // Every shape at every length from 16 to 8192, in float and double.
#define CATENAE_WINDOW_SHAPES(S, N)                                                                                    \
    template struct window_tables<S, window_shape::hann, N>;                                                           \
    template struct window_tables<S, window_shape::sqrt_hann, N>;                                                      \
    template struct window_tables<S, window_shape::hamming, N>;                                                        \
    template struct window_tables<S, window_shape::blackman, N>;                                                       \
    template struct window_tables<S, window_shape::blackman_harris, N>;
#define CATENAE_WINDOW_LENGTHS(S)                                                                                      \
    CATENAE_WINDOW_SHAPES(S, 16)                                                                                       \
    CATENAE_WINDOW_SHAPES(S, 32)                                                                                       \
    CATENAE_WINDOW_SHAPES(S, 64)                                                                                       \
    CATENAE_WINDOW_SHAPES(S, 128)                                                                                      \
    CATENAE_WINDOW_SHAPES(S, 256)                                                                                      \
    CATENAE_WINDOW_SHAPES(S, 512)                                                                                      \
    CATENAE_WINDOW_SHAPES(S, 1024)                                                                                     \
    CATENAE_WINDOW_SHAPES(S, 2048)                                                                                     \
    CATENAE_WINDOW_SHAPES(S, 4096)                                                                                     \
    CATENAE_WINDOW_SHAPES(S, 8192)
        CATENAE_WINDOW_LENGTHS(float)
        CATENAE_WINDOW_LENGTHS(double)
#undef CATENAE_WINDOW_LENGTHS
#undef CATENAE_WINDOW_SHAPES

    } // namespace CATENAE_ABI
} // namespace catenae
