// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose configuration has a copy constructor of its own.
// Verifies CAT-STAGE-007.
// Expect: catenae: a stage's configuration is trivially copyable
#ifndef CATENAE_CF_CONTROL
#define CF_MEMBERS                                                                                                     \
    stage() noexcept = default;                                                                                        \
    stage(const stage&) noexcept {}                                                                                    \
    stage& operator=(const stage&) noexcept = default;
#endif
#include "cf_stage.h"
