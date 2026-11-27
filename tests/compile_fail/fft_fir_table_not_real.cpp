// Copyright 2026 Timothy Place and the catenae contributors
// A constant FFT FIR design from a spectrum whose DC bin is not real, as no real filter's is:
// from_table's check fails while the constant is evaluated, and check_failed is not constexpr.
// Verifies CAT-FFT-FIR-DESIGN-002.
// Expect: check_failed
#include <array>

#include "catenae/fft_fir_design.h"
#include "catenae/sample_format.h"

namespace cf {
    using design = catenae::fft_fir_design<float, 3, 2>;
#ifdef CATENAE_CF_CONTROL
    constexpr std::array<catenae::complex<float>, 3> k_bins = {{{0.5f, 0.0f}, {0.25f, -0.25f}, {0.0f, 0.0f}}};
#else
    constexpr std::array<catenae::complex<float>, 3> k_bins = {{{0.5f, 0.125f}, {0.25f, -0.25f}, {0.0f, 0.0f}}};
#endif
    constexpr design k_design = design::from_table(k_bins);
} // namespace cf

static_assert(cf::k_design.table()[1].re > 0.0f, "a real filter's spectrum");
