// Copyright 2026 Timothy Place and the catenae contributors
// A high-pass FIR of an even tap count: its zero at Nyquist would stop what it passes.
// Verifies CAT-FIR-DESIGN-001.
// Expect: passes Nyquist
#include "catenae/fir_design.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using design = catenae::fir_design<float, 63>;
#else
    using design = catenae::fir_design<float, 64>;
#endif
    inline bool designs() noexcept {
        return design::make(catenae::fir_highpass{0.2, 60.0}).has_value();
    }
} // namespace cf
