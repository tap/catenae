// Copyright 2026 Timothy Place and the catenae contributors
// A constant linear-phase FIR design from a table that is neither symmetric nor antisymmetric:
// from_table's check fails while the constant is evaluated, and check_failed is not constexpr.
// Verifies CAT-FIR-DESIGN-004.
// Expect: check_failed
#include <array>

#include "catenae/fir_design.h"

namespace cf {
    using design = catenae::fir_design<float, 3>;
#ifdef CATENAE_CF_CONTROL
    constexpr std::array<float, 3> k_taps = {0.25f, 0.5f, 0.25f};
#else
    constexpr std::array<float, 3> k_taps = {0.25f, 0.5f, 0.26f};
#endif
    constexpr design k_design = design::from_table(k_taps);
} // namespace cf

static_assert(cf::k_design.table()[1] > 0.0f, "a symmetric table");
