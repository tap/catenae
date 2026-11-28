// Copyright 2026 Timothy Place and the catenae contributors
// A tap of two channels over a meter of one: a tap's channel count is its analyzer's port's.
// Verifies CAT-TAP-003.
// Expect: catenae: a tap's channel count is its analyzer's port's
#include "catenae/peak_meter.h"
#include "catenae/tap.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_channels = 1;
#else
    constexpr std::size_t k_channels = 2;
#endif
    using tap_t = catenae::tap<k_channels, catenae::peak_meter<1, 64>>;
} // namespace cf

static_assert(catenae::stage_traits<cf::tap_t>::opted_in, "a stage");
