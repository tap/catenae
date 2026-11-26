// Copyright 2026 Timothy Place and the catenae contributors
// A q15 plan before the fixed-point engine (C6), against a float plan.
// Verifies CAT-FFT-007.
// Expect: catenae: the fixed-point FFT engine arrives at C6
#include <cstddef>

#include "catenae/fft.h"
#include "catenae/sample_format.h"

#ifdef CATENAE_CF_CONTROL
using cf_format = float;
#else
using cf_format = catenae::q15;
#endif

std::size_t cf_fft_length();
std::size_t cf_fft_length() {
    return catenae::fft_plan<cf_format>::of<512>().size();
}
