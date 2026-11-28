/// @file test_window.cpp
/// @brief The STFT's windows (include/catenae/window.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "analyzer_support.h"
#include "catenae/check.h"
#include "catenae/window.h"
#include "catenae_test/check_trap.h"

#ifndef CATENAE_TEST_MAX_FFT_N
#define CATENAE_TEST_MAX_FFT_N 1048576
#endif

namespace {

    /// The longest constants checked: 8192, the longest there is, or a bare-metal leg's 4096
    /// (CATENAE_TEST_MAX_FFT_N): every shape at 8192 in both formats is 245,820 bytes of flash, which
    /// the M55's image does not have room for beside the rest of the battery.
    constexpr std::size_t k_longest = CATENAE_TEST_MAX_FFT_N < 8192 ? CATENAE_TEST_MAX_FFT_N : 8192;
    /// make()'s length past the constants checked.
    constexpr std::size_t k_made = 2 * k_longest;

    template <typename T>
    class window : public ::testing::Test {};
    using floating = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(window, floating, );

    constexpr double k_pi = 3.141592653589793238462643383279502884;

    /// Each named window's formula, at t = 2 pi n / N, in double from libm's cosines.
    double formula(catenae::window_shape shape, std::size_t n, std::size_t size) {
        const double t = 2.0 * k_pi * double(n) / double(size);
        switch (shape) {
        case catenae::window_shape::hann:
            return 0.5 - 0.5 * std::cos(t);
        case catenae::window_shape::sqrt_hann:
            return std::sin(0.5 * t);
        case catenae::window_shape::hamming:
            return 0.54 - 0.46 * std::cos(t);
        case catenae::window_shape::blackman:
            return 0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2.0 * t);
        case catenae::window_shape::blackman_harris:
            return 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
        }
        return 0.0;
    }

    constexpr catenae::window_shape k_shapes[] = {catenae::window_shape::hann, catenae::window_shape::sqrt_hann,
                                                  catenae::window_shape::hamming, catenae::window_shape::blackman,
                                                  catenae::window_shape::blackman_harris};

    /// A window's every value against its formula: within S's rounding, plus libm's cosines'
    /// cancellation (1e-15 absolute in double).
    template <typename T, std::size_t N>
    void expect_formula(const catenae::window_design<T, N>& w, catenae::window_shape shape) {
        const double tolerance = std::is_same<T, float>::value ? 6e-8 : 2e-15;
        for (std::size_t n = 0; n < N; ++n) {
            const double want = formula(shape, n, N);
            ASSERT_NEAR(static_cast<double>(w[n]), want, tolerance * (1.0 + std::fabs(want)))
                << "shape " << int(shape) << ", n " << n << " of " << N;
        }
        EXPECT_GE(w[0], T{0});
        for (std::size_t n = 1; n <= N / 2; ++n) {
            ASSERT_GT(w[n], T{0}) << "n " << n;
        }
    }

    /// The constant of one shape, as a design.
    template <typename T, std::size_t N>
    catenae::window_design<T, N> named(catenae::window_shape shape) {
        using d = catenae::window_design<T, N>;
        switch (shape) {
        case catenae::window_shape::hann:
            return d(catenae::window::hann);
        case catenae::window_shape::sqrt_hann:
            return d(catenae::window::sqrt_hann);
        case catenae::window_shape::hamming:
            return d(catenae::window::hamming);
        case catenae::window_shape::blackman:
            return d(catenae::window::blackman);
        case catenae::window_shape::blackman_harris:
            return d(catenae::window::blackman_harris);
        }
        return d(catenae::window::hann);
    }

    template <typename T, std::size_t N>
    void expect_tables() {
        for (const catenae::window_shape shape : k_shapes) {
            const auto constant = named<T, N>(shape);
            expect_formula(constant, shape);
            // make() writes the constant's bits.
            std::array<T, N / 2 + 1> made{};
            const auto               m = catenae::window_design<T, N>::make(shape, catenae::span<T, N / 2 + 1>(made));
            for (std::size_t n = 0; n <= N / 2; ++n) {
                ASSERT_TRUE(analyzer_test::same_bits(made[n], constant.half()[n]))
                    << "shape " << int(shape) << ", n " << n << " of " << N;
            }
            EXPECT_EQ(m.half().data(), made.data());
        }
    }

    // Verifies CAT-WINDOW-001, CAT-WINDOW-003.
    TYPED_TEST(window, NamedWindowsAreTheirFormulas) {
        using sample = TypeParam;
        expect_tables<sample, 16>();
        expect_tables<sample, 512>();
        expect_tables<sample, k_longest>();
        // Hann's quarter and half points exactly; sqrt-Hann squared is Hann.
        const catenae::window_design<sample, 512> h(catenae::window::hann);
        EXPECT_EQ(h[0], sample(0));
        EXPECT_EQ(h[128], sample(0.5));
        EXPECT_EQ(h[256], sample(1));
        EXPECT_EQ(h[384], sample(0.5));
        const catenae::window_design<sample, 512> r(catenae::window::sqrt_hann);
        const double                              square = std::is_same<sample, float>::value ? 2e-7 : 1e-15;
        for (std::size_t n = 0; n < 512; ++n) {
            EXPECT_NEAR(double(r[n]) * double(r[n]), double(h[n]), square);
        }
        // Past the constants checked, make() is the formula too (at 16384 hosted, past them all).
        std::vector<sample> big(k_made / 2 + 1);
        const auto          b = catenae::window_design<sample, k_made>::make(
            catenae::window_shape::blackman_harris, catenae::span<sample, k_made / 2 + 1>(big.data(), big.size()));
        expect_formula(b, catenae::window_shape::blackman_harris);
    }

    /// I0 by its series, written out here.
    double i0(double x) {
        double term = 1.0;
        double sum  = 1.0;
        for (int k = 1; k < 500; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    // Verifies CAT-WINDOW-001, CAT-WINDOW-002.
    TYPED_TEST(window, KaiserIsItsFormulaAndMakeRejects) {
        using sample = TypeParam;
        using d      = catenae::window_design<sample, 512>;
        std::array<sample, 257> table{};
        for (const double beta : {0.0, 2.5, 8.6, 50.0}) {
            const auto k = d::make(catenae::window::kaiser{beta}, catenae::span<sample, 257>(table));
            ASSERT_TRUE(k.has_value()) << beta;
            for (std::size_t n = 0; n < 512; ++n) {
                const double t    = 2.0 * double(n) / 512.0 - 1.0;
                const double want = i0(beta * std::sqrt(1.0 - t * t)) / i0(beta);
                ASSERT_NEAR(double((*k)[n]), want, (std::is_same<sample, float>::value ? 6e-8 : 4e-15) * (1.0 + want))
                    << beta << ", " << n;
            }
        }
        for (const double beta :
             {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            EXPECT_EQ(d::make(catenae::window::kaiser{beta}, catenae::span<sample, 257>(table)).error(),
                      catenae::status::invalid_argument)
                << beta;
        }
        EXPECT_EQ(d::make(catenae::window::kaiser{50.5}, catenae::span<sample, 257>(table)).error(),
                  catenae::status::out_of_range);
    }

    // Verifies CAT-WINDOW-002.
    TYPED_TEST(window, FromTableChecksItsValues) {
        using sample = TypeParam;
        using d      = catenae::window_design<sample, 16>;
        std::array<sample, 9> half{};
        for (std::size_t n = 0; n < 9; ++n) {
            half[n] = sample(n) / sample(8);
        }
        const d ok = d::from_table(half); // w[0] = 0 is allowed
        EXPECT_EQ(ok[15], half[1]);
        half[3] = sample(0);
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(half)));
        half[3] = std::numeric_limits<sample>::quiet_NaN();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(half)));
        half[3] = sample(0.375);
        half[0] = sample(-1);
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(half)));
    }

} // namespace
