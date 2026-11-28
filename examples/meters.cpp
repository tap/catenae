/// @file meters.cpp
/// @brief A stereo bus metered by examples/meters.h: the meters' raw results leave the audio
/// thread through a mailbox, and the reader turns them into decibels (guide: Meters).
// Copyright 2026 Timothy Place and the catenae contributors
#include "meters.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "catenae/compiler.h"
#include "catenae/mailbox.h"

namespace {

    // ANCHOR: reading
    /// What the audio thread posts: the meters' raw results, copied out of the chain's state.
    struct bus_reading {
        example::peak_t::result_type        peak;
        example::rms_t::result_type         energy;
        example::ppm_t::result_type         ppm;
        example::correlation_t::result_type correlation;
    };
    // ANCHOR_END: reading

    CATENAE_CONSTINIT catenae::memory_of<example::bus_meters_t> s_bus{};
    CATENAE_CONSTINIT catenae::mailbox<bus_reading> s_readings{};

    // ANCHOR: post
    /// The audio thread, after each callback: when the RMS meter has a new result (all four
    /// complete their hops together, as meters.h asserts), post them all. Nothing here converts
    /// or waits.
    void post_new_results(std::uint32_t& seen) noexcept {
        const auto& chain = example::bus_meters;
        const auto& st    = s_bus.state;
        const auto  ready = chain.stage<1>().results_ready(example::bus_meters_t::stage_state<1>(st));
        if (ready == seen) {
            return;
        }
        seen = ready;
        s_readings.post({chain.stage<0>().result(example::bus_meters_t::stage_state<0>(st)),
                         chain.stage<1>().result(example::bus_meters_t::stage_state<1>(st)),
                         chain.stage<2>().result(example::bus_meters_t::stage_state<2>(st)),
                         chain.stage<3>().result(example::bus_meters_t::stage_state<3>(st))});
    }
    // ANCHOR_END: post

    // ANCHOR: read
    /// The reader, on its own thread: decibels and the correlation at lag 0.
    void print_newest() noexcept {
        static_cast<void>(s_readings.take([](const bus_reading& r) {
            std::printf("left: peak %.2f dBFS, RMS %.2f dBFS, PPM %.2f dBFS; correlation %+.2f\n",
                        static_cast<double>(example::peak_t::dbfs(r.peak[0])),
                        static_cast<double>(example::rms_t::dbfs(r.energy[0])),
                        static_cast<double>(example::ppm_t::dbfs(r.ppm[0])),
                        static_cast<double>(example::correlation_t::normalized(r.correlation, 0)));
        }));
    }
    // ANCHOR_END: read

    /// Half a second of a 1 kHz sine at half of full scale, right as left times `right`.
    void play(float right, std::uint32_t& seen) noexcept {
        std::array<float, 64 * 2> block{};
        for (std::size_t at = 0; at < 24000; at += 64) {
            for (std::size_t f = 0; f < 64; ++f) {
                const float v    = 0.5f * std::sin(6.2831853f * 1000.0f * static_cast<float>(at + f) / 48000.0f);
                block[2 * f]     = v;
                block[2 * f + 1] = right * v;
            }
            static_cast<void>(example::bus_meters.process(s_bus, block, block)); // in place
            post_new_results(seen);
        }
    }

} // namespace

int main() {
    example::bus_meters.reset(s_bus.state);
    std::uint32_t seen = 0;
    play(1.0f, seen); // the same on both channels
    print_newest();
    play(-1.0f, seen); // the right inverted
    print_newest();
    std::printf("CATENAE_EXAMPLE_DONE ok=1\n");
    return 0;
}
