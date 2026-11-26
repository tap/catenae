// Copyright 2026 Timothy Place and the catenae contributors
// unpack given a sample format where a wire format belongs.
// Verifies CAT-WIRE-005.
// Expect: catenae: unpack reads a wire format
#include "catenae/unpack.h"

#ifdef CATENAE_CF_CONTROL
static_assert(catenae::stage_traits<catenae::unpack<catenae::wire::s32le, 2>>::opted_in, "a wire format");
#else
static_assert(catenae::stage_traits<catenae::unpack<catenae::q31, 2>>::opted_in, "a sample format");
#endif
