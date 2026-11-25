// Copyright 2026 Timothy Place and the catenae contributors
// A number as an operand of |.
// Verifies CAT-CHAIN-001.
// Expect: catenae: both sides of \| are stages
#include "catenae/gain.h"

void formed() noexcept;
void formed() noexcept {
#ifdef CATENAE_CF_CONTROL
    static_cast<void>(catenae::gain<1>{1.0f, 0} | catenae::gain<1>{0.5f, 0});
#else
    catenae::gain<1>{1.0f, 0} | 3;
#endif
}
