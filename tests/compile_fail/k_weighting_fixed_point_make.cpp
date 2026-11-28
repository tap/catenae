// Copyright 2026 Timothy Place and the catenae contributors
// A q31 K-weighting designed by make(): its table is Q2.30, but the design needs its section
// scaling, which arrives at C6 (D47).
// Verifies CAT-K-WEIGHTING-002.
// Expect: catenae: a fixed-point K-weighting needs its section scaling
#include "catenae/loudness_design.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using design = catenae::k_weighting_design<float>;
#else
    using design = catenae::k_weighting_design<catenae::q31>;
#endif

    bool designed() noexcept;
    bool designed() noexcept {
        return design::make(48000.0).has_value();
    }
} // namespace cf
