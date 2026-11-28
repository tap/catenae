// Copyright 2026 Timothy Place and the catenae contributors
// A true-peak meter whose window of 100 frames is not a whole number of its 64-frame hops.
// Verifies CAT-TRUE-PEAK-004.
// Expect: catenae: a meter's window is a whole number of its hops
#include "catenae/true_peak_meter.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_window = 128;
#else
    constexpr std::size_t k_window = 100;
#endif
    using meter_t = catenae::true_peak_meter<1, 64, k_window>;
} // namespace cf

static_assert(catenae::analyzer_traits<cf::meter_t>::opted_in, "an analyzer");
