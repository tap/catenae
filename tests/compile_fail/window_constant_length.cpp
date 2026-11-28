// Copyright 2026 Timothy Place and the catenae contributors
// A named window's constant at 16384, past the constants' 8192: make() builds a longer one.
// Verifies CAT-WINDOW-003.
// Expect: catenae: a named window's constant is a power of two from 16 to 8192
#include "catenae/window.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_length = 8192;
#else
    constexpr std::size_t k_length = 16384;
#endif
    using design = catenae::window_design<float, k_length>;
    design hann() noexcept;
    design hann() noexcept {
        return design{catenae::window::hann};
    }
} // namespace cf
