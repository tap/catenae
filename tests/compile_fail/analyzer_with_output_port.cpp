// Copyright 2026 Timothy Place and the catenae contributors
// An analyzer that declares an output port: tap<> passes the audio through, so it has none.
// Verifies CAT-ANALYZER-001.
// Expect: catenae: an analyzer has no output port
#ifndef CATENAE_CF_CONTROL
#define CF_MEMBERS using output_port = catenae::port<float, 1>;
#endif
#include "cf_analyzer.h"
