// Copyright 2026 Timothy Place and the catenae contributors
// convert from double: not among the four formats convert pins (D29).
// Verifies CAT-CONVERT-006.
// Expect: catenae: convert is among float, q15, q31 and Q4.27
#include "catenae/convert.h"

#ifdef CATENAE_CF_CONTROL
static_assert(catenae::stage_traits<catenae::convert<float, catenae::q15, 1>>::opted_in, "float to q15");
#else
static_assert(catenae::stage_traits<catenae::convert<double, catenae::q15, 1>>::opted_in, "double to q15");
#endif
