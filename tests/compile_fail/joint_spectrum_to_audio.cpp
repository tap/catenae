// Copyright 2026 Timothy Place and the catenae contributors
// A spectrum written into a stage that reads audio.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: a spectrum cannot feed a time-domain port
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::spectrum<float, 1, 8>;
using in_port  = catenae::spectrum<float, 1, 8>;
#else
using out_port = catenae::spectrum<float, 1, 8>;
using in_port  = catenae::port<float, 8>;
#endif
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
