// Copyright 2026 Timothy Place and the catenae contributors
// An FFT FIR of 34 taps in blocks of 32: overlap-save keeps the last Block samples of each window
// of 2 Block, which are the linear convolution only while Block is at least Taps - 1.
// Verifies CAT-FFT-FIR-DESIGN-003.
// Expect: catenae: an FFT FIR's block is a power of two from 2 to 32,768, at least Taps - 1
#include "catenae/fft_fir.h"
#include "catenae/stage_traits.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage_t = catenae::fft_fir<1, 33, 32>;
#else
    using stage_t = catenae::fft_fir<1, 34, 32>;
#endif
    static_assert(catenae::stage_traits<stage_t>::input_group == 32, "a block of 32");
} // namespace cf
