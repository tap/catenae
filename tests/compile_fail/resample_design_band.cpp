// Copyright 2026 Timothy Place and the catenae contributors
// A resample by 3 given a half-band: its design is the third-band filter of its ratio.
// Verifies CAT-RESAMPLE-004.
// Expect: design is the max\(L, M\)-th-band filter
#include "catenae/resample.h"
#include "catenae_test/nyquist_test_tables.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage = catenae::resample<1, catenae::ratio_t<1, 3>, catenae::nyquist_design<3, 2, 70>>;
#else
    using stage = catenae::resample<1, catenae::ratio_t<1, 3>, catenae::half_band<1>>;
#endif
    static_assert(catenae::stage_traits<stage>::opted_in, "a stage");
} // namespace cf
