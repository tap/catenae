/// @file test_sanitize.cpp
/// @brief sanitize's contract (include/catenae/sanitize.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/sanitize.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T, typename B>
    T of_bits(B bits) {
        T value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }
    template <typename B, typename T>
    B bits_of(T value) {
        B bits;
        std::memcpy(&bits, &value, sizeof bits);
        return bits;
    }

    /// Whether sanitize gives +0 (`zero`) or the value itself, bit for bit, for one bit pattern.
    template <typename T, typename B>
    ::testing::AssertionResult gives(B bits, bool zero) {
        const T x    = of_bits<T>(bits);
        const T out  = catenae::basic_sanitize<T, 1>::value(x);
        const B got  = bits_of<B>(out);
        const B owed = zero ? B{0} : bits;
        if (got != owed) {
            return ::testing::AssertionFailure() << std::hex << "bits " << bits << " gave " << got;
        }
        return ::testing::AssertionSuccess();
    }

    // Verifies CAT-SANITIZE-001: in float, every exponent with a spread of mantissas and both
    // signs; the special classes at every mantissa bit.
    TEST(sanitize, FloatKeepsNormalsAndZerosAndZeroesTheRest) {
        for (std::uint32_t sign : {0u, 0x80000000u}) {
            for (std::uint32_t exponent = 0; exponent <= 0xFFu; ++exponent) {
                for (const std::uint32_t mantissa : {0u, 1u, 0x2AAAAAu, 0x400000u, 0x7FFFFFu}) {
                    const std::uint32_t bits    = sign | (exponent << 23) | mantissa;
                    const bool          special = exponent == 0xFFu;              // NaN, infinity
                    const bool          tiny    = exponent == 0 && mantissa != 0; // subnormal
                    ASSERT_TRUE((gives<float, std::uint32_t>(bits, special || tiny)));
                }
            }
            for (unsigned bit = 0; bit < 23; ++bit) {
                ASSERT_TRUE((gives<float, std::uint32_t>(sign | 0x7F800000u | (1u << bit), true))); // a NaN
                ASSERT_TRUE((gives<float, std::uint32_t>(sign | (1u << bit), true)));               // a subnormal
            }
        }
        EXPECT_EQ(catenae::sanitize<1>::value(std::numeric_limits<float>::quiet_NaN()), 0.0f);
        EXPECT_EQ(catenae::sanitize<1>::value(std::numeric_limits<float>::signaling_NaN()), 0.0f);
        EXPECT_EQ(catenae::sanitize<1>::value(-std::numeric_limits<float>::infinity()), 0.0f);
        EXPECT_EQ(catenae::sanitize<1>::value(std::numeric_limits<float>::denorm_min()), 0.0f);
        EXPECT_EQ(catenae::sanitize<1>::value(std::numeric_limits<float>::min()), std::numeric_limits<float>::min());
        EXPECT_EQ(catenae::sanitize<1>::value(-std::numeric_limits<float>::max()), -std::numeric_limits<float>::max());
    }

    // Verifies CAT-SANITIZE-001, in double.
    TEST(sanitize, DoubleKeepsNormalsAndZerosAndZeroesTheRest) {
        for (std::uint64_t sign : {std::uint64_t{0}, std::uint64_t{1} << 63}) {
            for (std::uint64_t exponent = 0; exponent <= 0x7FFu; ++exponent) {
                for (const std::uint64_t mantissa : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{0x8000000000000},
                                                     std::uint64_t{0xFFFFFFFFFFFFF}}) {
                    const std::uint64_t bits    = sign | (exponent << 52) | mantissa;
                    const bool          special = exponent == 0x7FFu;
                    const bool          tiny    = exponent == 0 && mantissa != 0;
                    ASSERT_TRUE((gives<double, std::uint64_t>(bits, special || tiny)));
                }
            }
        }
        EXPECT_EQ(catenae::sanitize_f64<1>::value(std::numeric_limits<double>::denorm_min()), 0.0);
        EXPECT_EQ(catenae::sanitize_f64<1>::value(std::numeric_limits<double>::infinity()), 0.0);
    }

    // Verifies CAT-SANITIZE-002: twice is once; in place and apart alike; the generic tests.
    TEST(sanitize, TwiceIsOnceAndItPassesTheGenericTests) {
        using s_t = catenae::sanitize<2>;
        std::vector<float> in;
        for (std::uint32_t bits = 0; bits < 0xFFFFFFFFu - 0x01000193u; bits += 0x01000193u) {
            in.push_back(of_bits<float>(bits)); // every class, both signs
        }
        if (in.size() % 2 != 0) {
            in.pop_back();
        }
        const s_t          s{};
        s_t::state         st{};
        s_t::workspace     ws{};
        std::vector<float> once(in.size());
        std::vector<float> twice(in.size());
        static_cast<void>(s.run(st, ws, catenae::span<const float>(in), catenae::span<float>(once)));
        static_cast<void>(s.run(st, ws, catenae::span<const float>(once), catenae::span<float>(twice)));
        EXPECT_EQ(std::memcmp(once.data(), twice.data(), once.size() * sizeof(float)), 0);
        std::vector<float> same = in;
        static_cast<void>(s.run(st, ws, catenae::span<const float>(same), catenae::span<float>(same)));
        EXPECT_EQ(std::memcmp(once.data(), same.data(), once.size() * sizeof(float)), 0);
        EXPECT_TRUE(catenae_test::counts_are_exact(s));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(s, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(s));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(catenae::sanitize_f64<3>{}));
    }

} // namespace
