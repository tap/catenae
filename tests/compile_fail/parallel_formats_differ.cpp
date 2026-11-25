// Copyright 2026 Timothy Place and the catenae contributors
// An arm in float beside one in double: one sample format a side.
// Verifies CAT-PARALLEL-001.
// Expect: catenae: parallel<>'s arms read one sample format and one layout
#include "catenae/gain.h"
#include "catenae/parallel.h"

#ifdef CATENAE_CF_CONTROL
using other = catenae::gain<1>;
#else
using other = catenae::gain_f64<1>;
#endif
static_assert(catenae::parallel<catenae::gain<2>, other>::input_group == 1, "the type is formed");
