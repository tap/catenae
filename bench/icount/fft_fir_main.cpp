/// @file fft_fir_main.cpp
/// @brief The FFT FIR in the ratchet: the 255-tap low-pass of examples/long_filter.h in a
/// callback by fast convolution and frame by frame, the worst single callback, and what its
/// design's make() costs (guide: Long filters).
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare-metal targets have no argv):
//
//   catenae_icount_fft_fir_f32           CATENAE_FFT_FIR_KIND=0: example::long_filter, the FFT FIR
//                                        in steady<>, stereo
//   catenae_icount_fir_f32_255           1: example::long_direct, the same taps frame by frame
//   catenae_icount_callmax_fft_fir_f32   0 with CATENAE_ICOUNT_BRACKET=1: each callback bracketed
//                                        by tools/probe/call_marker.h's markers; the count is the
//                                        worst single callback, the one that completes a block
//                                        (QEMU keys only)
//   catenae_icount_callmax_design_fft_fir_f32_255  2: the spectrum's make() from the ROM taps,
//                                        bracketed: one double transform of 512 (QEMU keys only)
//
// Each of the first three runs 1000 callbacks of 64 stereo frames of noise and folds the first
// and last value each callback writes into a checksum; every design is a table.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "long_filter.h"

#ifndef CATENAE_FFT_FIR_KIND
#define CATENAE_FFT_FIR_KIND 0
#endif
#ifndef CATENAE_ICOUNT_BRACKET
#define CATENAE_ICOUNT_BRACKET 0
#endif

extern "C" std::uint64_t catenae_host_counted() noexcept;

namespace {

    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    std::uint64_t fold(std::uint64_t hash, float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        hash ^= bits;
        hash *= k_fnv_prime;
        return hash;
    }

#if CATENAE_FFT_FIR_KIND == 2
    using design_t = catenae::fft_fir_design<float, 255, 256>;
    CATENAE_CONSTINIT design_t::table_type s_table{};
    CATENAE_CONSTINIT std::array<catenae::complex<double>, design_t::workspace_size> s_work{};
#else
    constexpr int k_blocks = 1000;
#if CATENAE_FFT_FIR_KIND == 0
    constexpr auto& k_path = example::long_filter;
#elif CATENAE_FFT_FIR_KIND == 1
    constexpr auto& k_path = example::long_direct;
#else
#error "CATENAE_FFT_FIR_KIND: 0 the FFT FIR, 1 the direct form or 2 the spectrum's make()"
#endif
    using path_t = std::remove_cv_t<std::remove_reference_t<decltype(k_path)>>;
    CATENAE_CONSTINIT catenae::memory_of<path_t> s_memory{};
#endif

} // namespace

// The counted region: callgrind's --toggle-collect names it on the host.
extern "C" CATENAE_PROBE_NOINLINE std::uint64_t catenae_host_counted() noexcept {
    std::uint64_t hash = k_fnv_basis;
#if CATENAE_FFT_FIR_KIND == 2
    catenae_call_begin();
    const auto made = design_t::make(example::long_lowpass, catenae::span<design_t::bin_type, design_t::bins>(s_table),
                                     catenae::span<catenae::complex<double>, design_t::workspace_size>(s_work));
    catenae_call_end();
    if (made) {
        for (const catenae::complex<float>& b : made->table()) {
            hash = fold(fold(hash, b.re), b.im);
        }
    }
#else
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    std::uint32_t             seed = 0x2545f491u;
    k_path.reset(s_memory.state);
    for (int block = 0; block < k_blocks; ++block) {
        for (float& v : in) {
            seed ^= seed << 13u;
            seed ^= seed >> 17u;
            seed ^= seed << 5u;
            v = static_cast<float>(seed / 256u) / 16777216.0f - 0.5f;
        }
#if CATENAE_ICOUNT_BRACKET
        catenae_call_begin();
#endif
        static_cast<void>(k_path.process(s_memory, in, out));
#if CATENAE_ICOUNT_BRACKET
        catenae_call_end();
#endif
        hash = fold(fold(hash, out[0]), out[out.size() - 1]);
    }
#endif
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
