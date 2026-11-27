// Copyright 2026 Timothy Place and the catenae contributors
// A Nyquist design the generator never wrote: there is no such type.
// Verifies CAT-RESAMPLE-004.
// Expect: no generated Nyquist table at this band, length and attenuation
#include "catenae/resample.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using design = catenae::nyquist_design<2, 10, 70>;
#else
    using design = catenae::nyquist_design<2, 11, 70>;
#endif
    static_assert(design::taps > 0, "a design");
} // namespace cf
