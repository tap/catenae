// Copyright 2026 Timothy Place and the catenae contributors
// An up2 at a design divisor no half-band is pinned at: the stage does not exist.
// Verifies CAT-RESAMPLE-004.
// Expect: no half-band is pinned at this design divisor
#include "catenae/resample.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage = catenae::up2<2, 2>;
#else
    using stage = catenae::up2<2, 3>;
#endif
    static_assert(catenae::stage_traits<stage>::opted_in, "a stage");
} // namespace cf
