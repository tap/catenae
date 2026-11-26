// Copyright 2026 Timothy Place and the catenae contributors
// A wire format of 16 bits in a 32-bit word: not one of the formats unpack and pack move.
// Verifies CAT-WIRE-005.
// Expect: catenae: a wire format is 16-bit, 24-bit packed, 24 bits in 32, or 32-bit
#include "catenae/wire.h"

#ifdef CATENAE_CF_CONTROL
using format_t = catenae::wire::format<2, 16, catenae::wire::order::little>;
#else
using format_t = catenae::wire::format<4, 16, catenae::wire::order::little>;
#endif

static_assert(format_t::bytes_per_sample >= 2, "the format's type is formed");
