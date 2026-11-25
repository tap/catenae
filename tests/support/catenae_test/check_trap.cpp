/// @file check_trap.cpp
/// @brief The battery's definition of check_failed (check_trap.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include "catenae_test/check_trap.h"

#include <csetjmp>
#include <cstdlib>

namespace catenae_test {

    check_trap& trap() noexcept {
        static check_trap the_trap{};
        return the_trap;
    }

    // MSVC warns at every setjmp in C++ (C4611), whatever the frame holds; this one holds
    // nothing with a destructor, and the jump crosses only the statement's frames.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4611) // DEV-015
#endif
    bool fails_a_check(void (*body)(void*), void* context) noexcept {
        check_trap& t = trap();
        t.armed       = true;
        if (setjmp(t.resume) == 0) {
            body(context);
            t.armed = false;
            return false;
        }
        return true;
    }
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

} // namespace catenae_test

namespace catenae {
    inline namespace CATENAE_ABI {

        void check_failed(check code) noexcept {
            catenae_test::check_trap& t = catenae_test::trap();
            ++t.failures;
            t.code = code;
            if (t.armed) {
                t.armed = false;
                std::longjmp(t.resume, 1);
            }
            std::abort();
        }

    } // namespace CATENAE_ABI
} // namespace catenae
