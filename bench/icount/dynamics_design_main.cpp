/// @file dynamics_design_main.cpp
/// @brief What one dynamics make() costs, in instructions (on the design's card): the worst
/// single call over a few specs.
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv), each a
// callmax_ scenario: every make() is bracketed by tools/probe/call_marker.h's markers and the
// count is the worst single call (QEMU keys only, scripts/icount.py):
//
//   catenae_icount_callmax_design_compressor   CATENAE_DYNAMICS_DESIGN=0
//   catenae_icount_callmax_design_limiter      CATENAE_DYNAMICS_DESIGN=1
//   catenae_icount_callmax_design_expander     CATENAE_DYNAMICS_DESIGN=2
//
// Each makes four float designs, their time constants from 1 to 48,000 frames; the numbers go
// through an FNV-1a-64 fold, so the checksum is a bit-exact fingerprint of what make() returned.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/dynamics_design.h"

#ifndef CATENAE_DYNAMICS_DESIGN
#define CATENAE_DYNAMICS_DESIGN 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    template <typename Numbers>
    std::uint64_t fold(std::uint64_t hash, const Numbers& n) noexcept {
        std::array<unsigned char, sizeof(Numbers)> bytes{};
        std::memcpy(bytes.data(), &n, sizeof(Numbers));
        for (const unsigned char b : bytes) {
            hash ^= b;
            hash *= k_fnv_prime;
        }
        return hash;
    }

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::uint64_t hash = k_fnv_basis;
    for (const double frames : {1.0, 48.0, 2400.0, 48000.0}) {
        catenae_call_begin();
#if CATENAE_DYNAMICS_DESIGN == 0
        const auto d = catenae::compressor_design<float>::make({-18.0, 4.0, 6.0, frames, 2.0 * frames, 6.0});
#elif CATENAE_DYNAMICS_DESIGN == 1
        const auto d = catenae::limiter_design<float>::make({-1.0, frames});
#elif CATENAE_DYNAMICS_DESIGN == 2
        const auto d = catenae::expander_design<float>::make({-50.0, 4.0, 6.0, 40.0, frames, 2400, 2.0 * frames});
#else
#error "CATENAE_DYNAMICS_DESIGN: 0 compressor, 1 limiter or 2 expander"
#endif
        catenae_call_end();
        hash = d ? fold(hash, d->numbers()) : hash;
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
