// Copyright 2026 Timothy Place and the catenae contributors
// A tap over a stage, which is not an analyzer: a tap holds a type that specialises is_analyzer.
// Verifies CAT-TAP-003.
// Expect: catenae: a tap holds an analyzer
#include "catenae/gain.h"
#include "catenae/rms_meter.h"
#include "catenae/tap.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using held_t = catenae::rms_meter<1, 64>;
#else
    using held_t = catenae::gain<1>;
#endif
    using tap_t = catenae::tap<1, held_t>;
} // namespace cf

static_assert(catenae::stage_traits<cf::tap_t>::opted_in, "a stage");
