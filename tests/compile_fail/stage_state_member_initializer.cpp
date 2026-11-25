// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose state has a default member initialiser: no longer trivially constructible.
// Verifies CAT-STAGE-002.
// Expect: catenae: a stage's state is a plain aggregate
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_STATE
#define CF_TYPES                                                                                                       \
    struct state {                                                                                                     \
        float held = 0.0f;                                                                                             \
    };
#endif
#include "cf_stage.h"
