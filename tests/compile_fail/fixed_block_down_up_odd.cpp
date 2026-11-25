// Copyright 2026 Timothy Place and the catenae contributors
// Down by 2 then up by 2 (input group 2) given a fixed block of 63 frames.
// Verifies CAT-CHAIN-004.
// Expect: a fixed block must be a whole number of the chain's input groups; wrap the chain in steady
#include <array>

#include "catenae/chain.h"
#include "catenae_fixtures/stages.h"

using catenae::operator|;
constexpr auto c = catenae_fixtures::down2_first<1>{} | catenae_fixtures::up2_toy<1>{};

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t k_frames = 64;
#else
constexpr std::size_t k_frames = 63;
#endif

std::size_t odd_block(catenae::memory_of<decltype(c)>& m, const std::array<float, k_frames>& in,
                      std::array<float, k_frames + 2>& out) noexcept;
std::size_t odd_block(catenae::memory_of<decltype(c)>& m, const std::array<float, k_frames>& in,
                      std::array<float, k_frames + 2>& out) noexcept {
    return c.process(m, in, out);
}
