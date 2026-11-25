// Copyright 2026 Timothy Place and the catenae contributors
// Two threads through a handoff, and a third reading its report, for the thread sanitizer:
// a ramp goes in, and every real frame that comes out must be later than the one before it
// (frames are discarded at a start, never reordered or torn; what a full ring refuses is sent again).
// Throwaway; run by verify.sh.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "sketch.h"

[[noreturn]] void catenae::check_failed(int code) noexcept {
    std::printf("check %d failed\n", code);
    std::abort();
}

using namespace catenae;

int main() {
    constexpr handoff<2, 1024, 128, 64> link{};
    static state_of<decltype(link)>     s_state;
    link.reset(s_state);
    constexpr std::size_t k_frames = 3'000'000;
    std::atomic<bool>     done{false};

    std::thread producer([&]() noexcept {
        std::array<float, 64 * 2> block{};
        std::size_t               next = 1;
        while (next < k_frames) {
            for (std::size_t i = 0; i < 64; ++i) {
                block[2 * i]     = static_cast<float>(next + i);
                block[2 * i + 1] = -block[2 * i];
            }
            next += link.push(s_state, block); // what the ring had no room for is sent again
        }
        done.store(true);
    });
    std::thread watcher([&]() noexcept {
        std::size_t over = 0;
        while (!done.load()) {
            over += link.report(s_state).fill_frames > 1024 ? 1U : 0U;
        }
        if (over != 0) {
            std::printf("report showed a fill above the capacity %zu times\n", over);
            std::abort();
        }
    });

    std::array<float, 64 * 2> out{};
    float                     last  = 0.0f;
    std::size_t               real  = 0;
    std::size_t               torn  = 0;
    std::size_t               order = 0;
    std::size_t               pulls = 0;
    while (!done.load()) {
        const std::size_t frames = link.pull(s_state, out);
        for (std::size_t i = 0; i < frames; ++i) {
            torn += out[2 * i + 1] < -out[2 * i] || out[2 * i + 1] > -out[2 * i] ? 1U : 0U;
            order += out[2 * i] > last ? 0U : 1U;
            last = out[2 * i];
        }
        real += frames;
        if (++pulls % 5000 == 0) {
            link.restart(s_state); // the consumer's restart, while the producer runs
        }
    }
    producer.join();
    watcher.join();
    const handoff_report report = link.report(s_state);
    std::printf("%zu real frames, torn %zu, out of order %zu; %u underruns, %u overruns\n", real, torn, order, report.underruns,
                report.overruns);
    return torn == 0 && order == 0 ? 0 : 1;
}
