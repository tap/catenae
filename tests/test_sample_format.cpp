/// @file test_sample_format.cpp
/// @brief The sample formats' contract (include/catenae/sample_format.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/sample_format.h"

namespace {

    using catenae::fixed;
    using catenae::format_traits;
    using catenae::is_sample_format;

    // Verifies CAT-FORMAT-001.
    TEST(sample_format, FormatsAreFloatDoubleAndFixedTags) {
        static_assert(is_sample_format<float>::value && is_sample_format<double>::value, "the floating formats");
        static_assert(is_sample_format<catenae::q15>::value && is_sample_format<catenae::q31>::value,
                      "the named fixed formats");
        static_assert(is_sample_format<catenae::q4_27>::value, "a format with integer bits");
        static_assert(is_sample_format<fixed<std::int16_t, 0>>::value
                          && is_sample_format<fixed<std::int32_t, 0>>::value,
                      "no fraction bits at all");
        static_assert(!is_sample_format<std::int16_t>::value && !is_sample_format<std::int32_t>::value,
                      "a raw integer is storage, not a format");
        static_assert(!is_sample_format<long double>::value && !is_sample_format<std::int8_t>::value,
                      "nothing else is a format");
        static_assert(!is_sample_format<fixed<std::int16_t, 16>>::value
                          && !is_sample_format<fixed<std::int32_t, 32>>::value,
                      "no more fraction bits than the storage holds below its sign");
        static_assert(!is_sample_format<fixed<std::int16_t, -1>>::value, "no negative fraction bits");
        SUCCEED();
    }

    // Verifies CAT-FORMAT-002.
    TEST(sample_format, FixedFormatsShareTheirStoragesKernel) {
        static_assert(std::is_same<catenae::storage_t<catenae::q15>, std::int16_t>::value, "q15 is stored in 16 bits");
        static_assert(std::is_same<catenae::storage_t<catenae::q31>, std::int32_t>::value, "q31 is stored in 32 bits");
        static_assert(std::is_same<catenae::storage_t<catenae::q4_27>, std::int32_t>::value, "q4_27 in 32 bits");
        static_assert(std::is_same<catenae::storage_t<float>, float>::value, "float is its own storage");
        static_assert(std::is_same<format_traits<catenae::q4_27>::kernel, catenae::sample_traits<std::int32_t>>::value,
                      "every fixed<int32_t, F> runs the Q31 kernel ladder");
        static_assert(
            std::is_same<format_traits<fixed<std::int16_t, 8>>::kernel, catenae::sample_traits<std::int16_t>>::value,
            "every fixed<int16_t, F> runs the Q15 kernel ladder");
        EXPECT_EQ(format_traits<catenae::q15>::fraction_bits, 15);
        EXPECT_EQ(format_traits<catenae::q15>::integer_bits, 0);
        EXPECT_EQ(format_traits<catenae::q31>::fraction_bits, 31);
        EXPECT_EQ(format_traits<catenae::q31>::integer_bits, 0);
        EXPECT_EQ(format_traits<catenae::q4_27>::fraction_bits, 27);
        EXPECT_EQ(format_traits<catenae::q4_27>::integer_bits, 4);
        EXPECT_EQ((format_traits<fixed<std::int16_t, 8>>::integer_bits), 7);
        EXPECT_TRUE(format_traits<catenae::q15>::is_fixed_point);
        EXPECT_FALSE(format_traits<double>::is_fixed_point);
        EXPECT_EQ(catenae::q4_27::fraction_bits, 27);
    }

    // Verifies CAT-FORMAT-003.
    TEST(sample_format, ComplexIsTwoPartsWithNoPadding) {
        using cf = catenae::complex<float>;
        using cq = catenae::complex<std::int16_t>;
        static_assert(std::is_standard_layout<cf>::value && std::is_trivially_copyable<cf>::value, "plain storage");
        static_assert(std::is_aggregate<cf>::value && std::is_trivially_default_constructible<cf>::value,
                      "an aggregate with no constructor");
        static_assert(sizeof(cf) == 2 * sizeof(float) && sizeof(cq) == 2 * sizeof(std::int16_t), "no padding");
        static_assert(offsetof(cf, re) == 0 && offsetof(cf, im) == sizeof(float), "re, then im");
        constexpr cf c{1.0f, -2.0f};
        static_assert(c.re > 0.0f && c.im < 0.0f, "a constant expression");
        SUCCEED();
    }

} // namespace
