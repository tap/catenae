// Copyright 2026 Timothy Place and the catenae contributors
// A constant compressor design from a table whose attack coefficient is 1: from_table's check
// fails while the constant is evaluated, and check_failed is not constexpr.
// Verifies CAT-DYNAMICS-DESIGN-003.
// Expect: check_failed
#include "catenae/dynamics_design.h"

namespace cf {
    using design = catenae::compressor_design<float>;
#ifdef CATENAE_CF_CONTROL
    constexpr float k_attack = 0.5f;
#else
    constexpr float k_attack = 1.0f;
#endif
    constexpr design k_design = design::from_table({-2.0f, 0.3f, 0.75f, 0.5f, 0.0f, k_attack, 0.5f, 0.99f, 0.01f});
} // namespace cf

static_assert(cf::k_design.numbers().attack < 1.0f, "a one-pole's coefficient");
