// Copyright 2026 Timothy Place and the catenae contributors
// An arm that writes in blocks of four: parallel<> needs frame-synchronous arms.
// Verifies CAT-PARALLEL-001.
// Expect: catenae: parallel<>'s arms are frame-synchronous
#include "catenae/gain.h"
#include "catenae/parallel.h"
#include "catenae/steady.h"
#include "catenae_fixtures/stages.h"

#ifdef CATENAE_CF_CONTROL
using arm = catenae::steady<catenae_fixtures::block_stage<1, 4>>;
#else
using arm = catenae_fixtures::block_stage<1, 4>;
#endif
static_assert(catenae::parallel<arm, catenae::gain<1>, catenae::align::by_caller>::input_group == 1,
              "the type is formed");
