// Copyright 2026 Timothy Place and the catenae contributors
// A stage with an input group of 2 that does not say what it writes from rest: its arrival
// numbers bound the count, and fed_by needs it exactly.
// Verifies CAT-STAGE-013.
// Expect: catenae: a stage whose input_group is above 1 declares static constexpr std::size_t outputs_from_rest
#define CF_OMIT_INPUT_GROUP
#ifdef CATENAE_CF_CONTROL
#define CF_MEMBERS                                                                                                     \
    static constexpr std::size_t input_group = 2;                                                                      \
    static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept {                                           \
        return n / 2 * 2;                                                                                              \
    }
#else
#define CF_MEMBERS static constexpr std::size_t input_group = 2;
#endif
#include "cf_stage.h"
