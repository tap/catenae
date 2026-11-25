// Copyright 2026 Timothy Place and the catenae contributors
// A stage that declares rests_at_zero = false: the member says the state rests at zero, and is
// declared only to say so (D49).
// Verifies CAT-STAGE-003.
// Expect: or declares rests_at_zero = true: exactly one
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_REST
#define CF_MEMBERS                                                                                                     \
    static constexpr bool rests_at_zero = false;                                                                       \
    void                  reset(state& st) const noexcept {                                                            \
        st.held = 0.0f;                                                                               \
    }
#endif
#include "cf_stage.h"
