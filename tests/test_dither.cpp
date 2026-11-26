/// @file test_dither.cpp
/// @brief dither's contract (include/catenae/dither.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/convert.h"
#include "catenae/dither.h"
#include "catenae_test/harness.h"

namespace {

    /// Fewer samples under an emulator, where 2^20 would take minutes and more memory than a
    /// Cortex-M board has; the tolerances below are stated for the count used.
#if defined(CATENAE_TEST_EMULATED)
    constexpr std::size_t k_samples = std::size_t{1} << 14;
#else
    constexpr std::size_t k_samples = std::size_t{1} << 20;
#endif

    /// `n` frames of `x` on every channel through a dither from reset, as values in LSBs at Bits.
    template <typename D>
    std::vector<double> noise_in_lsb(const D& d, float x, std::size_t n, std::size_t channel, double lsb) {
        constexpr std::size_t ch = D::input_port::channels;
        typename D::state     st{};
        typename D::workspace ws{};
        catenae::stage_traits<D>::reset(d, st);
        std::vector<float> buffer(n * ch, x);
        static_cast<void>(d.run(st, ws, catenae::span<const float>(buffer), catenae::span<float>(buffer)));
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = (static_cast<double>(buffer[i * ch + channel]) - static_cast<double>(x)) / lsb;
        }
        return out;
    }

    double mean(const std::vector<double>& v) {
        double sum = 0.0;
        for (const double x : v) {
            sum += x;
        }
        return sum / static_cast<double>(v.size());
    }
    double variance(const std::vector<double>& v) {
        const double m   = mean(v);
        double       sum = 0.0;
        for (const double x : v) {
            sum += (x - m) * (x - m);
        }
        return sum / static_cast<double>(v.size());
    }

    constexpr double k_lsb16 = 1.0 / 32768.0; // an LSB at 16 bits of full scale

    // Verifies CAT-DITHER-001.
    TEST(dither, TheNoiseIsTriangularOverOneLsb) {
        const catenae::dither<2, 16> d{};
        const std::vector<double>    e = noise_in_lsb(d, 0.0f, k_samples, 0, k_lsb16);
        for (const double v : e) {
            ASSERT_GT(v, -1.0);
            ASSERT_LT(v, 1.0);
        }
        // Mean 0 and variance 1/6, within four standard errors (the variance of a sample
        // variance of TPDF is (m4 - s^4) / n, with m4 = 1/15).
        const double n = static_cast<double>(k_samples);
        EXPECT_NEAR(mean(e), 0.0, 4.0 * std::sqrt((1.0 / 6.0) / n));
        EXPECT_NEAR(variance(e), 1.0 / 6.0, 4.0 * std::sqrt((1.0 / 15.0 - 1.0 / 36.0) / n));
        // The histogram, in eight bins of a quarter LSB: each bin's count is the triangle's
        // area times n, within five standard deviations of a binomial count.
        std::array<double, 8> counts{};
        for (const double v : e) {
            counts[static_cast<std::size_t>((v + 1.0) * 4.0)] += 1.0;
        }
        for (std::size_t k = 0; k < 8; ++k) {
            const double lo  = -1.0 + 0.25 * static_cast<double>(k);
            const double hi  = lo + 0.25;
            const auto   cdf = [](double x) {
                return x < 0.0 ? 0.5 * (1.0 + x) * (1.0 + x) : 1.0 - 0.5 * (1.0 - x) * (1.0 - x);
            };
            const double p    = cdf(hi) - cdf(lo);
            const double owed = p * n;
            EXPECT_NEAR(counts[k], owed, 5.0 * std::sqrt(owed * (1.0 - p))) << "bin " << k;
        }
    }

    /// The mean and the error's variance, in LSBs at 16 bits, of `x` dithered and quantized to
    /// q15 by convert, which rounds half up.
    std::array<double, 2> quantized(float x, bool dithered) {
        const catenae::dither<1, 16>                   d{};
        const catenae::convert<float, catenae::q15, 1> q{};
        catenae::dither<1, 16>::state                  st{};
        catenae::dither<1, 16>::workspace              ws{};
        catenae::stage_traits<catenae::dither<1, 16>>::reset(d, st);
        std::vector<float> buffer(k_samples, x);
        if (dithered) {
            static_cast<void>(d.run(st, ws, catenae::span<const float>(buffer), catenae::span<float>(buffer)));
        }
        std::vector<double> error(k_samples);
        double              sum = 0.0;
        for (std::size_t i = 0; i < k_samples; ++i) {
            const double out = static_cast<double>(q.value(buffer[i]));
            sum += out;
            error[i] = out - static_cast<double>(x) / k_lsb16;
        }
        return {sum / static_cast<double>(k_samples), variance(error)};
    }

    // Verifies CAT-DITHER-002.
    TEST(dither, AQuantizedMeanFollowsTheInputBelowAnLsb) {
        const double n = static_cast<double>(k_samples);
        EXPECT_EQ(quantized(static_cast<float>(0.3 * k_lsb16), false)[0], 0.0); // without dither: lost
        for (const double at : {0.0, 0.25, 0.3, 0.5, 0.75, 1.5, -2.7}) {
            const std::array<double, 2> r = quantized(static_cast<float>(at * k_lsb16), true);
            // Rounding half up after TPDF: the mean is the input plus half an LSB less half an
            // LSB, the input itself; the error's variance is 1/6 + 1/12 = 1/4 at every input.
            EXPECT_NEAR(r[0], at, 4.0 * std::sqrt(0.25 / n)) << at;
            EXPECT_NEAR(r[1], 0.25, 0.02) << at;
        }
    }

    // Verifies CAT-DITHER-003.
    TEST(dither, TheNoiseIsTheSeedsAlone) {
        const catenae::dither<2, 16> d{};
        const catenae::dither<2, 16> other{12345u};
        static_assert(catenae::dither<2, 16>::default_seed == 0x2545F491u, "the default seed");
        EXPECT_EQ(d.seed(), 0x2545F491u);
        EXPECT_EQ(other.seed(), 12345u);
        const std::vector<double> once  = noise_in_lsb(d, 0.0f, 4096, 0, k_lsb16);
        const std::vector<double> again = noise_in_lsb(d, 0.0f, 4096, 0, k_lsb16);
        EXPECT_EQ(once, again); // after every reset, the same sequence
        EXPECT_NE(noise_in_lsb(other, 0.0f, 4096, 0, k_lsb16), once);
        // The two channels' generators are uncorrelated: |r| within four standard errors of 0.
        const std::vector<double> left  = noise_in_lsb(d, 0.0f, k_samples, 0, k_lsb16);
        const std::vector<double> right = noise_in_lsb(d, 0.0f, k_samples, 1, k_lsb16);
        double                    cross = 0.0;
        for (std::size_t i = 0; i < k_samples; ++i) {
            cross += left[i] * right[i];
        }
        const double r = cross / static_cast<double>(k_samples) / (1.0 / 6.0);
        EXPECT_LT(std::fabs(r), 4.0 / std::sqrt(static_cast<double>(k_samples)));
        // The generic tests, chunking among them.
        EXPECT_TRUE(catenae_test::reset_ignores_storage(d));
        EXPECT_TRUE(catenae_test::counts_are_exact(d));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(d, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(d));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(catenae::dither_f64<3, 24>{}));
    }

    // Verifies CAT-DITHER-004.
    TEST(dither, InAFixedFormatTheNoiseIsWholeUnitsAndTheSumSaturates) {
        using d_t = catenae::dither_q31<1, 24>; // an LSB at 24 bits is 2^8 raw units
        const d_t      d{};
        d_t::state     st{};
        d_t::workspace ws{};
        catenae::stage_traits<d_t>::reset(d, st);
        // 2^20 samples on every build, in blocks of 4096 and integer work only: each end is
        // reached about 16 times. The first block's squares give the variance.
        std::vector<std::int32_t> block(4096);
        std::int32_t              lo = 0;
        std::int32_t              hi = 0;
        double                    sq = 0.0;
        for (std::size_t n = 0; n < 256; ++n) {
            std::fill(block.begin(), block.end(), 0);
            static_cast<void>(
                d.run(st, ws, catenae::span<const std::int32_t>(block), catenae::span<std::int32_t>(block)));
            for (const std::int32_t v : block) {
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
                if (n == 0) {
                    sq += static_cast<double>(v) * static_cast<double>(v);
                }
            }
        }
        EXPECT_EQ(lo, -255); // (-256, 256): every value within, both ends reached
        EXPECT_EQ(hi, 255);
        // Over 4096 samples the mean square (in LSB^2) has a standard error of about 0.003: 4 of them.
        EXPECT_NEAR(sq / 4096.0 / (256.0 * 256.0), 1.0 / 6.0, 0.012);
        // At the format's limits the sum saturates.
        std::vector<std::int32_t> top(4096, std::numeric_limits<std::int32_t>::max());
        std::vector<std::int32_t> bottom(4096, std::numeric_limits<std::int32_t>::min());
        static_cast<void>(d.run(st, ws, catenae::span<const std::int32_t>(top), catenae::span<std::int32_t>(top)));
        static_cast<void>(
            d.run(st, ws, catenae::span<const std::int32_t>(bottom), catenae::span<std::int32_t>(bottom)));
        for (std::size_t i = 0; i < top.size(); ++i) {
            ASSERT_GE(top[i], std::numeric_limits<std::int32_t>::max() - 255);
            ASSERT_LE(bottom[i], std::numeric_limits<std::int32_t>::min() + 255);
        }
        // Q4.27 takes its LSB from its own F: at 24 bits, 2^4 raw units.
        using q4_t = catenae::dither_q4_27<1, 24>;
        const q4_t      q4{};
        q4_t::state     s4{};
        q4_t::workspace w4{};
        catenae::stage_traits<q4_t>::reset(q4, s4);
        std::vector<std::int32_t> zeros(4096, 0);
        static_cast<void>(q4.run(s4, w4, catenae::span<const std::int32_t>(zeros), catenae::span<std::int32_t>(zeros)));
        for (const std::int32_t v : zeros) {
            ASSERT_GT(v, -16);
            ASSERT_LT(v, 16);
        }
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(d));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(d));
    }

} // namespace
