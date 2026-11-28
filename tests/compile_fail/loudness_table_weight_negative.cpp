// Copyright 2026 Timothy Place and the catenae contributors
// A constant loudness meter from BS.1770-4's 48 kHz table with a negative weight: from_table's
// check fails while the constant is evaluated, and check_failed is not constexpr.
// Verifies CAT-LOUDNESS-004.
// Expect: check_failed
#include "catenae/biquad_design.h"
#include "catenae/loudness_meter.h"

namespace cf {
    using meter_t = catenae::loudness_meter<2, 960, 5>;
    using design  = meter_t::design_type;
#ifdef CATENAE_CF_CONTROL
    constexpr float k_right = 1.0f;
#else
    constexpr float k_right = -1.0f;
#endif
    constexpr design k_k_weighting = design::from_table(
        {{{1.53512485958697f, -2.69169618940638f, 1.19839281085285f, -1.69065929318241f, 0.73248077421585f},
          {1.0f, -2.0f, 1.0f, -1.99004745483398f, 0.99007225036621f}}},
        {0.0f, 0.0f, 0, 0, 0.0f});
    constexpr meter_t k_meter = meter_t::from_table(k_k_weighting, {1.0f, k_right});
} // namespace cf

static_assert(cf::k_meter.weights()[0] > 0.0f, "a weighed channel");
