// Copyright 2026 Timothy Place and the catenae contributors
// A stage whose configuration holds a 516-byte table by value, one float above the 512 (D54).
// Verifies CAT-STAGE-007.
// Expect: catenae: a stage's configuration is at most 512 bytes
#ifndef CATENAE_CF_CONTROL
#define CF_MEMBERS float table[129];
#endif
#include "cf_stage.h"
