/// @file planar.cpp
/// @brief A mono strip over every channel of a port by planar_section, beside the interleaved
/// strip of the same stages at the port's channel count (guide: Channels).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/planar_section.h"

namespace {

    // ANCHOR: mono
    // A strip written once, for one channel: a gain that ramps over 96 frames, then a delay.
    constexpr auto mono_strip = catenae::gain<1>{0.5f, 96} | catenae::delay<1, 32>{};
    // ANCHOR_END: mono

    // ANCHOR: section
    // The same strip over each channel of a six-channel port: one configuration, six states.
    constexpr auto six_from_mono = catenae::chain(catenae::planar<6>(mono_strip));
    // ANCHOR_END: section

    // ANCHOR: interleaved
    // The strip written for six channels, interleaved: catenae's native layout (D5).
    constexpr auto six_interleaved = catenae::gain<6>{0.5f, 96} | catenae::delay<6, 32>{};
    // ANCHOR_END: interleaved

    using section_t     = std::remove_cv_t<decltype(six_from_mono)>;
    using interleaved_t = std::remove_cv_t<decltype(six_interleaved)>;

    CATENAE_CONSTINIT catenae::memory_of<section_t> s_section{};
    CATENAE_CONSTINIT catenae::memory_of<interleaved_t> s_interleaved{};

    constexpr std::size_t k_calls = 32;

    CATENAE_CONSTINIT std::array<float, 64 * 6> s_in{};
    CATENAE_CONSTINIT std::array<float, 64 * 6> s_a{};
    CATENAE_CONSTINIT std::array<float, 64 * 6> s_b{};

} // namespace

int main() {
    six_from_mono.reset(s_section.state);
    six_interleaved.reset(s_interleaved.state);
    std::uint32_t seed = 0x2545F491u;
    bool          same = true;
    for (std::size_t call = 0; call < k_calls; ++call) {
        for (float& x : s_in) {
            seed ^= seed << 13u;
            seed ^= seed >> 17u;
            seed ^= seed << 5u;
            x = static_cast<float>(seed / 256u) / 16777216.0f - 0.5f;
        }
        const std::size_t a = six_from_mono.process(s_section, s_in, s_a);
        const std::size_t b = six_interleaved.process(s_interleaved, s_in, s_b);
        same                = same && a == 64 && b == 64 && std::memcmp(s_a.data(), s_b.data(), sizeof s_a) == 0;
    }
    std::printf("six channels from the mono strip, against the six-channel strip: %s\n",
                same ? "the same, bit for bit" : "different");
    std::printf("latency %.0f frames either way; a voice %zu bytes from the mono strip, %zu interleaved\n",
                static_cast<double>(six_from_mono.latency().nominal), sizeof(catenae::state_of<section_t>),
                sizeof(catenae::state_of<interleaved_t>));
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", same ? 1 : 0);
    return same ? 0 : 1;
}
