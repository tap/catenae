// Copyright 2026 Timothy Place and the catenae contributors
// The constant tables of 131072, one length above the range, against those of 65536.
// Verifies CAT-FFT-004.
// Expect: catenae: an FFT length is a power of two from 4 to 65536
#include <cstddef>

#include "catenae/fft.h"

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t cf_length = 65536;
#else
constexpr std::size_t cf_length = 131072;
#endif

std::size_t cf_fft_length();
std::size_t cf_fft_length() {
    return catenae::fft_plan<double>::of<cf_length>().size();
}
