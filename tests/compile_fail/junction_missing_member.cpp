// Copyright 2026 Timothy Place and the catenae contributors
// A junction that leaves out capacity_frames.
// Verifies CAT-JUNCTION-001.
// Expect: catenae: a junction declares static constexpr std::size_t capacity_frames
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_CAPACITY
#endif
#include "cf_junction.h"
