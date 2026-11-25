// Copyright 2026 Timothy Place and the catenae contributors
// A junction whose state starts its indices with default member initialisers.
// Verifies CAT-JUNCTION-002.
// Expect: catenae: a junction's state is a plain aggregate
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_STATE
#define CF_TYPES                                                                                                       \
    struct state {                                                                                                     \
        float         held[8];                                                                                         \
        std::uint32_t written = 0;                                                                                     \
        std::uint32_t read    = 0;                                                                                     \
    };
#endif
#include "cf_junction.h"
