// Copyright 2026 Timothy Place and the catenae contributors
// PLAN.md 9.5's first refusal: an STFT's spectrum into a time-domain gain.
// Verifies CAT-STFT-004.
// Expect: catenae: a spectrum cannot feed a time-domain port
#include "catenae/gain.h"
#include "catenae/stft.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    constexpr auto k_chain = catenae::stft<512, 128, 1>{catenae::window::hann}
                             | catenae::istft<512, 128, 1>{catenae::window::hann} | catenae::gain<1>{0.5f, 96};
#else
    constexpr auto k_chain = catenae::stft<512, 128, 1>{catenae::window::hann} | catenae::gain<1>{0.5f, 96};
#endif
} // namespace cf

static_assert(sizeof(cf::k_chain) > 0, "a chain");
