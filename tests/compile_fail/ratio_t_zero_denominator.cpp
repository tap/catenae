// Copyright 2026 Timothy Place and the catenae contributors
// A ratio as a type with a denominator of 0.
// Verifies CAT-RATIO-004.
// Expect: catenae: a ratio_t's denominator is never 0
#include "catenae/ratio.h"

#ifdef CATENAE_CF_CONTROL
static_assert(catenae::ratio_t<3, 2>::down == 2, "3/2");
#else
static_assert(catenae::ratio_t<3, 0>::down == 0, "3/0");
#endif
