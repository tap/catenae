// Copyright 2026 Timothy Place and the catenae contributors
// An STFT of 512-sample frames every 100 samples: the hop does not divide the frame.
// Verifies CAT-STFT-004.
// Expect: catenae: an STFT's frame is a power of two from 16 to 65536, its hop dividing it
#include "catenae/stft.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_hop = 128;
#else
    constexpr std::size_t k_hop = 100;
#endif
    using stage_t = catenae::stft<512, k_hop, 1>;
} // namespace cf

static_assert(catenae::stage_traits<cf::stage_t>::opted_in, "a stage");
