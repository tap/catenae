/// @file test_ratio.cpp
/// @brief ratio's contract (include/catenae/ratio.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <type_traits>

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

    /// A template that takes a ratio, as a stage takes its rate.
    template <typename R>
    struct takes_a_ratio {
        static constexpr ratio rate = R::value;
    };

    // Verifies CAT-RATIO-004.
    TEST(ratio, ARatioIsATypeForATemplateArgument) {
        using three_over_two = catenae::ratio_t<3, 2>;
        static_assert(same_terms(three_over_two::value, ratio{3, 2}), "the value");
        static_assert(three_over_two::up == 3 && three_over_two::down == 2, "its terms");
        static_assert(same_terms(takes_a_ratio<three_over_two>::rate, ratio{3, 2}), "through a template");
        // Reduced, as std::ratio is: two spellings of one fraction share a value and a type.
        using six_over_four = catenae::ratio_t<6, 4>;
        static_assert(same_terms(six_over_four::value, ratio{3, 2}), "the value is reduced");
        static_assert(!std::is_same<six_over_four, three_over_two>::value, "two spellings are two types");
        static_assert(std::is_same<six_over_four::type, three_over_two>::value, "whose type is one");
        static_assert(std::is_same<three_over_two::type, three_over_two>::value, "a reduced spelling is its type");
        static_assert(same_terms(catenae::ratio_t<0, 9>::value, ratio{0, 1}), "zero is 0/1");
        static_assert(same_terms(catenae::ratio_t<147, 160>::value * catenae::ratio_t<160, 147>::value, ratio{1, 1}),
                      "values do arithmetic");
        EXPECT_EQ((catenae::ratio_t<160, 147>::up), 160u);
    }

} // namespace
