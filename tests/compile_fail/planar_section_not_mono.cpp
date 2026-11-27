// Copyright 2026 Timothy Place and the catenae contributors
// A planar_section of a stereo gain: it runs mono stages, one channel in and one out, over each
// channel of its port.
// Verifies CAT-PLANAR-003.
// Expect: catenae: a planar_section runs mono stages
#include "catenae/gain.h"
#include "catenae/planar_section.h"
#include "catenae/stage_traits.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using section_t = catenae::planar_section<2, catenae::gain<1>>;
#else
    using section_t = catenae::planar_section<2, catenae::gain<2>>;
#endif
    static_assert(catenae::stage_traits<section_t>::input_port::channels == 2, "two channels");
} // namespace cf
