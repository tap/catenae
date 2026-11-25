// Copyright 2026 Timothy Place and the catenae contributors
// A stage in place with an input group of 2: one that writes in bursts is not in place.
// Verifies CAT-STAGE-005.
// Expect: catenae: a stage is in place only with one port type, input_group 1
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_IN_PLACE
#define CF_OMIT_INPUT_GROUP
#define CF_MEMBERS                                                                                                     \
    static constexpr bool        is_in_place = true;                                                                   \
    static constexpr std::size_t input_group = 2;
#endif
#include "cf_stage.h"
