// Copyright 2026 Timothy Place and the catenae contributors
// A constant peak programme meter's design from a table whose release is 1: from_table's check
// fails while the constant is evaluated, and check_failed is not constexpr.
// Verifies CAT-PPM-DESIGN-003.
// Expect: check_failed
#include "catenae/ppm_design.h"

namespace cf {
    using design = catenae::ppm_design<float>;
#ifdef CATENAE_CF_CONTROL
    constexpr float k_release = 0.999f;
#else
    constexpr float k_release = 1.0f;
#endif
    constexpr design k_design = design::from_table({0.01f, k_release});
} // namespace cf

static_assert(cf::k_design.numbers().release < 1.0f, "a one-pole's release");
