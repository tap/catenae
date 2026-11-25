// Copyright 2026 Timothy Place and the catenae contributors
// A CATENAE_CONSTINIT static that start-up code would build: its constructor is not constexpr.
// On MSVC the macro is empty and GCC and Clang carry the check (DEV-001).
// Verifies CAT-COMPILER-001.
// Compilers: GNU Clang AppleClang
// Expect: does not have a constant initializer
#include "catenae/compiler.h"

namespace cf {
    struct built {
#ifdef CATENAE_CF_CONTROL
        constexpr built() noexcept
#else
        built() noexcept
#endif
            : level(1) {
        }
        int level;
    };
    extern built g_built;
} // namespace cf

CATENAE_CONSTINIT cf::built cf::g_built{};
