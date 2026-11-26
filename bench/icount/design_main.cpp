/// @file design_main.cpp
/// @brief What one recursive make() costs, in instructions (PLAN.md 3.3: on each design's
/// card): the worst single call over a few frequencies.
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv), each a
// callmax_ scenario: every make() is bracketed by tools/probe/call_marker.h's markers and the
// count is the worst single call (QEMU keys only, scripts/icount.py):
//
//   catenae_icount_callmax_design_cookbook        CATENAE_DESIGN_SECTIONS=0: each of the
//                                                 cookbook's nine at 0.05 and at 0.2
//   catenae_icount_callmax_design_butterworth_<N> CATENAE_DESIGN_SECTIONS=N/2: the order-N
//                                                 Butterworth at 0.45 / 2, 4, 8 and 16
//
// All in float. Each design's rows go through an FNV-1a-64 fold, so the checksum is a bit-exact
// fingerprint of what make() returned on the target.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/biquad_design.h"

#ifndef CATENAE_DESIGN_SECTIONS
#define CATENAE_DESIGN_SECTIONS 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    template <std::size_t K>
    std::uint64_t fold(std::uint64_t hash, const catenae::biquad_design<float, K>& d) noexcept {
        for (const auto& row : d.table()) {
            for (const float c : row) {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &c, sizeof bits);
                hash ^= bits;
                hash *= k_fnv_prime;
            }
        }
        return (hash ^ d.tail_frames()) * k_fnv_prime;
    }

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::uint64_t hash = k_fnv_basis;
#if CATENAE_DESIGN_SECTIONS == 0
    constexpr std::array<catenae::biquad_response, 9> responses = {
        catenae::biquad_response::lowpass,        catenae::biquad_response::highpass,
        catenae::biquad_response::bandpass_skirt, catenae::biquad_response::bandpass_peak,
        catenae::biquad_response::notch,          catenae::biquad_response::allpass,
        catenae::biquad_response::peaking,        catenae::biquad_response::low_shelf,
        catenae::biquad_response::high_shelf};
    for (const catenae::biquad_response r : responses) {
        for (const double f0 : {0.05, 0.2}) {
            catenae_call_begin();
            const auto d = catenae::biquad_design<float>::make({r, f0, 2.0, 6.0});
            catenae_call_end();
            hash = d ? fold(hash, *d) : hash;
        }
    }
#else
    for (const double divisor : {2.0, 4.0, 8.0, 16.0}) {
        catenae_call_begin();
        const auto d = catenae::butterworth_design<float, 2 * CATENAE_DESIGN_SECTIONS>::make(0.45 / divisor);
        catenae_call_end();
        hash = d ? fold(hash, *d) : hash;
    }
#endif
    return hash;
}

int main() {
    const std::uint64_t hash = catenae_host_counted();
    const auto          high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto          low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=1 checksum=%08" PRIx32 "%08" PRIx32 "\n", high, low);
    return 0;
}
