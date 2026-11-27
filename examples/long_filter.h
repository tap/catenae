/// @file long_filter.h
/// @brief A long low-pass in a callback (guide: Long filters): the 255-tap design of
/// examples/fft_fir_tables.h frame by frame, and by fast convolution in blocks of 256, made
/// frame-synchronous for a callback of any size by steady<>.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_EXAMPLES_LONG_FILTER_H
#define CATENAE_EXAMPLES_LONG_FILTER_H

#include <type_traits>

#include "catenae/chain.h"
#include "catenae/fft_fir.h"
#include "catenae/fir.h"
#include "catenae/steady.h"
#include "fft_fir_tables.h"

namespace example {

    // ANCHOR: direct
    /// What one would naturally write: 255 taps on every frame of both channels.
    inline constexpr auto long_direct = catenae::chain(catenae::fir<2, 255>{long_lowpass});
    using long_direct_t               = std::remove_cv_t<decltype(long_direct)>;
    // ANCHOR_END: direct

    // ANCHOR: blocks
    /// The same filter by fast convolution: a stage that works in blocks of 256 frames.
    inline constexpr catenae::fft_fir<2, 255> long_blocks{long_lowpass_fft};
    /// In a chain for a callback of any size: steady<> evens out the blocks, 255 frames later.
    inline constexpr auto long_filter = catenae::chain(catenae::steady<catenae::fft_fir<2, 255>>{long_blocks});
    using long_filter_t               = std::remove_cv_t<decltype(long_filter)>;
    // ANCHOR_END: blocks

} // namespace example

#endif // CATENAE_EXAMPLES_LONG_FILTER_H
