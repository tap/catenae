/// @file check_trap.h
/// @brief The battery's check_failed: a failed check returns to the test that expected it.
// Copyright 2026 Timothy Place and the catenae contributors
//
// check_failed is [[noreturn]] and the library defines none (PLAN.md section 3.4); the battery
// defines one (check_trap.cpp) that, while a test has armed it, records the code and jumps
// back to the test with longjmp, and otherwise aborts. It works on every leg, the bare-metal
// ones included, where GoogleTest has no death tests. Between the setjmp and the check, a test
// holds nothing with a destructor: the jump crosses only catenae's inline functions.
//
//   CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s[9]));
#ifndef CATENAE_TESTS_SUPPORT_CATENAE_TEST_CHECK_TRAP_H
#define CATENAE_TESTS_SUPPORT_CATENAE_TEST_CHECK_TRAP_H

#include <csetjmp>

#include <gtest/gtest.h>

#include "catenae/check.h"

namespace catenae_test {

    /// Where an armed check_failed returns to, and what it saw.
    struct check_trap {
        std::jmp_buf   resume;
        bool           armed;
        catenae::check code;
        unsigned       failures;
    };

    /// The battery's one trap.
    check_trap& trap() noexcept;

} // namespace catenae_test

/// Runs `statement`, which must fail the check `expected_code`.
#define CATENAE_EXPECT_CHECK(expected_code, statement)                                                                 \
    do {                                                                                                               \
        ::catenae_test::trap().armed = true;                                                                           \
        if (setjmp(::catenae_test::trap().resume) == 0) {                                                              \
            statement;                                                                                                 \
            ::catenae_test::trap().armed = false;                                                                      \
            ADD_FAILURE() << "no check failed in: " #statement;                                                        \
        }                                                                                                              \
        else {                                                                                                         \
            EXPECT_EQ(static_cast<int>(::catenae_test::trap().code), static_cast<int>(expected_code)) << #statement;   \
        }                                                                                                              \
    } while (false)

/// Runs `statement`, which must fail no check.
#define CATENAE_EXPECT_NO_CHECK(statement)                                                                             \
    do {                                                                                                               \
        const unsigned catenae_before = ::catenae_test::trap().failures;                                               \
        statement;                                                                                                     \
        EXPECT_EQ(::catenae_test::trap().failures, catenae_before) << #statement;                                      \
    } while (false)

#endif // CATENAE_TESTS_SUPPORT_CATENAE_TEST_CHECK_TRAP_H
