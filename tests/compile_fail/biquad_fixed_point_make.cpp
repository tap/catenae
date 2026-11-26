// Copyright 2026 Timothy Place and the catenae contributors
// A q31 biquad designed by make(): the coefficients are Q2.30 and the table check holds in them,
// but the design needs its section scaling, which arrives at C6 (D47).
// Verifies CAT-BIQUAD-DESIGN-006.
// Expect: catenae: a fixed-point biquad design needs its section scaling
#include "catenae/biquad_design.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using design = catenae::biquad_design<float>;
#else
    using design = catenae::biquad_design<catenae::q31>;
#endif

    bool designed() noexcept;
    bool designed() noexcept {
        return design::make({catenae::biquad_response::lowpass, 0.1, 0.7071, 0.0}).has_value();
    }
} // namespace cf
