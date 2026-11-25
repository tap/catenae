// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose rate_ratio is 2/2, not reduced.
// Verifies CAT-STAGE-006.
// Expect: catenae: a stage's rate_ratio is a reduced ratio
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_RATE_RATIO
#define CF_MEMBERS static constexpr catenae::ratio rate_ratio{2, 2};
#endif
#include "cf_stage.h"
