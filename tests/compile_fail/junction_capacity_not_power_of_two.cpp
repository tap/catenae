// Copyright 2026 Timothy Place and the catenae contributors
// A junction whose capacity is 12 frames.
// Verifies CAT-JUNCTION-004.
// Expect: catenae: a junction's capacity_frames is a power of two
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_CAPACITY
#define CF_MEMBERS static constexpr std::size_t capacity_frames = 12;
#endif
#include "cf_junction.h"
