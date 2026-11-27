// Copyright 2026 Timothy Place and the catenae contributors
// An oversampler by 2 around a FIR of 4 taps, whose 3/2 frames at the inner rate leave the
// latency a quarter of a base frame short of whole: no delay at an inner rate makes it whole.
// Verifies CAT-OVERSAMPLE-003.
// Expect: no delay at an inner rate makes this oversampler's latency whole
#include "catenae/fir.h"
#include "catenae/oversample.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using by = catenae::oversample<2, catenae::fir<2, 3>>; // 1 inner frame: one more makes it 20
#else
    using by = catenae::oversample<2, catenae::fir<2, 4>>;
#endif
    static_assert(by::factor == 2, "an oversampler");
} // namespace cf
