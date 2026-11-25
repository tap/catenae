/// @file strip_main.cpp
/// @brief The strip of examples/strip.h, as a chain and as a graph joined at run time: what
/// the guide's "Stages and chains" chapter measures.
// Copyright 2026 Timothy Place and the catenae contributors
//
//   catenae_icount_strip_chain  CATENAE_STRIP_GRAPH=0: the chain, 1000 blocks of 64 stereo frames
//   catenae_icount_strip_graph  CATENAE_STRIP_GRAPH=1: the same arithmetic as virtual nodes
//                               joined at run time (examples/run_time_graph.h)
//
// The two print the same checksum: the same output, bit for bit (a test pins it on the host).
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "run_time_graph.h"
#include "strip.h"

#ifndef CATENAE_STRIP_GRAPH
#define CATENAE_STRIP_GRAPH 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    std::uint64_t fold(std::uint64_t hash, float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (bits / (1u << (8 * byte))) % 256u;
            hash *= k_fnv_prime;
        }
        return hash;
    }

#if CATENAE_STRIP_GRAPH
    example::run_time::gain_node  s_level{1.0f};
    example::run_time::delay_node s_line;
    example::run_time::halve_node s_half;
    example::run_time::graph      s_graph;
#else
    CATENAE_CONSTINIT catenae::memory_of<example::strip_t> s_memory{};
#endif

} // namespace

// ANCHOR: counted
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
#if CATENAE_STRIP_GRAPH
        s_graph.process(in.data(), out.data(), 64);
#else
        static_cast<void>(example::strip.process(s_memory, in, out));
#endif
        hash = fold(hash, out[0]);
        hash = fold(hash, out[127]);
    }
    return hash;
}
// ANCHOR_END: counted

int main() {
#if CATENAE_STRIP_GRAPH
    const bool ready = s_graph.connect(s_level) && s_graph.connect(s_line) && s_graph.connect(s_half);
#else
    example::strip.reset(s_memory.state);
    const bool ready = true;
#endif
    const std::uint64_t hash = catenae_host_counted();
    const auto          high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto          low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=%d checksum=%08" PRIx32 "%08" PRIx32 "\n", ready ? 1 : 0, high, low);
    return 0;
}
