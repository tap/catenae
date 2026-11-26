// Copyright 2026 Timothy Place and the catenae contributors
// A plan of 512 from its constant tables given a std::array of 256 bins, against one of 257.
// Verifies CAT-FFT-008.
// Expect: catenae: an FFT frame of length N holds N/2 \+ 1 bins
#include <array>
#include <cstddef>

#include "catenae/fft.h"
#include "catenae/sample_format.h"

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t cf_bins = 257;
#else
constexpr std::size_t cf_bins = 256;
#endif

void cf_fft_frame(std::array<catenae::complex<float>, cf_bins>& frame);
void cf_fft_frame(std::array<catenae::complex<float>, cf_bins>& frame) {
    catenae::fft_plan<float>::of<512>().forward(frame);
}
