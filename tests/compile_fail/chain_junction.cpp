// Copyright 2026 Timothy Place and the catenae contributors
// A junction as an operand of |: feeding and fed_by join a chain to one (D19).
// Verifies CAT-CHAIN-001.
// Expect: catenae: a junction is never inside a chain
#define CF_OPT_IN
#include "catenae/gain.h"
#include "cf_junction.h"

void formed() noexcept;
void formed() noexcept {
#ifdef CATENAE_CF_CONTROL
    static_cast<void>(catenae::gain<1>{1.0f, 0} | catenae::gain<1>{0.5f, 0});
#else
    catenae::gain<1>{1.0f, 0} | cf::junction{};
#endif
}
