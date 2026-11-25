// Copyright 2026 Timothy Place and the catenae contributors
// A stage that writes reset and also declares rests_at_zero.
// Verifies CAT-STAGE-003.
// Expect: or declares rests_at_zero = true: exactly one
#ifndef CATENAE_CF_CONTROL
#define CF_MEMBERS                                                                                                     \
    void reset(state& st) const noexcept {                                                                             \
        st.held = 0.0f;                                                                                                \
    }
#endif
#include "cf_stage.h"
