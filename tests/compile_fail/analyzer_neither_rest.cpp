// Copyright 2026 Timothy Place and the catenae contributors
// An analyzer that neither writes reset nor declares rests_at_zero.
// Verifies CAT-ANALYZER-002.
// Expect: or declares rests_at_zero = true: exactly one
#ifndef CATENAE_CF_CONTROL
#define CF_OMIT_REST
#endif
#include "cf_analyzer.h"
