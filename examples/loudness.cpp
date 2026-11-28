/// @file loudness.cpp
/// @brief A programme metered by examples/loudness.h: Tech 3341's 1 kHz tone, five seconds at
/// -23 dBFS and five at -13. The meters' raw powers and peaks leave the audio thread through a
/// mailbox, and the reader turns them into LUFS and dB TP (guide: Loudness and true peak).
// Copyright 2026 Timothy Place and the catenae contributors
#include "loudness.h"

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
    struct programme_reading {
        example::loudness_t::result_type  loudness; // three powers
        example::true_peak_t::result_type peak;     // each channel's
    };
    // ANCHOR_END: reading

    CATENAE_CONSTINIT catenae::memory_of<example::programme_meters_t> s_programme{};
    CATENAE_CONSTINIT catenae::mailbox<programme_reading> s_readings{};

    /// The audio thread, after each callback: post both meters' results when there are new ones.
    void post_new_results(std::uint32_t& seen) noexcept {
        const auto& chain = example::programme_meters;
        const auto& st    = s_programme.state;
        const auto  ready = chain.stage<0>().results_ready(example::programme_meters_t::stage_state<0>(st));
        if (ready == seen) {
            return;
        }
        seen = ready;
        s_readings.post({chain.stage<0>().result(example::programme_meters_t::stage_state<0>(st)),
                         chain.stage<1>().result(example::programme_meters_t::stage_state<1>(st))});
    }

    // ANCHOR: read
    /// The reader, on its own thread: LUFS and dB TP, to a tenth as Tech 3341 displays them.
    void print_newest() noexcept {
        static_cast<void>(s_readings.take([](const programme_reading& r) {
            const float peak = r.peak[0] > r.peak[1] ? r.peak[0] : r.peak[1];
            std::printf("momentary %.1f LUFS, short-term %.1f LUFS, integrated %.1f LUFS; true peak %.1f dBTP\n",
                        static_cast<double>(example::loudness_t::lufs(r.loudness.momentary)),
                        static_cast<double>(example::loudness_t::lufs(r.loudness.short_term)),
                        static_cast<double>(example::loudness_t::lufs(r.loudness.integrated)),
                        static_cast<double>(example::true_peak_t::dbtp(peak)));
        }));
    }
    // ANCHOR_END: read

    /// Five seconds of a 1 kHz sine at `dbfs` peak on both channels, in callbacks of 64 frames.
    void play(float dbfs, std::uint32_t& seen, std::size_t& frame) noexcept {
        const float               amplitude = std::pow(10.0f, dbfs / 20.0f);
        std::array<float, 64 * 2> block{};
        for (std::size_t at = 0; at < 240000; at += 64) {
            for (std::size_t f = 0; f < 64; ++f, ++frame) {
                // The phase of 48 frames' period, so minutes of it stay exact in float.
                const float v    = amplitude * std::sin(6.2831853f * static_cast<float>(frame % 48) / 48.0f);
                block[2 * f]     = v;
                block[2 * f + 1] = v;
            }
            static_cast<void>(example::programme_meters.process(s_programme, block, block)); // in place
            post_new_results(seen);
        }
    }

} // namespace

int main() {
    example::programme_meters.reset(s_programme.state);
    std::uint32_t seen  = 0;
    std::size_t   frame = 0;
    play(-23.0f, seen, frame);
    print_newest();
    play(-13.0f, seen, frame); // ten dB louder: the integrated value holds the programme so far
    print_newest();
    std::printf("CATENAE_EXAMPLE_DONE ok=1\n");
    return 0;
}
