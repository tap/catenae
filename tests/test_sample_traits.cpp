/// @file test_sample_traits.cpp
/// @brief The kernels' sample arithmetic (include/catenae/sample_traits.h).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Ported from DspTap's tests/test_sample_traits.cpp at b26a7db (NOTICE.md), its constants
// renamed for D30 and its float accumulator changed for D36: catenae's float profile
// accumulates in float, where DspTap's accumulates in double. Every number is a contract
// point (Q formats, rounding mode, saturation, accumulator pre-shift).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "catenae/sample_traits.h"

namespace {

    using q15 = catenae::sample_traits<std::int16_t>;
    using q31 = catenae::sample_traits<std::int32_t>;
    using f32 = catenae::sample_traits<float>;
    using f64 = catenae::sample_traits<double>;

    // Verifies CAT-TRAITS-001.
    TEST(sample_traits, CoefficientConversionRoundsAndSaturates) {
        EXPECT_EQ(q15::make_coeff(0.0), 0);
        EXPECT_EQ(q15::make_coeff(1.0), 16384); // Q1.14
        EXPECT_EQ(q15::make_coeff(-1.0), -16384);
        EXPECT_EQ(q15::make_coeff(10.0), 32767); // saturates
        EXPECT_EQ(q15::make_coeff(-10.0), -32768);
        EXPECT_EQ(q31::make_coeff(1.0), 1073741824);   // Q1.30
        EXPECT_EQ(q31::make_coeff(10.0), 2147483647);  // saturates
        EXPECT_FLOAT_EQ(f32::make_coeff(0.25), 0.25f); // unity scale, plain cast
        EXPECT_EQ(f64::make_coeff(0.1), 0.1);          // identity
    }

    // Verifies CAT-TRAITS-001.
    TEST(sample_traits, CoefficientScaleIsTheFormatsUnity) {
        static_assert(q15::coeff_frac_bits == 14, "Q1.14");
        static_assert(q31::coeff_frac_bits == 30, "Q1.30");
        EXPECT_DOUBLE_EQ(q15::coeff_scale, 16384.0);
        EXPECT_DOUBLE_EQ(q31::coeff_scale, 1073741824.0);
        EXPECT_DOUBLE_EQ(static_cast<double>(q15::make_coeff(1.0)), q15::coeff_scale);
        EXPECT_DOUBLE_EQ(static_cast<double>(q31::make_coeff(1.0)), q31::coeff_scale);
        EXPECT_DOUBLE_EQ(f32::coeff_scale, 1.0);
        EXPECT_DOUBLE_EQ(f64::coeff_scale, 1.0);
    }

    // Verifies CAT-TRAITS-002.
    TEST(sample_traits, QLadderClosesFromTheNamedConstants) {
        static_assert(q15::sample_frac_bits == 15 && q15::accum_pre_shift == 0, "Q0.15 samples, no pre-shift");
        static_assert(q15::accum_frac_bits == 29 && q15::finalize_shift == 14, "Q29, to Q15 by 14 bits");
        static_assert(q31::sample_frac_bits == 31 && q31::accum_pre_shift == 16, "Q0.31 samples, 16-bit pre-shift");
        static_assert(q31::accum_frac_bits == 45 && q31::finalize_shift == 14, "Q45, to Q31 by 14 bits");
        static_assert(q15::is_fixed_point && q31::is_fixed_point, "the integer profiles");
        static_assert(!f32::is_fixed_point && !f64::is_fixed_point, "the floating profiles");
        SUCCEED();
    }

    // Verifies CAT-TRAITS-001.
    TEST(sample_traits, RoundSatRoundsHalfAwayFromZero) {
        using catenae::detail::round_sat;
        EXPECT_EQ(round_sat<std::int16_t>(0.5), 1);
        EXPECT_EQ(round_sat<std::int16_t>(-0.5), -1);
        EXPECT_EQ(round_sat<std::int16_t>(0.49), 0);
        EXPECT_EQ(round_sat<std::int16_t>(-0.49), 0);
        EXPECT_EQ(round_sat<std::int16_t>(1e9), std::numeric_limits<std::int16_t>::max());
        EXPECT_EQ(round_sat<std::int16_t>(-1e9), std::numeric_limits<std::int16_t>::min());
    }

    // Verifies CAT-TRAITS-002.
    TEST(sample_traits, FinalizeSaturates) {
        EXPECT_EQ(q15::finalize(std::int64_t{1} << 40), 32767);
        EXPECT_EQ(q15::finalize(-(std::int64_t{1} << 40)), -32768);
        EXPECT_EQ(q31::finalize(std::int64_t{1} << 60), 2147483647);
        EXPECT_EQ(q31::finalize(-(std::int64_t{1} << 60)), -2147483648LL);
    }

    // Verifies CAT-TRAITS-002: Q29 to Q15 and Q45 to Q31 both add 2^13 and shift 14, so exactly
    // half rounds up and the negative side rounds half up too (-0.5 LSB lands on 0).
    TEST(sample_traits, FinalizeRoundsHalfUp) {
        EXPECT_EQ(q15::finalize((std::int64_t{1} << 13)), 1);
        EXPECT_EQ(q15::finalize((std::int64_t{1} << 13) - 1), 0);
        EXPECT_EQ(q15::finalize(-(std::int64_t{1} << 13)), 0);
        EXPECT_EQ(q15::finalize(-(std::int64_t{1} << 13) - 1), -1);
        EXPECT_EQ(q31::finalize((std::int64_t{1} << 13)), 1);
        EXPECT_EQ(q31::finalize((std::int64_t{1} << 13) - 1), 0);
        EXPECT_EQ(q31::finalize(-(std::int64_t{1} << 13)), 0);
        EXPECT_EQ(q31::finalize(-(std::int64_t{1} << 13) - 1), -1);
    }

    // Verifies CAT-TRAITS-002.
    TEST(sample_traits, Q15MacIsExactInInt64) {
        const auto acc = q15::mac(0, std::int16_t{32767}, q15::make_coeff(1.0));
        EXPECT_EQ(acc, std::int64_t{32767} * 16384);
        EXPECT_EQ(q15::finalize(acc), 32767);
    }

    // Verifies CAT-TRAITS-002: Q0.31 x Q1.30 = Q61 would overflow int64 after about 48
    // accumulations, so each product drops exactly 16 bits before the add.
    TEST(sample_traits, Q31MacPreShiftsSixteenBits) {
        const auto acc = q31::mac(0, std::int32_t{1} << 30, std::int32_t{1} << 30);
        EXPECT_EQ(acc, std::int64_t{1} << 44);
        std::int64_t big = 0;
        for (int i = 0; i < 80; ++i) {
            big = q31::mac(big, std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max());
        }
        EXPECT_GT(big, 0); // no wrap
        EXPECT_EQ(q31::finalize(big), std::numeric_limits<std::int32_t>::max());
    }

    // Verifies CAT-TRAITS-003: float accumulates in float (D36), so a contribution below
    // float's resolution is lost, as it is in the float kernel it models.
    TEST(sample_traits, FloatMacAccumulatesInFloat) {
        static_assert(std::is_same<f32::accum, float>::value && std::is_same<f32::coeff, float>::value,
                      "the float profile is float throughout");
        const float acc = f32::mac(f32::mac(0.0f, 1.0f, 1.0f), 0x1p-15f, 0x1p-15f);
        EXPECT_EQ(acc, 1.0f); // exactly: 2^-30 is below float's resolution at 1
        EXPECT_FLOAT_EQ(f32::mac(0.5f, 0.5f, 0.5f), 0.75f);
        EXPECT_FLOAT_EQ(f32::finalize(1e30f), 1e30f); // no saturation
    }

    // Verifies CAT-TRAITS-003.
    TEST(sample_traits, DoubleIsTheIdentityDatapath) {
        static_assert(std::is_same<f64::coeff, double>::value && std::is_same<f64::accum, double>::value,
                      "the golden model is double throughout");
        EXPECT_EQ(f64::make_coeff(1e300), 1e300);
        EXPECT_EQ(f64::finalize(1e300), 1e300);
        EXPECT_EQ(f64::mac(1.0, 0x1p-40, 1.0), 1.0 + 0x1p-40);
        EXPECT_EQ(f64::silence(), 0.0);
    }

    // Verifies CAT-TRAITS-005.
    TEST(sample_traits, SilenceIsZero) {
        EXPECT_EQ(q15::silence(), 0);
        EXPECT_EQ(q31::silence(), 0);
        EXPECT_FLOAT_EQ(f32::silence(), 0.0f);
        EXPECT_DOUBLE_EQ(f64::silence(), 0.0);
    }

    // The kernels' requirement set: the four storage types meet it, and nothing else does.
    static_assert(catenae::is_kernel_sample<double>::value && catenae::is_kernel_sample<float>::value,
                  "the floating storage types");
    static_assert(catenae::is_kernel_sample<std::int16_t>::value && catenae::is_kernel_sample<std::int32_t>::value,
                  "the fixed-point storage types");
    static_assert(!catenae::is_kernel_sample<std::int8_t>::value && !catenae::is_kernel_sample<long double>::value,
                  "no other type");

    // Every operation is constexpr.
    static_assert(q15::mac(q15::accum{}, std::int16_t{-32768}, std::int16_t{-16384}) == std::int64_t{32768} * 16384,
                  "the Q15 product at compile time");
    static_assert(q31::finalize(std::int64_t{1} << 13) == 1, "the Q31 rounding at compile time");

    // ------------------------------------------------------------------
    // finalize_divided<S, D>: finalize() of acc / D under one rounding.

    /// The exact round-half-up quotient floor((acc + D 2^(f-1)) / (D 2^f)).
    std::int64_t exact_quotient(std::int64_t acc, std::int64_t divisor, int f) {
        const std::int64_t den = divisor << f;
        const std::int64_t num = acc + den / 2;
        std::int64_t       q   = num / den;
        if (num % den != 0 && num < 0) {
            --q;
        }
        return q;
    }

    template <std::uint32_t D>
    void expect_q15_multiples_exact() {
        for (std::int32_t x = -32768; x <= 32767; ++x) {
            const std::int64_t acc = static_cast<std::int64_t>(D) * (std::int64_t{1} << q15::finalize_shift) * x;
            ASSERT_EQ((catenae::finalize_divided<std::int16_t, D>(acc)), x) << "D=" << D;
        }
    }

    template <std::uint32_t D>
    void expect_q31_multiples_exact() {
        for (const std::int64_t x : {std::int64_t{-2147483648LL}, std::int64_t{-1}, std::int64_t{0}, std::int64_t{1},
                                     std::int64_t{123456789}, std::int64_t{2147483647}}) {
            const std::int64_t acc = static_cast<std::int64_t>(D) * (std::int64_t{1} << q31::finalize_shift) * x;
            EXPECT_EQ((catenae::finalize_divided<std::int32_t, D>(acc)), x) << "D=" << D;
        }
    }

    // Verifies CAT-TRAITS-004: every exact multiple of D 2^f (the accumulator of DC through
    // rows summing to D x unity) comes out exact: all 65536 Q15 values for each divisor of the
    // family's vocabulary and two odd ones, a sample of Q31.
    TEST(sample_traits, FinalizeDividedIsExactAtEveryMultipleOfTheDivisor) {
        expect_q15_multiples_exact<1>();
        expect_q15_multiples_exact<2>();
        expect_q15_multiples_exact<3>();
        expect_q15_multiples_exact<4>();
        expect_q15_multiples_exact<5>();
        expect_q15_multiples_exact<6>();
        expect_q15_multiples_exact<7>();
        expect_q15_multiples_exact<8>();
        expect_q31_multiples_exact<1>();
        expect_q31_multiples_exact<3>();
        expect_q31_multiples_exact<6>();
        expect_q31_multiples_exact<8>();
    }

    // D = 1 is finalize(); a power of two is finalize's round-half-up with the shift widened
    // (exact), Q31's other divisors the exact quotient, and Q15's multiply-back the exact
    // quotient except within D 2^-11 of a rounding boundary, where it may land one step away.
    template <std::uint32_t D>
    void expect_q15_quotient(std::uint64_t seed) {
        constexpr int f = q15::finalize_shift;
        for (int i = 0; i < 20000; ++i) {
            seed                     = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto         acc   = static_cast<std::int64_t>(seed >> 30) - (std::int64_t{1} << 33);
            const std::int64_t exact = exact_quotient(acc, D, f);
            const auto         got   = static_cast<std::int64_t>(catenae::finalize_divided<std::int16_t, D>(acc));
            const std::int64_t clamp = std::min<std::int64_t>(32767, std::max<std::int64_t>(-32768, exact));
            if constexpr ((D & (D - 1)) == 0) {
                ASSERT_EQ(got, clamp) << "D=" << D << " acc=" << acc;
            }
            else {
                const double quotient = static_cast<double>(acc) / (static_cast<double>(D) * (1 << f));
                const double frac     = quotient - std::floor(quotient);
                if (std::abs(frac - 0.5) > static_cast<double>(D) / 2048.0) {
                    ASSERT_EQ(got, clamp) << "D=" << D << " acc=" << acc;
                }
                else {
                    ASSERT_LE(std::abs(got - clamp), 1) << "D=" << D << " acc=" << acc;
                }
            }
        }
    }

    // Verifies CAT-TRAITS-004.
    TEST(sample_traits, FinalizeDividedRoundsTheQuotientHalfUp) {
        for (const std::int64_t acc : {std::int64_t{0}, std::int64_t{8191}, std::int64_t{8192}, std::int64_t{-8192},
                                       std::int64_t{-8193}, std::int64_t{1} << 40, -(std::int64_t{1} << 40)}) {
            EXPECT_EQ((catenae::finalize_divided<std::int16_t, 1>(acc)), q15::finalize(acc)) << acc;
            EXPECT_EQ((catenae::finalize_divided<std::int32_t, 1>(acc)), q31::finalize(acc)) << acc;
            EXPECT_DOUBLE_EQ((catenae::finalize_divided<double, 1>(static_cast<double>(acc))),
                             f64::finalize(static_cast<double>(acc)));
        }
        expect_q15_quotient<2>(1);
        expect_q15_quotient<3>(2);
        expect_q15_quotient<6>(3);
        expect_q15_quotient<8>(4);
        std::uint64_t seed = 5;
        for (int i = 0; i < 20000; ++i) {
            seed           = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto acc = static_cast<std::int64_t>(seed >> 14) - (std::int64_t{1} << 49);
            const std::pair<std::int64_t, std::int64_t> cases[] = {
                {static_cast<std::int64_t>(catenae::finalize_divided<std::int32_t, 3>(acc)), 3},
                {static_cast<std::int64_t>(catenae::finalize_divided<std::int32_t, 6>(acc)), 6},
                {static_cast<std::int64_t>(catenae::finalize_divided<std::int32_t, 8>(acc)), 8}};
            for (const auto& c : cases) {
                const std::int64_t exact = exact_quotient(acc, c.second, q31::finalize_shift);
                ASSERT_EQ(c.first, std::min<std::int64_t>(2147483647, std::max<std::int64_t>(-2147483648LL, exact)))
                    << "D=" << c.second << " acc=" << acc;
            }
        }
        // Floating: finalize of the quotient, in the float accumulator for float (D36).
        EXPECT_FLOAT_EQ((catenae::finalize_divided<float, 3>(1.0f)), 1.0f / 3.0f);
        EXPECT_DOUBLE_EQ((catenae::finalize_divided<double, 6>(-3.0)), -0.5);
    }

    // Verifies CAT-TRAITS-004.
    TEST(sample_traits, FinalizeDividedSaturates) {
        const std::int64_t big = std::int64_t{1} << 36;
        EXPECT_EQ((catenae::finalize_divided<std::int16_t, 3>(big)), 32767);
        EXPECT_EQ((catenae::finalize_divided<std::int16_t, 3>(-big)), -32768);
        EXPECT_EQ((catenae::finalize_divided<std::int16_t, 8>(big)), 32767);
        EXPECT_EQ((catenae::finalize_divided<std::int32_t, 6>(std::int64_t{1} << 62)), 2147483647);
        EXPECT_EQ((catenae::finalize_divided<std::int32_t, 6>(-(std::int64_t{1} << 62))), -2147483648LL);
    }

} // namespace
