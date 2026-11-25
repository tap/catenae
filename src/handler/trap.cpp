/// @file trap.cpp
/// @brief A default check_failed that traps: link it when the system has no safe state of its own.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Not part of the compiled core (PLAN.md section 3.4): the library defines no handler, and an
// integrator defines check_failed where their system enters its safe state. This file is the
// shipped default for the rest, the target catenae::trap_handler: it stops the program at
// once, with no message, since there are no strings in ROM.
#include "catenae/check.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        void check_failed(check code) noexcept {
            static_cast<void>(code);
#if defined(_MSC_VER) && !defined(__clang__)
            __fastfail(7); // FAST_FAIL_FATAL_APP_EXIT
#else
            __builtin_trap();
#endif
        }

    } // namespace CATENAE_ABI
} // namespace catenae
