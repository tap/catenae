/// @file bursts_and_buffering.cpp
/// @brief A stage that works in blocks, bare and inside steady<>, call by call (guide: Bursts
/// and buffering). Its trace is what scripts/figure_steady.py draws guide/src/figures/steady.svg from.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "block_region.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/fed_by.h"
#include "catenae/handoff.h"
#include "catenae/steady.h"

namespace {

    // ANCHOR: bare
    constexpr auto region = catenae::chain(example::block_region<128>{});
    using region_t        = std::remove_cv_t<decltype(region)>;

    static_assert(region_t::input_group == 128, "owed exactly only in whole hops");
    static_assert(!region_t::is_frame_synchronous, "64 frames in give 0 or 128 out");
    static_assert(region_t::max_shortfall.up == 127 && region_t::max_shortfall.down == 1, "up to 127 behind");
    // ANCHOR_END: bare

    // ANCHOR: steady
    constexpr auto smooth = catenae::chain(catenae::steady{example::block_region<128>{}});
    using smooth_t        = std::remove_cv_t<decltype(smooth)>;

    static_assert(smooth_t::is_frame_synchronous, "n frames out for every n in");
    static_assert(catenae::steady<example::block_region<128>>::added_latency_frames == 127, "primed by 127");
    static_assert(smooth_t::exact_latency.up == 127 && smooth_t::exact_latency.down == 1, "and it says so");
    // ANCHOR_END: steady

    // ANCHOR: callback
    CATENAE_CONSTINIT catenae::memory_of<smooth_t> s_smooth{};

    // A host that calls with 64 frames, half a hop: the bare region would not compile here.
    void audio_callback(const std::array<float, 64>& in, std::array<float, 64>& out) noexcept {
        static_cast<void>(smooth.process(s_smooth, in, out));
    }
    // ANCHOR_END: callback

    CATENAE_CONSTINIT catenae::memory_of<region_t> s_region{};

    // ANCHOR: pull
    // The same region on the far side of a junction, whose producer pushes 64 frames at a time.
    // The pull side fills each 64-frame request exactly, with at most one pull: what the region
    // writes beyond the request waits in its holdover for the next one.
    constexpr catenae::handoff<float, 1, 1024, 256, 64> link{};
    CATENAE_CONSTINIT catenae::state_of<decltype(link)> s_link{};
    CATENAE_CONSTINIT catenae::fed_by_state<region_t> s_pull{};
    CATENAE_CONSTINIT catenae::fed_by_workspace<region_t, 64> s_pull_work{};

    static_assert(catenae::fed_by_workspace<region_t, 64>::largest_pull == 191, "64, and the 127 it can trail by");
    static_assert(decltype(link)::serves(191, 1), "the target is at least 191 + 64 - 1");
    // ANCHOR_END: pull

} // namespace

int main() {
    region.reset(s_region.state);
    smooth.reset(s_smooth.state);

    std::array<float, 64>             in{};
    std::array<float, 128>            burst{};
    std::array<float, 64>             out{};
    static std::array<float, 8 * 128> bare_all{};
    static std::array<float, 8 * 64>  smooth_all{};
    std::size_t                       bare_total = 0;
    bool                              ok         = true;

    std::printf("call,frames_in,bare_out,fifo_held,steady_out\n");
    for (std::size_t call = 1; call <= 8; ++call) {
        for (std::size_t i = 0; i < 64; ++i) {
            in[i] = static_cast<float>((call - 1) * 64 + i + 1); // the frame's own number
        }
        const std::size_t bare_out =
            region.process(s_region, catenae::span<const float>(in), catenae::span<float>(burst));
        for (std::size_t i = 0; i < bare_out; ++i) {
            bare_all[bare_total + i] = burst[i];
        }
        bare_total += bare_out;
        audio_callback(in, out);
        for (std::size_t i = 0; i < 64; ++i) {
            smooth_all[(call - 1) * 64 + i] = out[i];
        }
        const std::size_t held = smooth_t::stage_state<0>(s_smooth.state).held;
        std::printf("%u,64,%u,%u,64\n", static_cast<unsigned>(call), static_cast<unsigned>(bare_out),
                    static_cast<unsigned>(held));
        ok = ok && bare_out == (call % 2 == 0 ? 128u : 0u) && held == (call % 2 == 0 ? 127u : 63u);
    }
    // The steady form is the bare form 127 frames later, after silence.
    for (std::size_t i = 0; i < smooth_all.size(); ++i) {
        const float owed = i < 127 ? 0.0f : bare_all[i - 127];
        ok               = ok && !(smooth_all[i] < owed || smooth_all[i] > owed);
    }
    // The pull side: the junction holds its target, 256 frames; four requests of 64 follow, and
    // the fill it gives up to each is that request's pull.
    reset(region, s_pull);
    std::array<float, 64> pushed{};
    for (int i = 0; i < 4; ++i) {
        static_cast<void>(link.push(s_link, catenae::span<const float>(pushed)));
    }
    std::array<float, 64> request{};
    for (unsigned n = 1; n <= 4; ++n) {
        const std::uint32_t before = link.report(s_link).fill_frames;
        static_cast<void>(catenae::fed_by(link, s_link, region, s_pull, s_pull_work, catenae::span<float>(request)));
        const std::uint32_t pulled = before - link.report(s_link).fill_frames;
        std::printf("pull,%u,%u\n", n, static_cast<unsigned>(pulled));
        ok = ok && pulled == (n % 2 == 1 ? 128u : 0u);
    }
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
