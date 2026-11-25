// Copyright 2026 Timothy Place and the catenae contributors
// A stage that leaves out a member of section 3.2: max_lead.
// Verifies CAT-STAGE-001.
// Expect: catenae: a stage declares static constexpr ratio max_lead
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_MAX_LEAD
#endif
#include "cf_stage.h"
