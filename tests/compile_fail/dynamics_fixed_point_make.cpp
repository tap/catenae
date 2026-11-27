// Copyright 2026 Timothy Place and the catenae contributors
// A q31 compressor designed by make(): the contract is declared, the profile arrives at C6
// (D47).
// Verifies CAT-DYNAMICS-DESIGN-004.
// Expect: catenae: a fixed-point dynamics design is new design
#include "catenae/dynamics_design.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using design = catenae::compressor_design<float>;
#else
    using design = catenae::compressor_design<catenae::q31>;
#endif

    bool designed() noexcept;
    bool designed() noexcept {
        return design::make({-20.0, 4.0, 6.0, 10.0, 1000.0, 0.0}).has_value();
    }
} // namespace cf
