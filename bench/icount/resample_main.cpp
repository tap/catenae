/// @file resample_main.cpp
/// @brief The rate family in the ratchet: PLAN.md 9.4's four-times overdrive, and up2 and down2
/// alone in three formats (guide: Rate and latency).
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_overdrive   CATENAE_RESAMPLE_KIND=0: examples/overdrive.h, a callback of 64
//                              stereo frames, 1000 times
//   catenae_icount_up2_f32     1: up2<2> at divisor 1, 64 stereo frames in, 128 out, 1000 times
//   catenae_icount_down2_f32   2: down2<2>, 64 in, 32 out
//   catenae_icount_up2_q15     3, catenae_icount_down2_q15 4: the same in Q15
//   catenae_icount_up2_q31     5, catenae_icount_down2_q31 6: the same in Q31
//
// Each prints a checksum of the first and last value written each call.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "catenae/resample.h"
#include "overdrive.h"

#ifndef CATENAE_RESAMPLE_KIND
#define CATENAE_RESAMPLE_KIND 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr int           k_blocks    = 1000;
    constexpr std::size_t   k_frames    = 64;
    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

#if CATENAE_RESAMPLE_KIND == 0
    using stage_t = example::overdrive_t;
#elif CATENAE_RESAMPLE_KIND == 1
    using stage_t = catenae::up2<2>;
#elif CATENAE_RESAMPLE_KIND == 2
    using stage_t = catenae::down2<2>;
#elif CATENAE_RESAMPLE_KIND == 3
    using stage_t = catenae::up2_q15<2>;
#elif CATENAE_RESAMPLE_KIND == 4
    using stage_t = catenae::down2_q15<2>;
#elif CATENAE_RESAMPLE_KIND == 5
    using stage_t = catenae::up2_q31<2>;
#elif CATENAE_RESAMPLE_KIND == 6
    using stage_t = catenae::down2_q31<2>;
#else
#error "CATENAE_RESAMPLE_KIND: 0 overdrive, 1 to 6 up2 and down2 in float, Q15 and Q31"
#endif

#if CATENAE_RESAMPLE_KIND == 0
    const stage_t& k_stage = example::overdrive;
#else
    constexpr stage_t k_stage{};
#endif
    using value                 = typename stage_t::input_port::value_type;
    constexpr std::size_t k_out = stage_t::max_outputs_for(k_frames);

    CATENAE_CONSTINIT catenae::state_of<stage_t> s_state{};
    CATENAE_CONSTINIT catenae::workspace_of<stage_t> s_work{};

    /// A value from 24 random bits, about -0.5 to 0.5 of full scale.
    value sample(std::uint32_t bits) noexcept {
        const float unit = static_cast<float>(bits) / 16777216.0f - 0.5f;
#if CATENAE_RESAMPLE_KIND <= 2
        return unit;
#elif CATENAE_RESAMPLE_KIND <= 4
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
    std::array<value, k_frames * 2> in{};
    std::array<value, k_out * 2>    out{};
    std::uint64_t                   hash = k_fnv_basis;
    std::uint32_t                   seed = 1;
    for (int block = 0; block < k_blocks; ++block) {
        for (value& v : in) {
            seed = seed * 1664525u + 1013904223u;
            v    = sample(seed / 256u);
        }
        const std::size_t made =
            k_stage.run(s_state, s_work, catenae::span<const value>(in), catenae::span<value>(out));
        for (const std::size_t at : {std::size_t{0}, made * 2 - 1}) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &out[at], sizeof out[at]);
            hash ^= bits;
            hash *= k_fnv_prime;
        }
    }
    return hash;
}

int main() {
    k_stage.reset(s_state);
    const std::uint64_t hash = catenae_host_counted();
    const auto          high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto          low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=1 checksum=%08" PRIx32 "%08" PRIx32 "\n", high, low);
    return 0;
}
