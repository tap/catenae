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
