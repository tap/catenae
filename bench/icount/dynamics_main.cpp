/// @file dynamics_main.cpp
/// @brief The dynamics family in the ratchet: each stage alone and the strip of
/// examples/dynamics_tables.h, in float (guide: Dynamics).
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_compressor_f32   CATENAE_DYNAMICS_KIND=0: compressor<2>, the strip's design
//   catenae_icount_expander_f32     1: expander<2>
//   catenae_icount_limiter_f32      2: limiter<2, 48>
//   catenae_icount_dynamics         3: the strip, the three in one chain
//
// Each runs 1000 callbacks of 64 stereo frames of noise whose level steps every 8 callbacks
// between -70, -30, -12 and +6 dBFS, so every detector attacks, holds and releases, and folds
// the first and last value written each call into a checksum.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "dynamics_tables.h"

#ifndef CATENAE_DYNAMICS_KIND
#define CATENAE_DYNAMICS_KIND 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

#if CATENAE_DYNAMICS_KIND == 0
    constexpr auto k_path = catenae::chain(catenae::compressor<2>{example::bus_design});
#elif CATENAE_DYNAMICS_KIND == 1
    constexpr auto k_path = catenae::chain(catenae::expander<2>{example::gate_design});
#elif CATENAE_DYNAMICS_KIND == 2
    constexpr auto k_path = catenae::chain(catenae::limiter<2, 48>{example::ceiling_design});
#elif CATENAE_DYNAMICS_KIND == 3
    constexpr auto k_path = example::dynamics_strip;
#else
#error "CATENAE_DYNAMICS_KIND: 0 compressor, 1 expander, 2 limiter or 3 the strip"
#endif
    using path_t = std::remove_cv_t<decltype(k_path)>;

    CATENAE_CONSTINIT catenae::memory_of<path_t> s_memory{};

    /// The levels the input steps through, as amplitudes: -70, -30, -12 and +6 dBFS.
    constexpr std::array<float, 4> k_levels = {3.1622777e-4f, 3.1622777e-2f, 0.25118864f, 1.9952623f};

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    std::uint64_t             hash = k_fnv_basis;
    std::uint32_t             seed = 1;
    k_path.reset(s_memory.state);
    for (int block = 0; block < k_blocks; ++block) {
        const float level = k_levels[static_cast<std::size_t>(block / 8) % k_levels.size()];
        for (float& v : in) {
            seed = seed * 1664525u + 1013904223u;
            v    = level * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
        }
        static_cast<void>(k_path.process(s_memory, in, out));
        for (const std::size_t at : {std::size_t{0}, out.size() - 1}) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &out[at], sizeof out[at]);
            hash ^= bits;
            hash *= k_fnv_prime;
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
