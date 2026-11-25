// Copyright 2026 Timothy Place and the catenae contributors
// float written into a stage that reads double.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: one stage's output and the next stage's input carry different sample formats
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::port<float, 2>;
#else
using out_port = catenae::port<double, 2>;
#endif
using in_port = catenae::port<float, 2>;
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
