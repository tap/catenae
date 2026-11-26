// Copyright 2026 Timothy Place and the catenae contributors
// sanitize over q31, where every value is a valid sample.
// Verifies CAT-SANITIZE-002.
// Expect: catenae: sanitize is for float and double: a fixed-point value is always valid
#include "catenae/sanitize.h"

#ifdef CATENAE_CF_CONTROL
static_assert(catenae::stage_traits<catenae::basic_sanitize<double, 2>>::opted_in, "double");
#else
static_assert(catenae::stage_traits<catenae::basic_sanitize<catenae::q31, 2>>::opted_in, "q31");
#endif
