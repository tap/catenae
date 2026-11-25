// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose input_group is an int, not a std::size_t.
// Verifies CAT-STAGE-001.
// Expect: catenae: a stage declares static constexpr std::size_t input_group
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_INPUT_GROUP
#define CF_MEMBERS static constexpr int input_group = 1;
#endif
#include "cf_stage.h"
