/// @file denoise_main.cpp
/// @brief The transform domain in the ratchet: PLAN.md 9.5's region of examples/spectral_gate.h
/// inside steady, in a callback, and its worst single callback (guide: The transform domain).
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_denoise_f32           CATENAE_ICOUNT_BRACKET=0: example::denoise, an STFT of 512
//                                        every 128, the spectral gate and the inverse, in steady,
//                                        mono, in 1000 callbacks of 64 frames
//   catenae_icount_callmax_denoise_f32   1: each callback bracketed by tools/probe/call_marker.h's
//                                        markers; the count is the worst single callback, one
//                                        that completes a hop (QEMU keys only)
//
// The input is a tone at bin 32's centre in noise, as in examples/denoise.cpp; the checksum
// folds the first and last value each callback writes.
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "spectral_gate.h"

#ifndef CATENAE_ICOUNT_BRACKET
#define CATENAE_ICOUNT_BRACKET 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    std::uint64_t fold(std::uint64_t hash, float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        hash ^= bits;
        hash *= k_fnv_prime;
        return hash;
    }

    CATENAE_CONSTINIT catenae::memory_of<example::denoise_t> s_memory{};

    /// One hop of the tone, 0.25 at bin 32's centre: a period of 16 frames, so 128 frames hold
    /// eight, made once outside the counted loop's arithmetic.
    CATENAE_CONSTINIT std::array<float, 128> s_tone{};

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::array<float, 64> in{};
    std::array<float, 64> out{};
    std::uint64_t         hash = k_fnv_basis;
    std::uint32_t         seed = 0x6a09e667u;
    example::denoise.reset(s_memory.state);
    for (int block = 0; block < k_blocks; ++block) {
        for (std::size_t f = 0; f < in.size(); ++f) {
            seed ^= seed << 13u;
            seed ^= seed >> 17u;
            seed ^= seed << 5u;
            const std::size_t at = (static_cast<std::size_t>(block) * in.size() + f) % s_tone.size();
            in[f]                = s_tone[at] + 0.04f * (static_cast<float>(seed / 256u) / 16777216.0f - 0.5f);
        }
#if CATENAE_ICOUNT_BRACKET
        catenae_call_begin();
#endif
        static_cast<void>(example::denoise.process(s_memory, in, out));
#if CATENAE_ICOUNT_BRACKET
        catenae_call_end();
#endif
        hash = fold(fold(hash, out[0]), out[out.size() - 1]);
    }
    return hash;
}

int main() {
    for (std::size_t i = 0; i < s_tone.size(); ++i) {
        s_tone[i] = 0.25f * std::sin(2.0f * 3.14159265358979f * static_cast<float>(i) / 16.0f);
    }
    const std::uint64_t hash = catenae_host_counted();
    // Two 32-bit halves: newlib's <inttypes.h> has no PRIx64.
    const auto high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=%d checksum=%08" PRIx32 "%08" PRIx32 "\n", hash != k_fnv_basis ? 1 : 0, high,
                low);
    return 0;
}
