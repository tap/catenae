// Copyright 2026 Timothy Place and the catenae contributors
// A junction of independent clocks that states no tolerance.
// Verifies CAT-JUNCTION-003.
// Expect: catenae: a junction of independent clocks declares float tolerance_ppm
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_CLOCKING
#define CF_MEMBERS static constexpr catenae::clocks clocking = catenae::clocks::independent;
#endif
#include "cf_junction.h"
