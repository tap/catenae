// Copyright 2026 Timothy Place and the catenae contributors
// An RMS meter whose window of 100 frames is not a whole number of its 64-frame hops.
// Verifies CAT-RMS-METER-004, CAT-PEAK-METER-004, CAT-AUTOCORRELATION-003.
// Expect: catenae: a meter's window is a whole number of its hops
#include "catenae/rms_meter.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_window = 128;
#else
    constexpr std::size_t k_window = 100;
#endif
    using meter_t = catenae::rms_meter<1, 64, k_window>;
} // namespace cf

static_assert(catenae::analyzer_traits<cf::meter_t>::opted_in, "an analyzer");
