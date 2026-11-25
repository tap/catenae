// Copyright 2026 Timothy Place and the catenae contributors
// A block stage of 128 frames given a fixed block of 64, half its hop.
// Verifies CAT-CHAIN-004.
// Expect: a fixed block must be a whole number of the chain's input groups; wrap the chain in steady
#include <array>

#include "catenae/chain.h"
#include "catenae_fixtures/stages.h"

constexpr auto c = catenae::chain(catenae_fixtures::block_stage<1, 128>{});

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t k_frames = 128;
#else
constexpr std::size_t k_frames = 64;
#endif

std::size_t half_hop(catenae::memory_of<decltype(c)>& m, const std::array<float, k_frames>& in,
                     std::array<float, 256>& out) noexcept;
std::size_t half_hop(catenae::memory_of<decltype(c)>& m, const std::array<float, k_frames>& in,
                     std::array<float, 256>& out) noexcept {
    return c.process(m, in, out);
}
