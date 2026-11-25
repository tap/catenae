/// @file test_check.cpp
/// @brief The checks' contract (include/catenae/check.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/check.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::check;

    constexpr int passes_always(int value) noexcept {
        catenae::detail::check_always(value >= 0, check::invariant);
        return value;
    }
    constexpr int passes_audit(int value) noexcept {
        catenae::detail::check_audit(value >= 0, check::invariant);
        return value;
    }

    // Verifies CAT-CHECK-001.
    TEST(check, FailedCheckReachesTheHandlerWithItsCode) {
        CATENAE_EXPECT_CHECK(check::output_too_small, catenae::detail::check_always(false, check::output_too_small));
        CATENAE_EXPECT_CHECK(check::expected_empty, catenae::detail::check_always(false, check::expected_empty));
        CATENAE_EXPECT_NO_CHECK(catenae::detail::check_always(true, check::output_too_small));
        static_assert(std::is_same<decltype(&catenae::check_failed), void (*)(check) noexcept>::value,
                      "check_failed takes a code and is noexcept");
    }

    // Verifies CAT-CHECK-002.
    TEST(check, AuditChecksRunOnlyAtTheAuditLevel) {
        if constexpr (catenae::detail::k_audit_level) {
            CATENAE_EXPECT_CHECK(check::invariant, catenae::detail::check_audit(false, check::invariant));
        }
        else {
            CATENAE_EXPECT_NO_CHECK(catenae::detail::check_audit(false, check::invariant));
        }
        CATENAE_EXPECT_NO_CHECK(catenae::detail::check_audit(true, check::invariant));
        EXPECT_EQ(catenae::detail::k_audit_level, CATENAE_CHECKS == CATENAE_CHECKS_AUDIT);
    }

    // Verifies CAT-CHECK-003: a check that holds is a constant expression; one that fails does
    // not compile (tests/compile_fail/check_in_constant.cpp).
    TEST(check, PassingCheckIsAConstantExpression) {
        static_assert(passes_always(3) == 3, "check_always that holds evaluates at compile time");
        static_assert(passes_audit(3) == 3, "check_audit that holds evaluates at compile time");
        EXPECT_EQ(passes_always(4), 4);
    }

    // Verifies CAT-CHECK-005: the numbers an integrator's handler switches on.
    TEST(check, CodesAreStableNumbers) {
        static_assert(std::is_same<std::underlying_type_t<check>, std::uint8_t>::value, "a code is one byte");
        EXPECT_EQ(static_cast<int>(check::input_not_whole_frames), 1);
        EXPECT_EQ(static_cast<int>(check::output_too_small), 2);
        EXPECT_EQ(static_cast<int>(check::flush_output_too_small), 3);
        EXPECT_EQ(static_cast<int>(check::push_too_large), 4);
        EXPECT_EQ(static_cast<int>(check::pull_too_large), 5);
        EXPECT_EQ(static_cast<int>(check::request_too_large), 6);
        EXPECT_EQ(static_cast<int>(check::expected_empty), 32);
        EXPECT_EQ(static_cast<int>(check::expected_not_error), 33);
        EXPECT_EQ(static_cast<int>(check::index_out_of_range), 64);
        EXPECT_EQ(static_cast<int>(check::invariant), 65);
        EXPECT_EQ(static_cast<int>(check::outputs_exceeded), 66);
        EXPECT_EQ(static_cast<int>(check::state_not_reset), 67);
        EXPECT_EQ(static_cast<int>(check::fifo_short), 68);
        EXPECT_EQ(static_cast<int>(check::fifo_over), 69);
        EXPECT_EQ(static_cast<int>(check::pull_above_staging), 70);
        EXPECT_EQ(static_cast<int>(check::flush_to_zero), 71);
    }

} // namespace
