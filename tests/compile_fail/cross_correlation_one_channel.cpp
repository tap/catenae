// Copyright 2026 Timothy Place and the catenae contributors
// A cross-correlation of channel 1 with itself: it reads two channels of its port.
// Verifies CAT-CROSS-CORRELATION-003.
// Expect: catenae: a cross-correlation reads two channels of its port
#include "catenae/cross_correlation.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_b = 0;
#else
    constexpr std::size_t k_b = 1;
#endif
    using meter_t = catenae::cross_correlation<2, 1, k_b, 4, 64>;
} // namespace cf

static_assert(catenae::analyzer_traits<cf::meter_t>::opted_in, "an analyzer");
