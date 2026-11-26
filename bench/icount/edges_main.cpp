/// @file edges_main.cpp
/// @brief C2's edges in the ratchet: 24-bit packed stereo off the wire, sanitized, mixed and
/// dithered in float, and back to the wire, as the c2 probe's path (bench/probe/c2_probe.cpp).
// Copyright 2026 Timothy Place and the catenae contributors
//
//   catenae_icount_edges  1000 blocks of 64 stereo frames through unpack, convert, sanitize,
//                         mix, dither, convert and pack; it prints a checksum of the bytes
//                         written.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "call_marker.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/convert.h"
#include "catenae/dither.h"
#include "catenae/mix.h"
#include "catenae/pack.h"
#include "catenae/sanitize.h"
#include "catenae/unpack.h"

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    namespace wire = catenae::wire;

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    constexpr auto k_edges = catenae::chain(catenae::unpack<wire::s24le_packed, 2>{})
                             | catenae::convert<catenae::q31, float, 2>{} | catenae::sanitize<2>{}
                             | catenae::mix<2, 2>{{0.75f, 0.25f, 0.25f, 0.75f}, 32} | catenae::dither<2, 24>{}
                             | catenae::convert<float, catenae::q31, 2>{} | catenae::pack<wire::s24le_packed, 2>{};
    using edges_t = std::remove_cv_t<decltype(k_edges)>;

    CATENAE_CONSTINIT catenae::memory_of<edges_t> s_memory{};

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::array<std::byte, 64 * 2 * 3> in{};
    std::array<std::byte, 64 * 2 * 3> out{};
    std::uint64_t                     hash = k_fnv_basis;
    std::uint32_t                     seed = 1;
    for (int block = 0; block < k_blocks; ++block) {
        for (std::byte& v : in) {
            seed = seed * 1664525u + 1013904223u;
            v    = static_cast<std::byte>(seed / 16777216u);
        }
        static_cast<void>(k_edges.process(s_memory, in, out));
        for (const std::size_t at : {std::size_t{0}, out.size() - 1}) {
            hash ^= std::to_integer<std::uint64_t>(out[at]);
            hash *= k_fnv_prime;
        }
    }
    return hash;
}

int main() {
    k_edges.reset(s_memory.state);
    const std::uint64_t hash = catenae_host_counted();
    const auto          high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto          low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=1 checksum=%08" PRIx32 "%08" PRIx32 "\n", high, low);
    return 0;
}
