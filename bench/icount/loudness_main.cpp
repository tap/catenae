/// @file loudness_main.cpp
/// @brief Loudness and true peak in the ratchet: the programme meters of examples/loudness.h in
/// float, and true peak in Q15 and Q31 (guide: Loudness and true peak).
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_loudness_f32    CATENAE_LOUDNESS_KIND=0: the example's two taps, loudness
//                                  (K-weighting from its table, 20 ms hops) and true peak
//   catenae_icount_true_peak_q15   1: true peak in Q15
//   catenae_icount_true_peak_q31   2: true peak in Q31
//
// Each runs 1000 callbacks of 64 stereo frames of noise, a result every 960 frames (the
// integrated value's histogram passes once a step from the fourth), and folds every result into
// a checksum as it completes, so the checksum is a fingerprint of what the meters read.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "catenae/detail/convert.h"
#include "catenae/tap.h"
#include "catenae/true_peak_meter.h"
#include "loudness.h"

#ifndef CATENAE_LOUDNESS_KIND
#define CATENAE_LOUDNESS_KIND 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

#if CATENAE_LOUDNESS_KIND == 0
    using value_type               = float;
    constexpr auto        k_path   = example::programme_meters;
    constexpr std::size_t k_stages = 2;
#elif CATENAE_LOUDNESS_KIND == 1 || CATENAE_LOUDNESS_KIND == 2
#if CATENAE_LOUDNESS_KIND == 1
    using format = catenae::q15;
#else
    using format = catenae::q31;
#endif
    using value_type      = catenae::storage_t<format>;
    constexpr auto k_path = catenae::chain(catenae::tap<2, catenae::basic_true_peak_meter<format, 2, 960, 48000>>{});
    constexpr std::size_t k_stages = 1;
#else
#error "CATENAE_LOUDNESS_KIND: 0 the float programme meters, 1 Q15 or 2 Q31 true peak"
#endif
    using path_t = std::remove_cv_t<decltype(k_path)>;

    CATENAE_CONSTINIT catenae::memory_of<path_t> s_memory{};

    template <typename R>
    std::uint64_t fold(std::uint64_t hash, const R& result) noexcept {
        std::array<unsigned char, sizeof(R)> bytes{};
        std::memcpy(bytes.data(), &result, sizeof(R));
        for (const unsigned char b : bytes) {
            hash ^= b;
            hash *= k_fnv_prime;
        }
        return hash;
    }

    /// Each tap's result folded in when its count has moved.
    template <std::size_t I>
    std::uint64_t fold_new(std::uint64_t hash, std::array<std::uint32_t, 2>& seen) noexcept {
        if constexpr (I < k_stages) {
            const auto& stage = k_path.template stage<I>();
            const auto& st    = path_t::template stage_state<I>(s_memory.state);
            if (stage.results_ready(st) != seen[I]) {
                seen[I] = stage.results_ready(st);
                hash    = fold(hash, stage.result(st));
            }
            return fold_new<I + 1>(hash, seen);
        }
        else {
            static_cast<void>(seen);
            return hash;
        }
    }

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::array<value_type, 64 * 2> block{};
    std::array<std::uint32_t, 2>   seen{};
    std::uint64_t                  hash = k_fnv_basis;
    std::uint32_t                  seed = 1;
    k_path.reset(s_memory.state);
    for (int b = 0; b < k_blocks; ++b) {
        for (std::size_t f = 0; f < 64; ++f) {
            seed = seed * 1664525u + 1013904223u;
#if CATENAE_LOUDNESS_KIND == 0
            const float left = 0.5f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
            block[2 * f]     = left;
            block[2 * f + 1] = 0.5f * left;
#else
            const std::int32_t left = static_cast<std::int32_t>(seed) / (CATENAE_LOUDNESS_KIND == 1 ? 131072 : 2);
            block[2 * f]            = catenae::detail::narrow<value_type>(left);
            block[2 * f + 1]        = catenae::detail::narrow<value_type>(left / 2);
#endif
        }
        static_cast<void>(k_path.process(s_memory, block, block));
        hash = fold_new<0>(hash, seen);
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
