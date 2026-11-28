/// @file window.h
/// @brief The analysis and synthesis windows of `stft` and `istft`: five named windows as
/// constants of the compiled core, Kaiser's made by `make()`, and `window_design<S, N>`, the view
/// a stage holds (C5, D37, D54).
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | Windows     | periodic, of length N: w[n] for n = 0 to N - 1, symmetric, w[n] = w[N - n],  |
/// |             | so N/2 + 1 values are stored. Hann 0.5 - 0.5 cos t; square-root Hann         |
/// |             | sin(t / 2); Hamming 0.54 - 0.46 cos t; Blackman 0.42 - 0.5 cos t + 0.08     |
/// |             | cos 2t; Blackman-Harris 0.35875 - 0.48829 cos t + 0.14128 cos 2t - 0.01168   |
/// |             | cos 3t, its minimum 4-term form; t = 2 pi n / N (Harris, "On the use of      |
/// |             | windows for harmonic analysis with the discrete Fourier transform", Proc.   |
/// |             | IEEE 66(1), 1978). Kaiser I0(beta sqrt(1 - (2n / N - 1)^2)) / I0(beta)       |
/// |             | (Kaiser, "Nonrecursive digital filter design using the I0-sinh window       |
/// |             | function", 1974)                                                            |
/// | Arithmetic  | the cosines are the FFT's own, integer arithmetic (src/fft_trig.h), each     |
/// |             | sum written in 1 - cos so no term cancels, in double, then rounded to S once: |
/// |             | the same bits on every target, from a table or from `make()`. Kaiser's I0 is |
/// |             | its power series in double (the FIR designer's), and its argument a square  |
/// |             | root: IEEE arithmetic only                                                  |
/// | Constants   | `window::hann`, `sqrt_hann`, `hamming`, `blackman`, `blackman_harris` name a |
/// |             | table of the core, `window_tables<S, Shape, N>`, at each power of two N from |
/// |             | 16 to 8192 (`window_max_table_size`): constant, one section per table, so an |
/// |             | image keeps only those it names                                             |
/// | Make        | `make(shape, table)` builds a named window at any FFT length into the       |
/// |             | caller's N/2 + 1 values, the constants' bits; `make(window::kaiser{beta},     |
/// |             | table)` a Kaiser                                                            |
/// | Table       | `from_table(half)` checks, in S: every value finite, w[0] not negative and   |
/// |             | every other value above 0, so a hop of at most N/2 never meets a zero sum   |
/// |             | of squares; a failing table does not initialise a constant, and at run time |
/// |             | reaches `check::table_rejected`                                            |
/// | Rejects     | `make`: `status::invalid_argument` for a beta that is NaN, infinite or       |
/// |             | negative; `status::out_of_range` for a beta above 50                        |
/// | Formats     | float and double; q31 declares its contract, its windows arriving with the  |
/// |             | fixed-point spectra at C6 (D38, D47)                                        |
/// | Rationale   | D12, D24, D37, D38, D47, D54                                                |
///
/// @req{CAT-WINDOW-001} Each named window, from its table or from `make()`, is its formula at
/// every n from 0 to N/2, within the rounding to S of a double computed from exact cosines, and
/// the table's bits are `make()`'s; Kaiser's is its formula within float's or double's rounding.
/// @req{CAT-WINDOW-002} `make()` rejects what its card names with the status it names, and
/// `from_table` accepts a window of finite values positive past n = 0 and refuses any other: in
/// a constant it does not compile, at run time `check::table_rejected`.
/// @req{CAT-WINDOW-003} A named window has a constant only at a power of two from 16 to 8192;
/// another length does not compile. In q31 the contract is instantiated and a window does not
/// compile until C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_WINDOW_H
#define CATENAE_WINDOW_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/expected.h"
#include "catenae/fft.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// The named windows' shapes.
        enum class window_shape : std::uint8_t {
            hann,            ///< 0.5 - 0.5 cos t
            sqrt_hann,       ///< sin(t / 2), whose square is Hann
            hamming,         ///< 0.54 - 0.46 cos t
            blackman,        ///< 0.42 - 0.5 cos t + 0.08 cos 2t
            blackman_harris, ///< the minimum 4-term Blackman-Harris
        };

        /// The windows a stage's configuration names.
        namespace window {
            /// A named window: its table is a constant of the core.
            template <window_shape Shape>
            struct named {
                static constexpr window_shape shape = Shape; ///< which
            };
            inline constexpr named<window_shape::hann>            hann{};            ///< periodic Hann
            inline constexpr named<window_shape::sqrt_hann>       sqrt_hann{};       ///< its square root
            inline constexpr named<window_shape::hamming>         hamming{};         ///< Hamming
            inline constexpr named<window_shape::blackman>        blackman{};        ///< Blackman
            inline constexpr named<window_shape::blackman_harris> blackman_harris{}; ///< Blackman-Harris

            /// Kaiser's window of parameter beta, made by `make()`.
            struct kaiser {
                double beta; ///< 0 is the rectangle; larger is narrower and lower in its sidelobes
            };
        } // namespace window

        /// The shortest window with a constant.
        inline constexpr std::size_t window_min_size = 16;
        /// The longest window with a constant; `make()` builds a longer one.
        inline constexpr std::size_t window_max_table_size = 8192;

        /// Whether a named window of length n has a constant.
        constexpr bool window_has_table(std::size_t n) noexcept {
            return fft_supports(n) && n >= window_min_size && n <= window_max_table_size;
        }

        /// The constant half of a named window, N/2 + 1 values, from the compiled core.
        template <typename S, window_shape Shape, std::size_t N>
        struct window_tables {
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value,
                          "catenae: a window's constant is float or double; fixed-point windows arrive at C6 (D47)");
            static_assert(window_has_table(N),
                          "catenae: a named window's constant is a power of two from 16 to 8192; make() builds "
                          "another");
            static const std::array<storage_t<S>, N / 2 + 1> values; ///< w[0] to w[N/2]
        };

        // The constants the core defines (src/window.cpp), declared so that no image instantiates
        // one of its own.
#define CATENAE_WINDOW_EXTERN_SHAPES(S, N)                                                                             \
    extern template struct window_tables<S, window_shape::hann, N>;                                                    \
    extern template struct window_tables<S, window_shape::sqrt_hann, N>;                                               \
    extern template struct window_tables<S, window_shape::hamming, N>;                                                 \
    extern template struct window_tables<S, window_shape::blackman, N>;                                                \
    extern template struct window_tables<S, window_shape::blackman_harris, N>;
#define CATENAE_WINDOW_EXTERN_LENGTHS(S)                                                                               \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 16)                                                                                \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 32)                                                                                \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 64)                                                                                \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 128)                                                                               \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 256)                                                                               \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 512)                                                                               \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 1024)                                                                              \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 2048)                                                                              \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 4096)                                                                              \
    CATENAE_WINDOW_EXTERN_SHAPES(S, 8192)
        CATENAE_WINDOW_EXTERN_LENGTHS(float)
        CATENAE_WINDOW_EXTERN_LENGTHS(double)
#undef CATENAE_WINDOW_EXTERN_LENGTHS
#undef CATENAE_WINDOW_EXTERN_SHAPES

        namespace detail {
            // The compiled windows (src/window.cpp): the tables' own arithmetic, into a caller's
            // N/2 + 1 values. Preconditions: n a power of two from 16 to 2^20, half of n/2 + 1.

            /// A named window's w[0] to w[n/2].
            void fill_window(window_shape shape, std::size_t n, span<float> half) noexcept;
            /// A named window's w[0] to w[n/2].
            void fill_window(window_shape shape, std::size_t n, span<double> half) noexcept;
            /// Kaiser's w[0] to w[n/2] for 0 <= beta <= 50.
            void fill_kaiser(double beta, std::size_t n, span<float> half) noexcept;
            /// Kaiser's w[0] to w[n/2] for 0 <= beta <= 50.
            void fill_kaiser(double beta, std::size_t n, span<double> half) noexcept;

            /// Whether a window's half is one `from_table` takes: finite, w[0] not negative and
            /// every other value above 0 (false for a NaN).
            template <typename T, std::size_t M>
            constexpr bool valid_window(const std::array<T, M>& half) noexcept {
                bool ok = half[0] >= T{0} && half[0] <= std::numeric_limits<T>::max();
                for (std::size_t n = 1; n < M; ++n) {
                    ok = ok && half[n] > T{0} && half[n] <= std::numeric_limits<T>::max();
                }
                return ok;
            }
            /// The same over a view.
            template <typename T, std::size_t M>
            constexpr bool valid_window(const table_view<T, M>& half) noexcept {
                bool ok = half[0] >= T{0} && half[0] <= std::numeric_limits<T>::max();
                for (std::size_t n = 1; n < M; ++n) {
                    ok = ok && half[n] > T{0} && half[n] <= std::numeric_limits<T>::max();
                }
                return ok;
            }
        } // namespace detail

        /// @brief A periodic window of length N in S: a view of its N/2 + 1 values, from a named
        /// window's constant, `make()` or a checked table.
        template <typename S, std::size_t N>
        class window_design {
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                              || std::is_same<S, q31>::value,
                          "catenae: a window is float, double or q31");
            static_assert(fft_has_tables(N) && N >= window_min_size,
                          "catenae: a window's length is a power of two from 16 to 65536, an FFT's with tables");

          public:
            using value_type                       = storage_t<S>;                      ///< one value
            static constexpr std::size_t size      = N;                                 ///< its length
            static constexpr std::size_t half_size = N / 2 + 1;                         ///< the values it views
            using table_type                       = table_view<value_type, half_size>; ///< what it views

            /// A named window's constant.
            template <window_shape Shape>
            explicit constexpr window_design(window::named<Shape>) noexcept
                : m_half(table_of<Shape>()) {}

            /// A named window at any length into the caller's table: the constant's bits.
            /// Outside the audio path.
            static window_design make(window_shape shape, span<value_type, half_size> table) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point windows arrive with the fixed-point spectra at C6 (D47)");
                detail::fill_window(shape, N, span<value_type>(table));
                return window_design(table_type(table));
            }

            /// Kaiser's window into the caller's table (the card's statuses). Outside the audio path.
            static expected<window_design, status> make(window::kaiser              kaiser,
                                                        span<value_type, half_size> table) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point windows arrive with the fixed-point spectra at C6 (D47)");
                const double beta = kaiser.beta;
                if (!(beta >= 0.0 && beta <= std::numeric_limits<double>::max())) {
                    return unexpected(status::invalid_argument);
                }
                if (beta > 50.0) {
                    return unexpected(status::out_of_range);
                }
                detail::fill_kaiser(beta, N, span<value_type>(table));
                return window_design(table_type(table));
            }

            /// A window from the caller's table of w[0] to w[N/2], checked: a table that fails does
            /// not initialise a constant; at run time it reaches the handler.
            static constexpr window_design from_table(const table_type& half) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point windows arrive with the fixed-point spectra at C6 (D47)");
                detail::check_always(detail::valid_window(half), check::table_rejected);
                return window_design(half);
            }

            /// w[0] to w[N/2].
            constexpr const table_type& half() const noexcept { return m_half; }
            /// w[n] for n from 0 to N - 1: the half, mirrored.
            constexpr value_type operator[](std::size_t n) const noexcept {
                return n <= N / 2 ? m_half[n] : m_half[N - n];
            }

          private:
            explicit constexpr window_design(const table_type& half) noexcept
                : m_half(half) {}

            template <window_shape Shape>
            static constexpr table_type table_of() noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point windows arrive with the fixed-point spectra at C6 (D47)");
                static_assert(window_has_table(N),
                              "catenae: a named window's constant is a power of two from 16 to 8192; make() builds "
                              "another");
                return table_type(window_tables<S, Shape, N>::values);
            }

            table_type m_half;
        };

        /// A window design views its table.
        template <typename S, std::size_t N>
        struct holds_view<window_design<S, N>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_WINDOW_H
