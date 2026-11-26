// Copyright 2026 Timothy Place and the catenae contributors
// A constant biquad design from a table with a pole on the unit circle (a2 = 1): from_table's
// check fails while the constant is evaluated, and check_failed is not constexpr.
// Verifies CAT-BIQUAD-DESIGN-005.
// Expect: check_failed
#include "catenae/biquad_design.h"

namespace cf {
    using design = catenae::biquad_design<float, 1>;
#ifdef CATENAE_CF_CONTROL
    constexpr float k_a2 = 0.5f;
#else
    constexpr float k_a2 = 1.0f;
#endif
    constexpr design k_design =
        design::from_table(design::table_type{{{0.25f, 0.5f, 0.25f, -0.5f, k_a2}}}, {1.0f, 0.5f, 100, 50, 0.0f});
} // namespace cf

static_assert(cf::k_design.table()[0][4] < 1.0f, "a stable table");
