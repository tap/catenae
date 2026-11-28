// Copyright 2026 Timothy Place and the catenae contributors
// PLAN.md 9.5's second refusal: an STFT region of hop 128 given fixed blocks of 64.
// Verifies CAT-STFT-004 and CAT-CHAIN-004.
// Expect: a fixed block must be a whole number of the chain's input groups; wrap the chain in steady
#include <array>

#include "catenae/chain.h"
#include "catenae/stft.h"

namespace cf {
    constexpr auto k_region =
        catenae::stft<512, 128, 1>{catenae::window::hann} | catenae::istft<512, 128, 1>{catenae::window::hann};

#ifdef CATENAE_CF_CONTROL
    constexpr std::size_t k_frames = 128;
#else
    constexpr std::size_t k_frames = 64;
#endif

    std::size_t half_hop(catenae::memory_of<decltype(k_region)>& m, const std::array<float, k_frames>& in,
                         std::array<float, k_frames>& out) noexcept;
    std::size_t half_hop(catenae::memory_of<decltype(k_region)>& m, const std::array<float, k_frames>& in,
                         std::array<float, k_frames>& out) noexcept {
        return k_region.process(m, in, out);
    }
} // namespace cf
