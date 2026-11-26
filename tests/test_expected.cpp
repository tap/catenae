/// @file test_expected.cpp
/// @brief status and expected's contract (include/catenae/expected.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "catenae/expected.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::expected;
    using catenae::status;
    using catenae::unexpected;

    struct design {
        int   taps;
        float gain;
    };

    constexpr expected<design, status> make(int taps) noexcept {
        if (taps <= 0) {
            return unexpected(status::invalid_argument);
        }
        if (taps > 64) {
            return unexpected(status::out_of_range);
        }
        return design{taps, 0.5f};
    }

    // Verifies CAT-EXPECTED-001.
    TEST(expected, ValueIsReachedEveryWay) {
        auto made = make(8);
        EXPECT_TRUE(made.has_value());
        EXPECT_TRUE(static_cast<bool>(made));
        EXPECT_EQ(made.value().taps, 8);
        EXPECT_EQ((*made).taps, 8);
        EXPECT_EQ(made->taps, 8);
        made->taps = 9;
        EXPECT_EQ(made.value().taps, 9);
        const auto& viewed = made;
        EXPECT_FLOAT_EQ(viewed->gain, 0.5f);
        EXPECT_EQ(viewed.value_or(design{1, 1.0f}).taps, 9);
    }

    // Verifies CAT-EXPECTED-002.
    TEST(expected, ErrorIsWhyThereIsNone) {
        const auto none = make(0);
        EXPECT_FALSE(none.has_value());
        EXPECT_FALSE(static_cast<bool>(none));
        EXPECT_EQ(none.error(), status::invalid_argument);
        EXPECT_EQ(make(65).error(), status::out_of_range);
        EXPECT_EQ(none.value_or(design{3, 1.0f}).taps, 3);
        const expected<int, status> unsupported = unexpected(status::unsupported);
        EXPECT_EQ(unsupported.error(), status::unsupported);
        EXPECT_EQ(unsupported.value_or(7), 7);
    }

    // Verifies CAT-EXPECTED-003.
    TEST(expected, EmptyAccessIsACheckInEveryBuild) {
        auto       none   = make(0);
        const auto viewed = make(0);
        const auto made   = make(4);
        CATENAE_EXPECT_CHECK(catenae::check::expected_empty, static_cast<void>(none.value()));
        CATENAE_EXPECT_CHECK(catenae::check::expected_empty, static_cast<void>(viewed.value()));
        CATENAE_EXPECT_CHECK(catenae::check::expected_empty, static_cast<void>(*none));
        CATENAE_EXPECT_CHECK(catenae::check::expected_empty, static_cast<void>(none->taps));
        CATENAE_EXPECT_CHECK(catenae::check::expected_not_error, static_cast<void>(made.error()));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(made.value()));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(none.error()));
    }

    // Verifies CAT-EXPECTED-004: usable in a constant expression (that an ignored one is an
    // error is tests/compile_fail/ignored_expected.cpp and ignored_status.cpp).
    TEST(expected, IsAConstantExpression) {
        static_assert(make(8).has_value() && make(8).value().taps == 8, "a value, at compile time");
        static_assert(!make(0).has_value() && make(0).error() == status::invalid_argument, "an error, too");
        static_assert(make(0).value_or(design{2, 1.0f}).taps == 2, "value_or, too");
        static_assert(std::is_trivially_copyable<expected<design, status>>::value, "a plain value to copy");
        static_assert(std::is_trivially_destructible<expected<design, status>>::value, "nothing to destroy");
        EXPECT_TRUE(make(1).has_value());
    }

    // Verifies CAT-EXPECTED-006.
    TEST(expected, AnExpectedAboutToEndGivesACopy) {
        using e_t = expected<design, status>;
        static_assert(std::is_same<decltype(*std::declval<e_t>()), design>::value, "*make() is a copy");
        static_assert(std::is_same<decltype(std::declval<e_t>().value()), design>::value, "so is value()");
        static_assert(std::is_same<decltype(std::declval<e_t>().error()), status>::value, "and error()");
        static_assert(std::is_same<decltype(*std::declval<e_t&>()), design&>::value, "one that lives on: a reference");
        static_assert(std::is_same<decltype(std::declval<const e_t&>().value()), const design&>::value, "const");
        static_assert(std::is_same<decltype(std::declval<const e_t&>().error()), const status&>::value, "const");
        // Bound to a reference, the copy lives as long as the reference (the sanitizer legs
        // would see a read of the ended expected otherwise).
        const design& kept = *make(12);
        const status& why  = make(0).error();
        EXPECT_EQ(kept.taps, 12);
        EXPECT_EQ(why, status::invalid_argument);
        CATENAE_EXPECT_CHECK(catenae::check::expected_empty, static_cast<void>(*make(0)));
        CATENAE_EXPECT_CHECK(catenae::check::expected_empty, static_cast<void>(make(0).value()));
        CATENAE_EXPECT_CHECK(catenae::check::expected_not_error, static_cast<void>(make(3).error()));
    }

    // Verifies CAT-EXPECTED-005.
    TEST(expected, StatusCodesAreStableNumbers) {
        static_assert(std::is_same<std::underlying_type_t<status>, std::uint8_t>::value, "a status is one byte");
        EXPECT_EQ(static_cast<int>(status::invalid_argument), 1);
        EXPECT_EQ(static_cast<int>(status::out_of_range), 2);
        EXPECT_EQ(static_cast<int>(status::unsupported), 3);
        EXPECT_EQ(static_cast<int>(status::size_mismatch), 4);
    }

} // namespace
