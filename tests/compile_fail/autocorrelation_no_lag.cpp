// Copyright 2026 Timothy Place and the catenae contributors
// An autocorrelation of lag 0 alone, which is rms_meter.
// Verifies CAT-AUTOCORRELATION-003.
// Expect: catenae: an autocorrelation has a lag above 0
#include "catenae/autocorrelation.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_lag = 1;
#else
    constexpr std::size_t k_lag = 0;
#endif
    using meter_t = catenae::autocorrelation<1, k_lag, 64>;
} // namespace cf

static_assert(catenae::analyzer_traits<cf::meter_t>::opted_in, "an analyzer");
