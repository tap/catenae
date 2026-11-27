/// @file fir_main.cpp
/// @brief The FIR family in the ratchet: the 63-tap low-pass of examples/fir_tables.h, stereo,
/// in each of four formats (guide: What arithmetic costs).
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_fir_f32   CATENAE_FIR_FORMAT=0: float, from the float table
//   catenae_icount_fir_f64   CATENAE_FIR_FORMAT=1: double, the float table's taps widened
//   catenae_icount_fir_q15   CATENAE_FIR_FORMAT=2: Q15, from the Q15 table (Q1.14 taps)
//   catenae_icount_fir_q31   CATENAE_FIR_FORMAT=3: Q31, the Q15 table's taps widened to Q1.30
//
// Each runs 1000 blocks of 64 stereo frames of the same pseudo-random input, scaled to the
// format, and prints a checksum of the samples written. Every design is a constant, so no
// designer runs in the counted image and the four differ in their arithmetic alone.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "catenae/fir.h"
#include "fir_tables.h"

#ifndef CATENAE_FIR_FORMAT
#define CATENAE_FIR_FORMAT 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

#if CATENAE_FIR_FORMAT == 0
    using format                                      = float;
    constexpr catenae::fir_design<float, 63> k_design = example::fir_lowpass;
#elif CATENAE_FIR_FORMAT == 1
    using format = double;
    constexpr std::array<double, 63> widened() noexcept {
        std::array<double, 63> t{};
        for (std::size_t k = 0; k < 63; ++k) {
            t[k] = static_cast<double>(example::fir_lowpass_taps[k]);
        }
        return t;
    }
    constexpr std::array<double, 63>          k_taps   = widened();
    constexpr catenae::fir_design<double, 63> k_design = catenae::fir_design<double, 63>::from_table(k_taps);
#elif CATENAE_FIR_FORMAT == 2
    using format                                             = catenae::q15;
    constexpr catenae::fir_design<catenae::q15, 63> k_design = example::fir_lowpass_q15;
#elif CATENAE_FIR_FORMAT == 3
    using format = catenae::q31;
    constexpr std::array<std::int32_t, 63> widened() noexcept {
        std::array<std::int32_t, 63> t{};
        for (std::size_t k = 0; k < 63; ++k) {
            t[k] = static_cast<std::int32_t>(example::fir_lowpass_q15_taps[k]) * 65536; // Q1.14 to Q1.30
        }
        return t;
    }
    constexpr std::array<std::int32_t, 63>          k_taps = widened();
    constexpr catenae::fir_design<catenae::q31, 63> k_design =
        catenae::fir_design<catenae::q31, 63>::from_table(k_taps);
#else
#error "CATENAE_FIR_FORMAT: 0 float, 1 double, 2 q15 or 3 q31"
#endif

    using value = catenae::storage_t<format>;
    using stage = catenae::basic_fir<format, 2, 63>;

    constexpr stage   k_stage{k_design};
    CATENAE_CONSTINIT stage::state s_state{};

    /// A sample from 24 random bits, about -0.5 to 0.5 of full scale.
    value sample(std::uint32_t bits) noexcept {
        const float unit = static_cast<float>(bits) / 16777216.0f - 0.5f;
#if CATENAE_FIR_FORMAT == 0
        return unit;
#elif CATENAE_FIR_FORMAT == 1
        return static_cast<double>(unit);
#elif CATENAE_FIR_FORMAT == 2
        static_cast<void>(unit);
        return static_cast<std::int16_t>(static_cast<std::int32_t>(bits >> 9) - 16384);
#else
        static_cast<void>(unit);
        return (static_cast<std::int32_t>(bits) - 8388608) * 128;
#endif
    }

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::array<value, 64 * 2> in{};
    std::array<value, 64 * 2> out{};
    catenae::no_workspace     ws{};
    std::uint64_t             hash = k_fnv_basis;
    std::uint32_t             seed = 1;
    for (int block = 0; block < k_blocks; ++block) {
        for (value& v : in) {
            seed = seed * 1664525u + 1013904223u;
            v    = sample(seed / 256u);
        }
        static_cast<void>(k_stage.run(s_state, ws, catenae::span<const value>(in), catenae::span<value>(out)));
        for (const std::size_t at : {std::size_t{0}, out.size() - 1}) {
            std::uint64_t bits = 0;
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
