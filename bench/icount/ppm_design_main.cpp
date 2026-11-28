/// @file ppm_design_main.cpp
/// @brief What one peak programme meter's make() costs, in instructions (on the design's card):
/// the worst single call over three rates.
// Copyright 2026 Timothy Place and the catenae contributors
//
// catenae_icount_callmax_design_ppm, a callmax_ scenario: every make() is bracketed by
// tools/probe/call_marker.h's markers and the count is the worst single call (QEMU keys only,
// scripts/icount.py). It makes the EBU's Type IIb meter in float at 44.1, 48 and 96 kHz: each
// simulates its 10 ms burst and 250 ms of tone about ten times, in double; the numbers go through
// an FNV-1a-64 fold, so the checksum is a fingerprint of what make() returned.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/ppm_design.h"

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;
} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::uint64_t hash = k_fnv_basis;
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        catenae_call_begin();
        const auto d = catenae::ppm_design<float>::make(catenae::ebu_ppm(rate));
        catenae_call_end();
        if (d) {
            std::array<unsigned char, sizeof(d->numbers())> bytes{};
            std::memcpy(bytes.data(), &d->numbers(), bytes.size());
            for (const unsigned char b : bytes) {
                hash ^= b;
                hash *= k_fnv_prime;
            }
        }
    }
    return hash;
}

int main() {
    const std::uint64_t hash = catenae_host_counted();
    const auto          high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto          low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=1 checksum=%08" PRIx32 "%08" PRIx32 "\n", high, low);
    return 0;
}
