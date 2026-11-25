// Copyright 2026 Timothy Place and the catenae contributors
// convert from a format to the same format.
// Verifies CAT-CONVERT-006.
// Expect: catenae: convert is between two different formats
#include "catenae/convert.h"

#ifdef CATENAE_CF_CONTROL
static_assert(catenae::stage_traits<catenae::convert<catenae::q31, catenae::q15, 1>>::opted_in, "q31 to q15");
#else
static_assert(catenae::stage_traits<catenae::convert<catenae::q31, catenae::q31, 1>>::opted_in, "q31 to q31");
#endif
