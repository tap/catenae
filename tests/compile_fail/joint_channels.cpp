// Copyright 2026 Timothy Place and the catenae contributors
// Two channels written into a stage that reads one.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: one stage's output and the next stage's input have different channel counts
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::port<float, 1>;
#else
using out_port = catenae::port<float, 2>;
#endif
using in_port = catenae::port<float, 1>;
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
