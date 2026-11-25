// Copyright 2026 Timothy Place and the catenae contributors
// An arm at 2:1 beside one at 1:1.
// Verifies CAT-PARALLEL-001.
// Expect: catenae: parallel<>'s arms run at one ratio
#include "catenae/gain.h"
#include "catenae/parallel.h"
#include "catenae_fixtures/stages.h"

#ifdef CATENAE_CF_CONTROL
using other = catenae_fixtures::up2_toy<1>;
#else
using other = catenae::gain<1>;
#endif
static_assert(catenae::parallel<catenae_fixtures::up2_toy<1>, other, catenae::align::by_caller>::input_group == 1,
              "the type is formed");
