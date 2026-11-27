// Copyright 2026 Timothy Place and the catenae contributors
// A limiter that looks no frames ahead: its look-ahead is 1 to 4,096 frames.
// Verifies CAT-LIMITER-003.
// Expect: catenae: a limiter looks 1 to 4,096 frames ahead
#include "catenae/limiter.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage_t = catenae::limiter<2, 1>;
#else
    using stage_t = catenae::limiter<2, 0>;
#endif
    static_assert(catenae::stage_traits<stage_t>::opted_in, "a stage");
} // namespace cf
