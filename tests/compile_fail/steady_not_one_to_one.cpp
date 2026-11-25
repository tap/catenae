// Copyright 2026 Timothy Place and the catenae contributors
// steady<> around a configuration that doubles the rate: it evens out frames, not rates.
// Verifies CAT-STEADY-001.
// Expect: catenae: steady<> wraps a 1:1 configuration
#include "catenae/steady.h"
#include "catenae_fixtures/stages.h"

#ifdef CATENAE_CF_CONTROL
using wrapped = catenae::steady<catenae_fixtures::block_stage<1, 4>>;
#else
using wrapped = catenae::steady<catenae_fixtures::up2_toy<1>>;
#endif
static_assert(wrapped::input_group == 1, "the type is formed");
