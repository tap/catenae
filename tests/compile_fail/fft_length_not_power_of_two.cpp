// Copyright 2026 Timothy Place and the catenae contributors
// The constant tables of a length that is not a power of two (12), against those of 16.
// Verifies CAT-FFT-004.
// Expect: catenae: an FFT length is a power of two from 4 to 65536
#include <cstddef>

#include "catenae/fft.h"

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t cf_length = 16;
#else
constexpr std::size_t cf_length = 12;
#endif

std::size_t cf_fft_length();
std::size_t cf_fft_length() {
    return catenae::fft_plan<float>::of<cf_length>().size();
}
