// Copyright 2026 Timothy Place and the catenae contributors
// A resample by 5: L and M are each 2^a 3^b, and 5 is neither.
// Verifies CAT-RESAMPLE-004.
// Expect: L and M are each of the form 2\^a 3\^b
#include "catenae/resample.h"
#include "catenae_test/nyquist_test_tables.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage = catenae::resample<1, catenae::ratio_t<3, 1>, catenae::nyquist_design<3, 2, 70>>;
#else
    using stage = catenae::resample<1, catenae::ratio_t<5, 1>, catenae::nyquist_design<3, 2, 70>>;
#endif
    static_assert(catenae::stage_traits<stage>::opted_in, "a stage");
} // namespace cf
