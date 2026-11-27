/// @file overdrive.cpp
/// @brief PLAN.md 9.4's chain: a tanh overdrive at four times the rate, two voices sharing one
/// workspace, and a dry path lined up with it (guide: Rate and latency).
// Copyright 2026 Timothy Place and the catenae contributors
#include "overdrive.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/gain.h"
#include "catenae/map.h"
#include "catenae/mix.h"
#include "catenae/oversample.h"
#include "catenae/parallel.h"
#include "catenae/resample.h"

namespace {

    using example::overdrive;
    using example::overdrive_t;
    using example::soft_clip;

    // ANCHOR: voices
    // The memory. A voice is its state; the workspace is the thread's and both voices use it.
    CATENAE_CONSTINIT catenae::state_of<overdrive_t> s_voice_a{};
    CATENAE_CONSTINIT catenae::state_of<overdrive_t> s_voice_b{};
    CATENAE_CONSTINIT catenae::workspace_of<overdrive_t> s_work{};

    void reset_voices() noexcept { // once, before the first callback
        overdrive.reset(s_voice_a);
        overdrive.reset(s_voice_b);
    }

    void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
        static_cast<void>(overdrive.process(s_voice_a, s_work, in, out));
    }
    // ANCHOR_END: voices

    // ANCHOR: bare
    // The same parts without oversample: the inner pair relaxed, and half a frame short of whole.
    constexpr auto bare = catenae::up2<2>{} | catenae::up2<2, 2>{} | catenae::map<2, soft_clip>{soft_clip{4.0f}}
                          | catenae::down2<2, 2>{} | catenae::down2<2>{};
    static_assert(std::remove_cv_t<decltype(bare)>::exact_latency == (catenae::ratio{47, 2}));
    // ANCHOR_END: bare

    // ANCHOR: dry_wet
    // A dry path beside the wet one: parallel delays the dry arm by the wet one's 24 frames, so
    // the two meet in time, and a mix adds them, half of each.
    constexpr auto wet_and_dry = catenae::parallel<overdrive_t, catenae::gain<2>>(overdrive, catenae::gain<2>{1.0f, 0})
                                 | catenae::mix<4, 2>{{0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f}, 0};
    static_assert(std::remove_cv_t<decltype(wet_and_dry)>::exact_latency == (catenae::ratio{24, 1}));
    // ANCHOR_END: dry_wet

    constexpr std::size_t k_period = 512; // the analysis length: 69 cycles of the tone exactly
    constexpr std::size_t k_cycles = 69;
    constexpr double      k_pi     = 3.141592653589793;

    /// The power of bin k of the last k_period samples of `y`, a mono signal: a phasor turned a
    /// bin's step a sample, so no sine is computed per sample.
    double bin_power(const std::array<float, 4 * k_period>& y, std::size_t k) noexcept {
        const double step_re = std::cos(2.0 * k_pi * static_cast<double>(k) / static_cast<double>(k_period));
        const double step_im = -std::sin(2.0 * k_pi * static_cast<double>(k) / static_cast<double>(k_period));
        double       w_re    = 1.0;
        double       w_im    = 0.0;
        double       re      = 0.0;
        double       im      = 0.0;
        for (std::size_t n = 0; n < k_period; ++n) {
            const double v = static_cast<double>(y[3 * k_period + n]);
            re += v * w_re;
            im += v * w_im;
            const double next = w_re * step_re - w_im * step_im;
            w_im              = w_re * step_im + w_im * step_re;
            w_re              = next;
        }
        return re * re + im * im;
    }

    /// The aliases' power below 3/8 of the rate, the bins that are no harmonic of the tone,
    /// over the fundamental's, in dB.
    double aliases_db(const std::array<float, 4 * k_period>& y) noexcept {
        double aliases = 0.0;
        for (std::size_t k = 1; k < k_period * 3 / 8; ++k) {
            aliases += k % k_cycles == 0 ? 0.0 : bin_power(y, k);
        }
        return 10.0 * std::log10(aliases / bin_power(y, k_cycles));
    }

    /// A tone of 69 cycles in 512 frames at -1 dBFS through the overdrive in callbacks, and through the
    /// same soft clip at the base rate: channel 0 of each, 2,048 frames.
    void drive(std::array<float, 4 * k_period>& oversampled, std::array<float, 4 * k_period>& plain) noexcept {
        reset_voices();
        const soft_clip           clip{4.0f};
        std::array<float, 64 * 2> in{};
        std::array<float, 64 * 2> out{};
        for (std::size_t block = 0; block < 4 * k_period / 64; ++block) {
            for (std::size_t f = 0; f < 64; ++f) {
                const std::size_t n = block * 64 + f;
                const double      a = 2.0 * k_pi * static_cast<double>(k_cycles * n) / static_cast<double>(k_period);
                in[2 * f]           = static_cast<float>(0.9 * std::sin(a));
                in[2 * f + 1]       = in[2 * f];
                plain[n]            = clip(in[2 * f]);
            }
            audio_callback(in, out);
            for (std::size_t f = 0; f < 64; ++f) {
                oversampled[block * 64 + f] = out[2 * f];
            }
        }
    }

} // namespace

int main() {
    static std::array<float, 4 * k_period> oversampled{};
    static std::array<float, 4 * k_period> plain{};
    drive(oversampled, plain);
    const double at_rate = aliases_db(plain);
    const double at_four = aliases_db(oversampled);
    std::printf("aliases below 3/8 of the rate, the soft clip at the base rate: %.1f dB\n", at_rate);
    std::printf("the same at four times the rate: %.1f dB\n", at_four);
    const unsigned long latency = overdrive_t::exact_latency.up; // printed the same on every target
    const unsigned long voice   = sizeof(catenae::state_of<overdrive_t>);
    const unsigned long work    = sizeof(catenae::workspace_of<overdrive_t>);
    std::printf("latency: %lu frames; the bare chain %.1f; with the dry path %.1f\n", latency,
                static_cast<double>(bare.latency().nominal), static_cast<double>(wet_and_dry.latency().nominal));
    std::printf("a voice: %lu bytes; the shared workspace: %lu bytes\n", voice, work);
    const bool ok = at_four < at_rate - 50.0;
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
