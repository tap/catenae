// Copyright 2026 Timothy Place and the catenae contributors
// A stage that declares is_frame_synchronous, which stage_traits derives (D35).
// Verifies CAT-STAGE-004.
// Expect: catenae: a stage never declares is_frame_synchronous
#ifndef CATENAE_CF_CONTROL
#define CF_MEMBERS static constexpr bool is_frame_synchronous = true;
#endif
#include "cf_stage.h"
