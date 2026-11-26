// Copyright 2026 Timothy Place and the catenae contributors
// make(spec) for a design too long to hold by value: it is a table, written into the caller's.
// Verifies CAT-FIR-DESIGN-003.
// Expect: a design this long is a table
#include "catenae/fir_design.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using design = catenae::fir_design<float, 128>;
#else
    using design = catenae::fir_design<float, 129>;
#endif
    inline bool designs() noexcept { return design::make(catenae::fir_lowpass{0.1, 60.0}).has_value(); }
} // namespace cf
