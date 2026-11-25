/// @file test_convert.cpp
/// @brief convert's numeric contract (include/catenae/convert.h), pinned value by value.
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/convert.h"
#include "catenae_test/harness.h"

namespace {

    using catenae::convert;
    using catenae::q15;
    using catenae::q31;
    using catenae::q4_27;

    constexpr float k_lsb15 = 1.0f / 32768.0f;
    constexpr float k_inf   = std::numeric_limits<float>::infinity();

    // Verifies CAT-CONVERT-002.
    TEST(convert, FloatToFixedRoundsHalfUpAndSaturates) {
        using to15  = convert<float, q15, 1>;
        using to31  = convert<float, q31, 1>;
        using to427 = convert<float, q4_27, 1>;
        EXPECT_EQ(to15::value(0.0f), 0);
        EXPECT_EQ(to15::value(0.5f), 16384);
        EXPECT_EQ(to15::value(-1.0f), -32768);
        EXPECT_EQ(to15::value(1.0f), 32767); // +1.0 is not in the format: saturated
        EXPECT_EQ(to15::value(1.5f), 32767);
        EXPECT_EQ(to15::value(-1.5f), -32768);
        // Half an LSB rounds up, toward +infinity, on both sides of zero.
        for (int k = -100; k < 100; ++k) {
            const float half = (static_cast<float>(k) + 0.5f) * k_lsb15;
            EXPECT_EQ(to15::value(half), k + 1) << k;
            EXPECT_EQ(to15::value(half - k_lsb15 * 0.25f), k) << k;
            EXPECT_EQ(to15::value(half + k_lsb15 * 0.25f), k + 1) << k;
        }
        EXPECT_EQ(to15::value(-0.5f * k_lsb15), 0);
        EXPECT_EQ(to15::value(32767.5f * k_lsb15), 32767); // rounds to 32768, saturated
        // The case v + 0.5f gets wrong: 0.5 - 2^-25 LSB.
        EXPECT_EQ(to15::value((0.5f - 0x1p-25f) * k_lsb15), 0);
        EXPECT_EQ(to31::value(0.25f), 536870912);
        EXPECT_EQ(to31::value(1.0f), 2147483647);
        EXPECT_EQ(to31::value(-1.0f), -2147483647 - 1);
        EXPECT_EQ(to31::value(0x1p-32f), 1); // half an LSB of q31: up
        EXPECT_EQ(to31::value(-0x1p-32f), 0);
        EXPECT_EQ(to427::value(1.0f), 134217728);
        EXPECT_EQ(to427::value(15.5f), 2080374784);
        EXPECT_EQ(to427::value(16.0f), 2147483647); // Q4.27 saturates at +16
        EXPECT_EQ(to427::value(-16.0f), -2147483647 - 1);
        // The largest values below saturation, whose whole part fills 31 bits: the truncation
        // is to 32 bits (a 64-bit one runs in software double on the Cortex-M, D36).
        EXPECT_EQ(to31::value(0x1.fffffep-1f), 2147483520);
        EXPECT_EQ(to31::value(-0x1.fffffep-1f), -2147483520);
        EXPECT_EQ(to427::value(0x1.fffffep3f), 2147483520);
        EXPECT_EQ(to427::value(-0x1.fffffep3f), -2147483520);
    }

    // Verifies CAT-CONVERT-003.
    TEST(convert, NonFiniteInputsHaveAValue) {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ((convert<float, q15, 1>::value(nan)), 0);
        EXPECT_EQ((convert<float, q31, 1>::value(nan)), 0);
        EXPECT_EQ((convert<float, q4_27, 1>::value(nan)), 0);
        EXPECT_EQ((convert<float, q15, 1>::value(k_inf)), 32767);
        EXPECT_EQ((convert<float, q15, 1>::value(-k_inf)), -32768);
        EXPECT_EQ((convert<float, q31, 1>::value(k_inf)), 2147483647);
        EXPECT_EQ((convert<float, q31, 1>::value(-k_inf)), -2147483647 - 1);
    }

    // Verifies CAT-CONVERT-004.
    TEST(convert, FixedToFloatRoundsOnce) {
        using from15  = convert<q15, float, 1>;
        using to15    = convert<float, q15, 1>;
        using from31  = convert<q31, float, 1>;
        using from427 = convert<q4_27, float, 1>;
        for (std::int32_t x = -32768; x <= 32767; ++x) {
            const auto  v = static_cast<std::int16_t>(x);
            const float f = from15::value(v);
            ASSERT_EQ(f, static_cast<float>(x) * k_lsb15) << x; // exact
            ASSERT_EQ(to15::value(f), v) << x;                  // and back
        }
        EXPECT_EQ(from31::value(2147483647), 1.0f); // 2^31 - 1 to nearest: 2^31
        EXPECT_EQ(from31::value(-2147483647 - 1), -1.0f);
        EXPECT_EQ(from31::value(1 << 30), 0.5f);
        EXPECT_EQ(from31::value(0x01000001), 0x1p-7f); // ties to even: 2^24 + 1 rounds to 2^24
        EXPECT_EQ(from31::value(0x01000003), 0x1.000004p-7f);
        EXPECT_EQ(from427::value(134217728), 1.0f);
        EXPECT_EQ(from427::value(-2147483647 - 1), -16.0f);
    }

    // Verifies CAT-CONVERT-005.
    TEST(convert, BetweenFixedFormatsShiftAndRoundHalfUp) {
        using up31     = convert<q15, q31, 1>;
        using down15   = convert<q31, q15, 1>;
        using to427    = convert<q31, q4_27, 1>;
        using from427  = convert<q4_27, q31, 1>;
        using q427to15 = convert<q4_27, q15, 1>;
        using q15to427 = convert<q15, q4_27, 1>;
        for (std::int32_t x = -32768; x <= 32767; ++x) {
            const auto v = static_cast<std::int16_t>(x);
            ASSERT_EQ(up31::value(v), x * 65536) << x;
            ASSERT_EQ(down15::value(x * 65536), v) << x;
            ASSERT_EQ(q15to427::value(v), x * 4096) << x;
            ASSERT_EQ(q427to15::value(x * 4096), v) << x;
        }
        EXPECT_EQ(down15::value(32768), 1); // half a q15 LSB: up
        EXPECT_EQ(down15::value(32767), 0);
        EXPECT_EQ(down15::value(-32768), 0); // -1/2: up, to 0
        EXPECT_EQ(down15::value(-32769), -1);
        EXPECT_EQ(down15::value(0x7FFF8000), 32767); // rounds to 2^15, saturated
        EXPECT_EQ(to427::value(8), 1);
        EXPECT_EQ(to427::value(7), 0);
        EXPECT_EQ(to427::value(2147483647), 134217728);
        EXPECT_EQ(from427::value(134217728), 2147483647); // +1.0: saturated
        EXPECT_EQ(from427::value(-134217728), -2147483647 - 1);
        EXPECT_EQ(from427::value(100000000), 1600000000);
        EXPECT_EQ(q427to15::value(134217728), 32767);
        EXPECT_EQ(q427to15::value(-2147483647 - 1), -32768);
        EXPECT_EQ(q427to15::value(2048), 1); // half a q15 LSB in Q4.27: up
    }

    // Verifies CAT-CONVERT-006.
    TEST(convert, IsAStageAmongFourFormats) {
        constexpr convert<float, q31, 2> c{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(c));
        EXPECT_TRUE(catenae_test::counts_are_exact(c));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(c, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(c));
        convert<float, q31, 2>::state     st{};
        convert<float, q31, 2>::workspace ws{};
        const std::vector<float>          in = {0.25f, -0.5f, 1.0f, -2.0f};
        std::vector<std::int32_t>         out(4);
        EXPECT_EQ(c.run(st, ws, catenae::span<const float>(in), catenae::span<std::int32_t>(out)), 2u);
        EXPECT_EQ(out[0], 536870912);
        EXPECT_EQ(out[1], -1073741824);
        EXPECT_EQ(out[2], 2147483647);
        EXPECT_EQ(out[3], -2147483647 - 1);
        static_assert(!catenae::stage_traits<convert<q31, q4_27, 1>>::is_in_place, "two ports, never in place");
        // Other formats, and a format to itself, do not compile: tests/compile_fail/convert_*.cpp.
    }

} // namespace
