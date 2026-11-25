/// @file threads_and_clocks.cpp
/// @brief A level posted from a control thread through a mailbox, and stereo frames handed from a
/// producer thread to a consumer thread through a handoff (guide: Threads and clocks). Hosted
/// builds with threads.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <thread>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/delay.h"
#include "catenae/fed_by.h"
#include "catenae/feeding.h"
#include "catenae/gain.h"
#include "catenae/handoff.h"
#include "catenae/mailbox.h"

namespace {

    // ANCHOR: mailbox
    // The strip, and a mailbox for its level. Both static, both built by constant initialisation.
    CATENAE_CONSTINIT auto s_strip = catenae::chain(catenae::gain<2>{1.0f, 96});
    CATENAE_CONSTINIT catenae::memory_of<decltype(s_strip)> s_strip_memory{};
    CATENAE_CONSTINIT catenae::mailbox<float> s_new_level;

    // Control thread: post the value. It never touches the chain.
    void change_the_level(float level) noexcept {
        s_new_level.post(level);
    }

    // Audio thread, at the top of its callback: collect, apply, process.
    void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
        static_cast<void>(s_new_level.take([](const float& level) noexcept { s_strip.stage<0>().set_level(level); }));
        static_cast<void>(s_strip.process(s_strip_memory, in, out));
    }
    // ANCHOR_END: mailbox

    // ANCHOR: junction
    // The producer's thread: a chain whose output goes into the junction, 64 frames a call.
    constexpr auto input = catenae::gain<2>{0.5f, 0} | catenae::delay<2, 16>{};

    // The junction: a configuration with no data, and a state at rest as it stands. It is fed 64
    // frames at a time and serves requests of up to 256, so its target is 320 (256 + 64 - 1,
    // rounded up): it fills to 320 frames before it pays out, and 320 frames is its latency.
    constexpr catenae::handoff<float, 2, 1024, 320, 64> link{};
    CATENAE_CONSTINIT catenae::state_of<decltype(link)> s_link{};
    CATENAE_CONSTINIT catenae::feeding_state<decltype(input)> s_into_link{};
    CATENAE_CONSTINIT catenae::feeding_workspace<decltype(input)> s_into_link_work{};

    // The consumer's thread, the same clock: requests of up to 256 frames, each at most one pull.
    constexpr auto    output = catenae::chain(catenae::gain<2>{2.0f, 0});
    CATENAE_CONSTINIT catenae::fed_by_state<decltype(output)> s_from_link{};
    CATENAE_CONSTINIT catenae::fed_by_workspace<decltype(output), 256> s_from_link_work{};
    // ANCHOR_END: junction

} // namespace

int main() {
    bool ok = true;

    // The mailbox: a control thread posts levels while the audio thread runs its callbacks.
    s_strip.reset(s_strip_memory.state);
    std::atomic<bool>         posted{false};
    std::thread               control([&posted] {
        for (int i = 1; i <= 1000; ++i) {
            change_the_level(static_cast<float>(i) / 1000.0f);
        }
        posted.store(true);
    });
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    in.fill(1.0f);
    while (!posted.load()) {
        audio_callback(in, out);
    }
    control.join();
    audio_callback(in, out); // collects the last post, if it came after the last look
    ok = ok && s_strip.stage<0>().level() > 0.999f;

    // The junction: one thread feeds numbered frames in, another reads them out.
    input.reset(s_into_link);
    reset(output, s_from_link);
    constexpr std::size_t      k_frames = 64 * 4000;
    std::atomic<bool>          fed{false};
    std::thread                producer([&fed] {
        std::array<float, 64 * 2> block{};
        for (std::size_t next = 1; next < k_frames; next += 64) {
            for (std::size_t i = 0; i < 64; ++i) {
                block[2 * i]     = static_cast<float>(next + i);
                block[2 * i + 1] = -block[2 * i];
            }
            static_cast<void>(catenae::feeding(link, s_link, input, s_into_link, s_into_link_work,
                                                              catenae::span<const float>(block)));
            std::this_thread::yield();
        }
        fed.store(true);
    });
    std::array<float, 256 * 2> got{};
    float                      last  = 0.0f;
    std::size_t                real  = 0;
    std::size_t                wrong = 0;
    std::size_t                size  = 37;
    while (!fed.load()) {
        size = size * 7 % 256 + 1; // requests of varied sizes, up to 256
        static_cast<void>(catenae::fed_by(link, s_link, output, s_from_link, s_from_link_work,
                                          catenae::span<float>(got.data(), size * 2)));
        for (std::size_t i = 0; i < size; ++i) {
            if (got[2 * i] > 0.0f) { // a real frame: later than the one before, and whole
                wrong += got[2 * i] > last && !(-got[2 * i + 1] < got[2 * i] || -got[2 * i + 1] > got[2 * i]) ? 0u : 1u;
                last = got[2 * i];
                ++real;
            }
        }
    }
    producer.join();
    ok = ok && real > 0 && wrong == 0;
    std::printf("level %.3f; %zu frames through the junction, %zu out of order or torn; %zu input frames starved\n",
                static_cast<double>(s_strip.stage<0>().level()), real, wrong, s_from_link.starved);
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
