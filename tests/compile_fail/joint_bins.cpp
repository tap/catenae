// Copyright 2026 Timothy Place and the catenae contributors
// A spectrum of 257 bins written into a stage that reads 129.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: one stage's output and the next stage's input have different numbers of bins
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::spectrum<float, 1, 129>;
#else
using out_port = catenae::spectrum<float, 1, 257>;
#endif
using in_port = catenae::spectrum<float, 1, 129>;
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
