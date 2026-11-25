/// @file version_main.cpp
/// @brief The ratchet's first workload: catenae::version(), the first compiled unit.
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_version          CATENAE_ICOUNT_BRACKET=0: the whole run is the count
//                                   under QEMU; on the host, callgrind counts
//                                   catenae_host_counted() alone
//   catenae_icount_callmax_version  CATENAE_ICOUNT_BRACKET=1: each call bracketed by
//                                   tools/probe/call_marker.h's markers, and the count is the
//                                   worst single call (QEMU keys only)
//
// Every result goes through an integer FNV-1a-64 fold, so the printed checksum is a bit-exact
// fingerprint of what the calls returned, and the DONE line is the guest's completion marker.
#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "call_marker.h"
#include "catenae/version.h"

#ifndef CATENAE_ICOUNT_BRACKET
#define CATENAE_ICOUNT_BRACKET 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_calls     = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    std::uint64_t fold(std::uint64_t hash, std::uint32_t value) noexcept {
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (value / (1u << (8 * byte))) % 256u;
            hash *= k_fnv_prime;
        }
        return hash;
    }

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::uint64_t hash = k_fnv_basis;
    for (int i = 0; i < k_calls; ++i) {
#if CATENAE_ICOUNT_BRACKET
        catenae_call_begin();
#endif
        const std::uint32_t value = catenae::version();
#if CATENAE_ICOUNT_BRACKET
        catenae_call_end();
#endif
        hash = fold(hash, value);
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
