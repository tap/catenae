// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose workspace has a constructor.
// Verifies CAT-STAGE-002.
// Expect: catenae: a stage's workspace is a plain aggregate
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_WORKSPACE
#define CF_TYPES                                                                                                       \
    struct workspace {                                                                                                 \
        workspace() noexcept {}                                                                                        \
        float scratch[4];                                                                                              \
    };
#endif
#include "cf_stage.h"
