// Copyright 2026 Timothy Place and the catenae contributors
// An inverse STFT whose hop is its whole frame: no sample is in two frames, and a window that is
// 0 at n = 0 leaves a zero sum of squares.
// Verifies CAT-STFT-004.
// Expect: catenae: an STFT's frame is a power of two from 16 to 65536, its hop dividing it and at most half
#include "catenae/stft.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_hop = 256;
#else
    constexpr std::size_t k_hop = 512;
#endif
    using stage_t = catenae::istft<512, k_hop, 1>;
} // namespace cf

static_assert(catenae::stage_traits<cf::stage_t>::opted_in, "a stage");
