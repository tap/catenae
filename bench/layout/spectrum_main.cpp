/// @file spectrum_main.cpp
/// @brief The split-complex measurement (PLAN.md 5.2, D25): two coefficient loops over D25's
/// frame, real and imaginary parts adjacent, and over split real and imaginary arrays, vDSP's
/// form, counted on every ratchet key. Informational, never a gate.
// Copyright 2026 Timothy Place and the catenae contributors
//
// One scenario per binary, chosen at compile time (bare metal has no argv):
//
//   CATENAE_SPECTRUM_KIND   0 product: each bin times a filter's, (a + ib)(c + id), as the FFT
//                             FIR's product writes it
//                           1 gain: each bin times a real gain from its power, a spectral gate's:
//                             g = 1 where |X|^2 is above the threshold, else 0.1
//   CATENAE_SPECTRUM_BINS   257 or 1025: the frame of a transform of 512 or 2048 (N/2 + 1)
//   CATENAE_SPECTRUM_CH     1 or 2: channels end to end, as D25 lays them
//   CATENAE_SPECTRUM_SPLIT  0 (_i): D25's frame, catenae::complex<float> per bin
//                           1 (_p): split complex, each channel's real parts, then its
//                             imaginary parts
//
// Each binary runs 128 frames. Before each, the frame is filled from a fixed source outside the
// counted region; the coefficient loop over every channel's bins is one step inside
// catenae_host_counted(), between tools/probe/call_marker.h's markers, as layout_main.cpp counts.
// The loops are plain header-style code, compiled under the workload's flags, as a spectral
// stage written in a header would be. The checksum folds each channel's real and imaginary parts
// bin by bin, so the two forms of a pair print one checksum exactly when their outputs are
// bit-identical.
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "catenae/sample_format.h"

#ifndef CATENAE_SPECTRUM_KIND
#define CATENAE_SPECTRUM_KIND 0
#endif
#ifndef CATENAE_SPECTRUM_BINS
#define CATENAE_SPECTRUM_BINS 257
#endif
#ifndef CATENAE_SPECTRUM_CH
#define CATENAE_SPECTRUM_CH 1
#endif
#ifndef CATENAE_SPECTRUM_SPLIT
#define CATENAE_SPECTRUM_SPLIT 0
#endif

extern "C" void catenae_host_counted() noexcept;

namespace {

    constexpr std::size_t k_bins   = CATENAE_SPECTRUM_BINS;
    constexpr std::size_t k_ch     = CATENAE_SPECTRUM_CH;
    constexpr std::size_t k_values = k_bins * k_ch;
    constexpr std::size_t k_steps  = 128;
#if CATENAE_SPECTRUM_KIND == 1
    constexpr float k_level = 0.0625f; ///< the gate's threshold on |X|^2
    constexpr float k_floor = 0.1f;    ///< the gate's gain below it
#endif

    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

    using cx = catenae::complex<float>;

    // The source every frame is filled from, and the filter's spectrum, in both forms.
    CATENAE_CONSTINIT std::array<float, k_values> s_source_re{};
    CATENAE_CONSTINIT std::array<float, k_values> s_source_im{};
#if CATENAE_SPECTRUM_SPLIT
    CATENAE_CONSTINIT std::array<float, k_values> s_re{};
    CATENAE_CONSTINIT std::array<float, k_values> s_im{};
    CATENAE_CONSTINIT std::array<float, k_bins> s_h_re{};
    CATENAE_CONSTINIT std::array<float, k_bins> s_h_im{};
#else
    CATENAE_CONSTINIT std::array<cx, k_values> s_frame{};
    CATENAE_CONSTINIT std::array<cx, k_bins> s_h{};
#endif

    /// The next value in [-0.5, 0.5): xorshift, the same in both forms.
    float next_value(std::uint32_t& s) noexcept {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return static_cast<float>(s / 256u) / 16777216.0f - 0.5f;
    }

    /// FNV-1a-64 over whole 32-bit words.
    std::uint64_t fold(std::uint64_t hash, float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        hash ^= bits;
        hash *= k_fnv_prime;
        return hash;
    }

    void fill() noexcept {
        for (std::size_t i = 0; i < k_values; ++i) {
#if CATENAE_SPECTRUM_SPLIT
            s_re[i] = s_source_re[i];
            s_im[i] = s_source_im[i];
#else
            s_frame[i] = cx{s_source_re[i], s_source_im[i]};
#endif
        }
    }

    float re_at(std::size_t i) noexcept {
#if CATENAE_SPECTRUM_SPLIT
        return s_re[i];
#else
        return s_frame[i].re;
#endif
    }

    float im_at(std::size_t i) noexcept {
#if CATENAE_SPECTRUM_SPLIT
        return s_im[i];
#else
        return s_frame[i].im;
#endif
    }

} // namespace

// One frame's loop, between the markers: what is counted.
extern "C" CATENAE_PROBE_NOINLINE void catenae_host_counted() noexcept {
    catenae_call_begin();
    for (std::size_t c = 0; c < k_ch; ++c) {
#if CATENAE_SPECTRUM_SPLIT
        float* const xr = s_re.data() + c * k_bins;
        float* const xi = s_im.data() + c * k_bins;
        for (std::size_t k = 0; k < k_bins; ++k) {
            const float a = xr[k];
            const float b = xi[k];
#if CATENAE_SPECTRUM_KIND == 0
            const float hr = s_h_re[k];
            const float hi = s_h_im[k];
            xr[k]          = a * hr - b * hi;
            xi[k]          = a * hi + b * hr;
#else
            const float g = a * a + b * b > k_level ? 1.0f : k_floor;
            xr[k]         = a * g;
            xi[k]         = b * g;
#endif
        }
#else
        cx* const x = s_frame.data() + c * k_bins;
        for (std::size_t k = 0; k < k_bins; ++k) {
            const float a = x[k].re;
            const float b = x[k].im;
#if CATENAE_SPECTRUM_KIND == 0
            const float hr = s_h[k].re;
            const float hi = s_h[k].im;
            x[k]           = cx{a * hr - b * hi, a * hi + b * hr};
#else
            const float g = a * a + b * b > k_level ? 1.0f : k_floor;
            x[k]          = cx{a * g, b * g};
#endif
        }
#endif
    }
    catenae_call_end();
}

int main() {
    std::uint32_t seed = 0x2545F491u;
    for (std::size_t i = 0; i < k_values; ++i) {
        s_source_re[i] = next_value(seed);
        s_source_im[i] = next_value(seed);
    }
    for (std::size_t k = 0; k < k_bins; ++k) {
        const float hr = next_value(seed);
        const float hi = next_value(seed);
#if CATENAE_SPECTRUM_SPLIT
        s_h_re[k] = hr;
        s_h_im[k] = hi;
#else
        s_h[k] = cx{hr, hi};
#endif
    }
    std::uint64_t hash = k_fnv_basis;
    for (std::size_t step = 0; step < k_steps; ++step) {
        fill();
        catenae_host_counted();
        for (std::size_t i = 0; i < k_values; ++i) {
            hash = fold(fold(hash, re_at(i)), im_at(i));
        }
    }
    // Two 32-bit halves: newlib's <inttypes.h> has no PRIx64.
    const auto high = static_cast<std::uint32_t>(hash / 4294967296ull);
    const auto low  = static_cast<std::uint32_t>(hash % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=1 checksum=%08" PRIx32 "%08" PRIx32 " brackets=%u\n", high, low,
                static_cast<unsigned>(k_steps));
    return 0;
}
