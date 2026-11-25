// Copyright 2026 Timothy Place and the catenae contributors
// Two arms at 2:1 whose latency is half a frame: the types cannot size a delay of half a frame.
// Verifies CAT-PARALLEL-002.
// Expect: catenae: parallel<> aligns by whole frames
#include "catenae/parallel.h"
#include "catenae_fixtures/stages.h"

#ifdef CATENAE_CF_CONTROL
constexpr catenae::align k_align = catenae::align::by_caller;
#else
constexpr catenae::align k_align = catenae::align::by_type;
#endif
static_assert(catenae::parallel<catenae_fixtures::up2_toy<1>, catenae_fixtures::up2_toy<1>, k_align>::input_group == 1,
              "the type is formed");
