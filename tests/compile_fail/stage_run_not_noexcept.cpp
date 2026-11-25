// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose run may throw: it is not noexcept.
// Verifies CAT-STAGE-001.
// Expect: catenae: a stage declares std::size_t run
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_RUN
#define CF_MEMBERS                                                                                                     \
    std::size_t run(state&, workspace&, catenae::span<const float> in, catenae::span<float>) const {                   \
        return in.size();                                                                                              \
    }
#endif
#include "cf_stage.h"
