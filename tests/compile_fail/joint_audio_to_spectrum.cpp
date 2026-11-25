// Copyright 2026 Timothy Place and the catenae contributors
// Audio written into a stage that reads a spectrum.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: a time-domain port cannot feed a spectrum
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::port<float, 2>;
using in_port  = catenae::port<float, 2>;
#else
using out_port = catenae::port<float, 2>;
using in_port  = catenae::spectrum<float, 1, 2>;
#endif
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
