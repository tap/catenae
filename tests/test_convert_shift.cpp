/// @file test_convert_shift.cpp
/// @brief The integer helpers' contract (catenae/detail/convert.h and catenae/detail/shift.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "catenae/detail/convert.h"
#include "catenae/detail/shift.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::detail::narrow;
    using catenae::detail::shift_left;
    using catenae::detail::shift_right;

    /// floor(v / 2^bits) by division, which C++ defines as truncation toward zero.
    std::int64_t floor_divided(std::int64_t v, int bits) {
        const std::int64_t d = std::int64_t{1} << bits;
        std::int64_t       q = v / d;
        if (v % d != 0 && v < 0) {
            --q;
        }
        return q;
    }

    // Verifies CAT-CONVERT-001.
    TEST(convert, NarrowKeepsEveryValueTheTargetHolds) {
        for (std::int32_t v = -32768; v <= 32767; ++v) {
            ASSERT_EQ(narrow<std::int16_t>(v), static_cast<std::int16_t>(v));
        }
        EXPECT_EQ(narrow<std::uint32_t>(std::int64_t{4294967295}), 4294967295u);
        EXPECT_EQ(narrow<std::int32_t>(std::uint64_t{2147483647}), 2147483647);
        EXPECT_EQ(narrow<std::size_t>(std::uint32_t{7}), std::size_t{7});
        static_assert(narrow<std::int16_t>(-32768) == -32768, "narrow is a constant expression");
        static_assert(narrow<std::uint8_t>(255u) == 255u, "at either end of the target's range");
        if constexpr (catenae::detail::k_audit_level) {
            CATENAE_EXPECT_CHECK(catenae::check::invariant, static_cast<void>(narrow<std::int16_t>(32768)));
            CATENAE_EXPECT_CHECK(catenae::check::invariant, static_cast<void>(narrow<std::uint32_t>(-1)));
            CATENAE_EXPECT_CHECK(catenae::check::invariant,
                                 static_cast<void>(narrow<std::int32_t>(std::uint32_t{2147483648u})));
            CATENAE_EXPECT_CHECK(catenae::check::invariant,
                                 static_cast<void>(narrow<std::uint8_t>(std::int64_t{-256})));
        }
        else {
            CATENAE_EXPECT_NO_CHECK(static_cast<void>(narrow<std::int16_t>(32768)));
        }
    }

    // Verifies CAT-SHIFT-001.
    TEST(shift, RightIsFloorDivisionForEveryValue) {
        for (std::int32_t v = -32768; v <= 32767; ++v) {
            for (int bits = 0; bits <= 14; ++bits) {
                ASSERT_EQ(shift_right(static_cast<std::int16_t>(v), bits), floor_divided(v, bits))
                    << v << " >> " << bits;
            }
        }
        for (const std::int64_t v :
             {std::numeric_limits<std::int64_t>::min(), std::int64_t{-(std::int64_t{1} << 40) - 1}, std::int64_t{-3},
              std::int64_t{-1}, std::int64_t{0}, std::int64_t{5}, std::numeric_limits<std::int64_t>::max()}) {
            for (int bits = 0; bits <= 62; ++bits) {
                ASSERT_EQ(shift_right(v, bits), floor_divided(v, bits)) << v << " >> " << bits;
            }
        }
        EXPECT_EQ(shift_right(std::numeric_limits<std::int32_t>::min(), 30), -2);
        static_assert(shift_right(-1, 1) == -1 && shift_right(-3, 1) == -2 && shift_right(3, 1) == 1,
                      "shift_right floors, at compile time too");
    }

    // Verifies CAT-SHIFT-001.
    TEST(shift, LeftIsMultiplicationByAPowerOfTwo) {
        for (std::int32_t v = -256; v <= 255; ++v) {
            for (int bits = 0; bits <= 7; ++bits) {
                ASSERT_EQ(shift_left(static_cast<std::int16_t>(v), bits), v * (1 << bits)) << v << " << " << bits;
            }
        }
        EXPECT_EQ(shift_left(std::int64_t{-1}, 62), std::numeric_limits<std::int64_t>::min() / 2);
        EXPECT_EQ(shift_left(std::int32_t{-3}, 29), -1610612736);
        static_assert(shift_left(-1, 4) == -16 && shift_left(std::int64_t{1}, 40) == 1099511627776,
                      "shift_left is a constant expression, for a negative value too");
    }

} // namespace
