/// @file recursive_main.cpp
/// @brief The recursive family in the ratchet: the order-4 Butterworth from its table and a
/// gain, stereo, as the recursive probe's path (bench/probe/recursive_probe.cpp).
// Copyright 2026 Timothy Place and the catenae contributors
//
//   catenae_icount_recursive  1000 blocks of 64 stereo frames through butterworth<2, 4> and
//                             gain<2>; it prints a checksum of the samples written.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "call_marker.h"
#include "catenae/biquad.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/gain.h"
#include "lowpass_table.h"

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    constexpr auto k_path =
        catenae::chain(catenae::butterworth<2, 4>{example::lowpass_at_0_1}) | catenae::gain<2>{0.5f, 32};
    using path_t = std::remove_cv_t<decltype(k_path)>;

    CATENAE_CONSTINIT catenae::memory_of<path_t> s_memory{};

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    std::uint64_t             hash = k_fnv_basis;
    std::uint32_t             seed = 1;
    for (int block = 0; block < k_blocks; ++block) {
        for (float& v : in) {
            seed = seed * 1664525u + 1013904223u;
            v    = static_cast<float>(seed / 256u) / 16777216.0f - 0.5f;
        }
        static_cast<void>(k_path.process(s_memory, in, out));
        for (const std::size_t at : {std::size_t{0}, out.size() - 1}) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &out[at], sizeof bits);
            hash ^= bits;
            hash *= k_fnv_prime;
        }
    }
    return hash;
}

int main() {
    k_path.reset(s_memory.state);
    const std::uint64_t hash = catenae_host_counted();
    const auto          high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto          low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=1 checksum=%08" PRIx32 "%08" PRIx32 "\n", high, low);
    return 0;
}
