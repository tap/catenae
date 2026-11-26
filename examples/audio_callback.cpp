/// @file audio_callback.cpp
/// @brief A filter whose design is a table in ROM, a callback that cannot fail, and a redesign
/// made off the audio path that can (guide: The audio callback; PLAN.md 9.2).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <type_traits>

#include "catenae/biquad.h"
#include "catenae/biquad_design.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/config.h"
#include "catenae/expected.h"
#include "catenae/gain.h"
#include "flush_to_zero.h"
#include "lowpass_table.h"

namespace {

    // ANCHOR: strip
    // A configuration whose parameters will change is a variable. It starts from a design that is
    // a table in ROM: no designer has run, and none need be linked.
    CATENAE_CONSTINIT auto s_strip =
        catenae::chain(catenae::butterworth<2, 4>{example::lowpass_at_0_1}) | catenae::gain<2>{1.0f, 96};
    using strip_t = std::remove_cv_t<decltype(s_strip)>;
    CATENAE_CONSTINIT catenae::memory_of<strip_t> s_memory{};

    // The audio callback: 64 stereo frames in and out. Nothing here can fail, wait or allocate.
    void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
        static_cast<void>(s_strip.process(s_memory, in, out));
    }
    // ANCHOR_END: strip

    // ANCHOR: redesign
    // Off the audio path: design (this can fail), then hand the design over (this cannot).
    catenae::status retune(double cutoff) noexcept {
        const auto designed = catenae::butterworth_design<float, 4>::make(cutoff);
        if (!designed) {
            return designed.error(); // the callback keeps the design it has
        }
        s_strip.stage<0>().set_design(*designed); // between callbacks: a step under a live history
        return catenae::status{};
    }
    // ANCHOR_END: redesign

    /// The gain of the strip at a tone of `frequency` cycles per sample, from its last 32 frames
    /// of 640, when the filter has settled.
    float gain_at(double frequency) {
        s_strip.reset(s_memory.state);
        std::array<float, 64 * 2> in{};
        std::array<float, 64 * 2> out{};
        double                    peak = 0.0;
        for (std::size_t block = 0; block < 10; ++block) {
            for (std::size_t f = 0; f < 64; ++f) {
                const double phase = 6.283185307179586 * frequency * static_cast<double>(block * 64 + f);
                in[2 * f]          = static_cast<float>(std::sin(phase));
                in[2 * f + 1]      = in[2 * f];
            }
            audio_callback(in, out);
            for (std::size_t f = 32; f < 64 && block == 9; ++f) {
                const double v = std::fabs(static_cast<double>(out[2 * f]));
                peak           = v > peak ? v : peak;
            }
        }
        return static_cast<float>(peak);
    }

} // namespace

int main() {
#if CATENAE_FLUSH_TO_ZERO
    example::flush_subnormals_on_this_thread(); // once, before the first callback (AOU-008)
#endif
    const float passed  = gain_at(0.05);                     // through the table design, cutoff 0.1
    const bool  moved   = retune(0.02) == catenae::status{}; // a new cutoff, made at run time
    const float cut     = gain_at(0.05);                     // the same tone, now above the cutoff
    const auto  refused = retune(0.7);                       // above Nyquist: refused, nothing changes
    std::printf("a tone at 0.05 through the cutoff at 0.1: %.2f\n", static_cast<double>(passed));
    std::printf("the same tone after retune(0.02): %.2f\n", static_cast<double>(cut));
    std::printf("retune(0.7): %s\n", refused == catenae::status::out_of_range ? "out_of_range" : "accepted");
    const bool ok =
        moved && passed > 0.95f && passed < 1.05f && cut < 0.05f && refused == catenae::status::out_of_range;
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
