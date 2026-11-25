// Copyright 2026 Timothy Place and the catenae contributors
// A wire port at a joint, wire to wire: only unpack reads one and pack writes one.
// Verifies CAT-CHAIN-001 and CAT-PORT-003.
// Expect: catenae: a wire port joins nothing inside a chain
#include "cf_ports.h"
#ifdef CATENAE_CF_CONTROL
using out_port = catenae::port<float, 2>;
using in_port  = catenae::port<float, 2>;
#else
using out_port = catenae::wire_port<cf::s24, 2>;
using in_port  = catenae::wire_port<cf::s24, 2>;
#endif
static_assert(cf::joined<out_port, in_port>::input_group == 1, "the chain's type is formed");
