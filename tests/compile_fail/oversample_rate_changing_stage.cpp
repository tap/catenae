// Copyright 2026 Timothy Place and the catenae contributors
// An oversampler around a stage that changes the rate: its stage is 1:1 and frame-synchronous.
// Verifies CAT-OVERSAMPLE-003.
// Expect: oversample's stage is 1:1 and frame-synchronous
#include "catenae/gain.h"
#include "catenae/oversample.h"
#include "catenae/resample.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using by = catenae::oversample<2, catenae::gain<2>>;
#else
    using by = catenae::oversample<2, catenae::up2<2>>;
#endif
    static_assert(by::factor == 2, "an oversampler");
} // namespace cf
