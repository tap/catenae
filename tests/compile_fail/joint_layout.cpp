// Copyright 2026 Timothy Place and the catenae contributors
// Interleaved frames written into a stage that reads planes.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: one stage's output and the next stage's input have different layouts
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::port<float, 2, catenae::layout::planar>;
#else
using out_port = catenae::port<float, 2>;
#endif
using in_port = catenae::port<float, 2, catenae::layout::planar>;
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
