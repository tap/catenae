// Copyright 2026 Timothy Place and the catenae contributors
// One arm writes planar channels and the other interleaved: one layout a side.
// Verifies CAT-PARALLEL-001.
// Expect: catenae: parallel<>'s arms write one sample format and one layout
#include "catenae/gain.h"
#include "catenae/interleave.h"
#include "catenae/parallel.h"

#ifdef CATENAE_CF_CONTROL
using other = catenae::deinterleave<1>;
#else
using other = catenae::gain<1>;
#endif
static_assert(catenae::parallel<catenae::deinterleave<2>, other>::input_group == 1, "the type is formed");
