// Copyright 2026 Timothy Place and the catenae contributors
// A fixed output block one frame short of what a 1:1 chain writes for its input block (the
// guide's Stages and chains quotes this message).
// Verifies CAT-CHAIN-004.
// Expect: catenae: a fixed output block holds max_outputs_for of the input block
#include <array>
#include <cstddef>

#include "catenae/gain.h"

constexpr auto c = catenae::chain(catenae::gain<2>{1.0f, 0});

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t k_out_frames = 64;
#else
constexpr std::size_t k_out_frames = 63;
#endif

std::size_t short_out(catenae::memory_of<decltype(c)>& m, const std::array<float, 64 * 2>& in,
                      std::array<float, k_out_frames * 2>& out) noexcept;
std::size_t short_out(catenae::memory_of<decltype(c)>& m, const std::array<float, 64 * 2>& in,
                      std::array<float, k_out_frames * 2>& out) noexcept {
    return c.process(m, in, out);
}
