// Copyright 2026 Timothy Place and the catenae contributors
// An analyzer that leaves out results_ready.
// Verifies CAT-ANALYZER-001.
// Expect: catenae: an analyzer declares std::uint32_t results_ready
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_RESULTS_READY
#endif
#include "cf_analyzer.h"
