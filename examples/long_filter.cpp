/// @file long_filter.cpp
/// @brief A 255-tap low-pass in a callback, frame by frame and by fast convolution, and what the
/// blocks change: when the output arrives, and how late (guide: Long filters).
// Copyright 2026 Timothy Place and the catenae contributors
#include "long_filter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/stage_traits.h"

namespace {

    using example::long_filter;
    using example::long_filter_t;

    // ANCHOR: callback
    // The memory: a voice's state and the thread's workspace, plain storage in .bss.
    CATENAE_CONSTINIT catenae::state_of<long_filter_t> s_voice{};
    CATENAE_CONSTINIT catenae::workspace_of<long_filter_t> s_work{};

    void reset_voice() noexcept {
        long_filter.reset(s_voice);
    } // once, before the first callback

    void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
        static_cast<void>(long_filter.process(s_voice, s_work, in, out)); // 64 frames out, every call
    }
    // ANCHOR_END: callback

    using direct_t = example::long_direct_t;
    CATENAE_CONSTINIT catenae::memory_of<direct_t> s_direct{};

    using blocks_t = catenae::fft_fir<2, 255>;
    CATENAE_CONSTINIT blocks_t::state s_blocks{};
    CATENAE_CONSTINIT blocks_t::workspace s_blocks_work{};

    constexpr std::size_t k_calls = 64; // 16 blocks of 256

    /// Stereo noise in [-0.5, 0.5): the same on every target.
    float next_input(std::uint32_t& s) noexcept {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return static_cast<float>(s / 256u) / 16777216.0f - 0.5f;
    }

    CATENAE_CONSTINIT std::array<std::array<float, 64 * 2>, k_calls> s_in{};
    CATENAE_CONSTINIT std::array<std::array<float, 64 * 2>, k_calls> s_steady{};
    CATENAE_CONSTINIT std::array<std::array<float, 64 * 2>, k_calls> s_straight{};
    CATENAE_CONSTINIT std::array<float, 64 * 2 * k_calls> s_bare{};

} // namespace

int main() {
    std::uint32_t seed = 0x2545F491u;
    for (auto& block : s_in) {
        for (float& x : block) {
            x = next_input(seed);
        }
    }

    // The block stage alone, in calls of 64: three write nothing, the fourth a block of 256.
    std::array<std::size_t, 4> written{};
    std::size_t                bare = 0;
    for (std::size_t call = 0; call < k_calls; ++call) {
        const std::size_t w =
            example::long_blocks.run(s_blocks, s_blocks_work, catenae::span<const float>(s_in[call]),
                                     catenae::span<float>(s_bare.data() + bare * 2, s_bare.size() - bare * 2));
        written[call % 4] = call < 4 ? w : written[call % 4];
        bare += w;
    }
    std::printf("blocks of 256 in calls of 64: %zu, %zu, %zu and %zu frames\n", written[0], written[1], written[2],
                written[3]);

    // In steady<>, 64 a call; and the direct form beside it.
    reset_voice();
    example::long_direct.reset(s_direct.state);
    bool even = true;
    for (std::size_t call = 0; call < k_calls; ++call) {
        audio_callback(s_in[call], s_steady[call]);
        even = example::long_direct.process(s_direct, s_in[call], s_straight[call]) == 64 && even;
    }
    using bare_traits   = catenae::stage_traits<blocks_t>;
    using steady_traits = catenae::stage_traits<long_filter_t>;
    std::printf("in steady<>: 64 a call; latency %u frames bare, %u in steady<>\n",
                static_cast<unsigned>(bare_traits::exact_latency.up),
                static_cast<unsigned>(steady_traits::exact_latency.up));

    // The two forms are one filter: the block stage's output beside the direct form's, frame for
    // frame, and steady<>'s the same 255 frames later, bit for bit.
    float       largest = 0.0f;
    bool        delayed = true;
    std::size_t frame   = 0;
    for (std::size_t call = 0; call < k_calls; ++call) {
        for (std::size_t i = 0; i < 64 * 2; ++i, ++frame) {
            largest              = std::max(largest, std::fabs(s_bare[frame] - s_straight[call][i]));
            const std::size_t at = frame >= 255 * 2 ? frame - 255 * 2 : 0;
            const float       b  = frame >= 255 * 2 ? s_bare[at] : 0.0f;
            delayed              = delayed && !(s_steady[call][i] < b) && !(s_steady[call][i] > b);
        }
    }
    std::printf("against the direct form: largest difference %.1e; steady<> the same, 255 frames later: %s\n",
                static_cast<double>(largest), delayed ? "yes" : "no");
    std::printf("memory: a voice %zu bytes and the workspace %zu; the direct form's voice %zu\n",
                sizeof(catenae::state_of<long_filter_t>), sizeof(catenae::workspace_of<long_filter_t>),
                sizeof(catenae::state_of<direct_t>));

    const bool ok = even && delayed && written[3] == 256 && written[0] == 0 && largest < 1e-5f && bare == 64 * k_calls;
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
