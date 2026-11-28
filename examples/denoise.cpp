/// @file denoise.cpp
/// @brief PLAN.md 9.5's region in a callback: a spectral gate between an STFT and its inverse,
/// bare for a host whose block is the hop and inside `steady` for one whose block is not
/// (guide: The transform domain).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "spectral_gate.h"

namespace {

    using example::denoise;
    using example::denoise_t;
    using example::gate;
    using example::gate_t;

    // ANCHOR: bare
    // A host whose callback is a hop, 128 frames: the region itself, 128 out for 128 in.
    CATENAE_CONSTINIT catenae::memory_of<gate_t> s_gate{};

    void hop_callback(const std::array<float, 128>& in, std::array<float, 128>& out) noexcept {
        static_cast<void>(gate.process(s_gate, in, out));
    }
    // ANCHOR_END: bare

    // ANCHOR: steady
    // A host whose callback is 48 frames, a millisecond at 48 kHz: the region in steady, n for n.
    CATENAE_CONSTINIT catenae::memory_of<denoise_t> s_denoise{};

    void ms_callback(const std::array<float, 48>& in, std::array<float, 48>& out) noexcept {
        static_cast<void>(denoise.process(s_denoise, in, out));
    }
    // ANCHOR_END: steady

    constexpr std::size_t k_frames = 96 * 128; // 12,288 frames: 96 hops, 256 callbacks of 48

    /// Noise in [-0.5, 0.5): the same on every target.
    float next_noise(std::uint32_t& s) noexcept {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return static_cast<float>(s / 256u) / 16777216.0f - 0.5f;
    }

    CATENAE_CONSTINIT std::array<float, k_frames> s_tone{};
    CATENAE_CONSTINIT std::array<float, k_frames> s_in{};
    CATENAE_CONSTINIT std::array<float, k_frames> s_bare{};
    CATENAE_CONSTINIT std::array<float, k_frames> s_steady{};

} // namespace

int main() {
    // A tone at bin 32's centre (3 kHz at 48 kHz), 12 dB below full scale, in noise 40 dB below.
    std::uint32_t seed = 0x6A09E667u;
    for (std::size_t i = 0; i < k_frames; ++i) {
        s_tone[i] = 0.25f * std::sin(2.0f * 3.14159265358979f * 32.0f / 512.0f * static_cast<float>(i));
        s_in[i]   = s_tone[i] + 0.04f * next_noise(seed);
    }

    gate.reset(s_gate.state);
    denoise.reset(s_denoise.state);
    std::array<float, 128> hop_in{};
    std::array<float, 128> hop_out{};
    for (std::size_t at = 0; at < k_frames; at += 128) {
        std::memcpy(hop_in.data(), &s_in[at], sizeof hop_in);
        hop_callback(hop_in, hop_out);
        std::memcpy(&s_bare[at], hop_out.data(), sizeof hop_out);
    }
    std::array<float, 48> ms_in{};
    std::array<float, 48> ms_out{};
    for (std::size_t at = 0; at < k_frames; at += 48) {
        std::memcpy(ms_in.data(), &s_in[at], sizeof ms_in);
        ms_callback(ms_in, ms_out);
        std::memcpy(&s_steady[at], ms_out.data(), sizeof ms_out);
    }

    // The steady form is the bare form 127 frames later, bit for bit.
    bool same = true;
    for (std::size_t i = 0; i < k_frames; ++i) {
        const float owed = i < 127 ? 0.0f : s_bare[i - 127];
        same             = same && std::memcmp(&s_steady[i], &owed, sizeof owed) == 0;
    }
    // The noise, before and after: what is not the tone, once every bin has settled (16 hops).
    double noise_in  = 0.0;
    double noise_out = 0.0;
    double tone_err  = 0.0;
    for (std::size_t i = 16 * 128; i < k_frames; ++i) {
        const double tone = static_cast<double>(s_tone[i - 384]);
        noise_in += (static_cast<double>(s_in[i - 384]) - tone) * (static_cast<double>(s_in[i - 384]) - tone);
        noise_out += (static_cast<double>(s_bare[i]) - tone) * (static_cast<double>(s_bare[i]) - tone);
        tone_err += tone * tone;
    }
    const double down_db = 10.0 * std::log10(noise_in / noise_out);
    const double snr_in  = 10.0 * std::log10(tone_err / noise_in);
    const double snr_out = 10.0 * std::log10(tone_err / noise_out);

    const unsigned long bare_latency   = gate_t::exact_latency.up / gate_t::exact_latency.down;
    const unsigned long steady_latency = denoise_t::exact_latency.up / denoise_t::exact_latency.down;
    std::printf("region latency %lu frames bare, %lu in steady\n", bare_latency, steady_latency);
    std::printf("steady is the bare region 127 frames later, bit for bit: %s\n", same ? "yes" : "no");
    std::printf("signal to noise %.1f dB in, %.1f dB out: the noise %.1f dB down\n", snr_in, snr_out, down_db);

    const bool ok = same && down_db > 16.0 && down_db < 18.0;
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
