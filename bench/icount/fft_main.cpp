/// @file fft_main.cpp
/// @brief The ratchet's FFT workloads: a forward and an inverse transform over 2^20 samples.
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bench/CMakeLists.txt):
//
//   catenae_icount_fft_f32_512    float, N = 512     CATENAE_FFT_T=float, CATENAE_FFT_N=512
//   catenae_icount_fft_f32_2048   float, N = 2048
//   catenae_icount_fft_f64_512    double, N = 512
//   catenae_icount_callmax_fft_f32_512  CATENAE_ICOUNT_BRACKET=1: each forward and each inverse
//                                 bracketed by tools/probe/call_marker.h's markers; the count is
//                                 the worst single call (QEMU keys only)
//
// The scenario is DspTap's rfft scenarios' (2^20 samples, a forward and an inverse each frame)
// on catenae's plan and frame. Each frame is filled from xorshift32, transformed both ways, and
// folded into an FNV-1a-64 checksum by the bits of one bin and one sample, so the printed
// checksum fingerprints the arithmetic and the DONE line is the guest's completion marker.
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "catenae/fft.h"
#include "catenae/span.h"

#ifndef CATENAE_ICOUNT_BRACKET
#define CATENAE_ICOUNT_BRACKET 0
#endif
#ifndef CATENAE_FFT_T
#define CATENAE_FFT_T float
#endif
#ifndef CATENAE_FFT_N
#define CATENAE_FFT_N 512
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    using sample                        = CATENAE_FFT_T;
    constexpr std::size_t   k_n         = CATENAE_FFT_N;
    constexpr std::size_t   k_frames    = (std::size_t{1} << 20u) / k_n;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    std::uint64_t fold(std::uint64_t hash, sample value) noexcept {
        unsigned char bytes[sizeof(sample)];
        std::memcpy(bytes, &value, sizeof value);
        for (const unsigned char byte : bytes) {
            hash ^= byte;
            hash *= k_fnv_prime;
        }
        return hash;
    }

    // The frame: static, as a workspace would be; a stack this size is not an audio thread's.
    CATENAE_CONSTINIT catenae::complex<sample> s_frame[k_n / 2u + 1u]{};

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    constexpr auto                                plan = catenae::fft_plan<sample>::of<k_n>();
    const catenae::span<catenae::complex<sample>> frame(s_frame, k_n / 2u + 1u);
    std::uint64_t                                 hash = k_fnv_basis;
    std::uint32_t                                 seed = 0x2545f491u;
    for (std::size_t f = 0; f < k_frames; ++f) {
        for (std::size_t i = 0; i < k_n / 2u; ++i) {
            seed ^= seed << 13u;
            seed ^= seed >> 17u;
            seed ^= seed << 5u;
            // Two 16-bit halves as two samples in [-1, 1).
            s_frame[i] = {static_cast<sample>(static_cast<std::int32_t>(seed % 65536u) - 32768) / sample(32768),
                          static_cast<sample>(static_cast<std::int32_t>(seed / 65536u) - 32768) / sample(32768)};
        }
#if CATENAE_ICOUNT_BRACKET
        catenae_call_begin();
#endif
        plan.forward(frame);
#if CATENAE_ICOUNT_BRACKET
        catenae_call_end();
#endif
        hash = fold(hash, s_frame[(f * 7u) % (k_n / 2u + 1u)].re);
#if CATENAE_ICOUNT_BRACKET
        catenae_call_begin();
#endif
        const int e = plan.inverse(frame);
#if CATENAE_ICOUNT_BRACKET
        catenae_call_end();
#endif
        hash = fold(hash, s_frame[(f * 5u) % (k_n / 2u)].im);
        hash ^= static_cast<std::uint64_t>(e);
    }
    return hash;
}

int main() {
    const std::uint64_t hash = catenae_host_counted();
    // Two 32-bit halves: newlib's <inttypes.h> has no PRIx64.
    const auto high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=%d checksum=%08" PRIx32 "%08" PRIx32 "\n", hash != k_fnv_basis ? 1 : 0, high,
                low);
    return 0;
}
