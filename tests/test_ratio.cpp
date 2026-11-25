/// @file test_ratio.cpp
/// @brief ratio's contract (include/catenae/ratio.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>

#include <gtest/gtest.h>

#include "catenae/ratio.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::ratio;

    constexpr bool same_terms(ratio a, ratio b) noexcept {
        return a.up == b.up && a.down == b.down;
    }

    // Verifies CAT-RATIO-001.
    TEST(ratio, EqualAsFractions) {
        static_assert(ratio{1, 2} == ratio{2, 4} && ratio{0, 1} == ratio{0, 7}, "equal fractions are equal");
        static_assert(ratio{1, 2} != ratio{2, 3}, "and unequal ones are not");
        static_assert(same_terms(catenae::reduce(ratio{6, 4}), ratio{3, 2}), "reduce divides by the gcd");
        static_assert(same_terms(catenae::reduce(ratio{0, 5}), ratio{0, 1}), "zero is 0/1");
        static_assert(same_terms(catenae::reduce(ratio{147, 160}), ratio{147, 160}), "44.1 to 48 is reduced");
        static_assert(catenae::is_reduced(ratio{3, 2}) && catenae::is_reduced(ratio{0, 1}), "reduced forms");
        static_assert(!catenae::is_reduced(ratio{6, 4}) && !catenae::is_reduced(ratio{0, 5}), "unreduced forms");
        static_assert(!catenae::is_reduced(ratio{1, 0}), "a zero denominator is no ratio");
        EXPECT_TRUE(catenae::is_reduced(catenae::reduce(ratio{4294967295u, 3u})));
    }

    // Verifies CAT-RATIO-002.
    TEST(ratio, ArithmeticIsExact) {
        static_assert(same_terms(ratio{1, 2} + ratio{1, 3}, ratio{5, 6}), "a sum, reduced");
        static_assert(same_terms(ratio{3, 4} - ratio{1, 4}, ratio{1, 2}), "a difference, reduced");
        static_assert(same_terms(ratio{2, 3} * ratio{3, 4}, ratio{1, 2}), "a product, reduced");
        static_assert(same_terms(ratio{1, 2} - ratio{1, 2}, ratio{0, 1}), "a difference of zero");
        static_assert(ratio{1, 3} < ratio{1, 2} && ratio{1, 2} > ratio{1, 3}, "comparisons");
        static_assert(ratio{1, 2} <= ratio{2, 4} && ratio{2, 4} >= ratio{1, 2}, "comparisons that are equal");
        // 64-bit intermediates: these terms overflow 32 bits before they are reduced.
        static_assert(ratio{4000000000u, 1} < ratio{4000000001u, 1}, "large terms compare exactly");
        static_assert(ratio{1, 4000000000u} > ratio{1, 4000000001u}, "large denominators compare exactly");
        static_assert(same_terms(ratio{100000, 99999} * ratio{99999, 100000}, ratio{1, 1}), "a product above 2^32");
        static_assert(same_terms(ratio{1, 100000} + ratio{1, 100000}, ratio{1, 50000}), "a sum above 2^32");
        EXPECT_EQ((ratio{147, 160} * ratio{160, 147}).up, 1u);
    }

    // Verifies CAT-RATIO-003.
    TEST(ratio, NegativeDifferenceIsAnAuditCheck) {
        if constexpr (catenae::detail::k_audit_level) {
            CATENAE_EXPECT_CHECK(catenae::check::invariant, static_cast<void>(ratio{1, 3} - ratio{1, 2}));
        }
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(ratio{1, 2} - ratio{1, 3}));
    }

} // namespace
