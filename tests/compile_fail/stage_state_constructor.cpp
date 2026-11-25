// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose state has a constructor: start-up code for a static one.
// Verifies CAT-STAGE-002.
// Expect: catenae: a stage's state is a plain aggregate
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_STATE
#define CF_TYPES                                                                                                       \
    struct state {                                                                                                     \
        state() noexcept                                                                                               \
            : held(0.0f) {}                                                                                            \
        float held;                                                                                                    \
    };
#endif
#include "cf_stage.h"
