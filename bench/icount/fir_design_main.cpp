/// @file fir_design_main.cpp
/// @brief What one FIR make() costs, in instructions (PLAN.md 3.3: on the design's card): the
/// worst single call over a few cutoffs.
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv), each a
// callmax_ scenario: every make() is bracketed by tools/probe/call_marker.h's markers and the
// count is the worst single call (QEMU keys only, scripts/icount.py):
//
//   catenae_icount_callmax_design_fir_f32_63     CATENAE_FIR_DESIGN=0: 63 float taps, by value
//   catenae_icount_callmax_design_fir_q15_255    CATENAE_FIR_DESIGN=1: 255 Q15 taps, by value,
//                                                through the row-sum-preserving quantization
//   catenae_icount_callmax_design_fir_f32_1023   CATENAE_FIR_DESIGN=2: 1,023 float taps into a
//                                                table, through a workspace
//
// Each a low-pass at 0.05, 0.1, 0.2 and 0.4, 80 dB down; the taps go through an FNV-1a-64 fold,
// so the checksum is a bit-exact fingerprint of what make() returned on the target.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/fir_design.h"

#ifndef CATENAE_FIR_DESIGN
#define CATENAE_FIR_DESIGN 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

#if CATENAE_FIR_DESIGN == 0
    using design = catenae::fir_design<float, 63>;
#elif CATENAE_FIR_DESIGN == 1
    using design = catenae::fir_design<catenae::q15, 255>;
#elif CATENAE_FIR_DESIGN == 2
    using design = catenae::fir_design<float, 1023>;
    std::array<float, 1023>                    s_table{};
    std::array<double, design::workspace_size> s_workspace{};
#else
#error "CATENAE_FIR_DESIGN: 0, 1 or 2"
#endif

    std::uint64_t fold(std::uint64_t hash, const design& d) noexcept {
        for (const auto c : d.table()) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &c, sizeof c);
            hash ^= bits;
            hash *= k_fnv_prime;
        }
        return hash;
    }

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::uint64_t hash = k_fnv_basis;
    for (const double cutoff : {0.05, 0.1, 0.2, 0.4}) {
        catenae_call_begin();
#if CATENAE_FIR_DESIGN == 2
        const auto d = design::make(catenae::fir_lowpass{cutoff, 80.0}, catenae::span<float, 1023>(s_table),
                                    catenae::span<double, design::workspace_size>(s_workspace));
#else
        const auto d = design::make(catenae::fir_lowpass{cutoff, 80.0});
#endif
        catenae_call_end();
        hash = d ? fold(hash, *d) : hash;
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
