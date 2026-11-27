// Copyright 2026 Timothy Place and the catenae contributors
// An oversampler by 3: oversample is by 2 or by 4.
// Verifies CAT-OVERSAMPLE-003.
// Expect: oversample is by 2 or by 4
#include "catenae/gain.h"
#include "catenae/oversample.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using by = catenae::oversample<2, catenae::gain<2>>;
#else
    using by = catenae::oversample<3, catenae::gain<2>>;
#endif
    static_assert(by::factor != 0, "an oversampler");
} // namespace cf
