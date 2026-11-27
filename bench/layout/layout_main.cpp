/// @file layout_main.cpp
/// @brief Interleaved against planar: C1's stages at 1, 2, 6 and 12 channels, counted on every
/// ratchet key (PLAN.md section 5.1; bench/README.md, "The layout benchmark").
// Copyright 2026 Timothy Place and the catenae contributors
//
// Carried from DspTap's bench/layout/layout_main.cpp (NOTICE.md) and rewritten on catenae's
// stages. Informational, never a gate. One scenario per binary, chosen at compile time (bare
// metal has no argv):
//
//   CATENAE_LAYOUT_KIND    0 gain: catenae::gain at 0.5, ramping over 96 frames, set to 0.75
//                            once after reset, so the first block ramps
//                          1 delay: catenae::delay of 32 frames
//                          2 map: catenae::map halving each value (stateless)
//                          3 strip: the three as one chain, as examples/strip.h writes it
//                          4 biquad: catenae::butterworth of order 4 at 0.1, from its table
//                            (examples/lowpass_table.h): the recursive stage's row
//                          5 fir: catenae::fir of 63 taps, the low-pass at 0.1 from its table
//                            (examples/fir_tables.h): the direct-form FIR's row
//                          6 overdrive: examples/overdrive.h's soft clip at four times the rate
//                            in catenae::oversample: the rate family's row
//                          7 fft_fir: catenae::fft_fir of 255 taps in blocks of 256 in
//                            catenae::steady, the low-pass at 0.1 from its spectrum
//                            (examples/fft_fir_tables.h): the FFT FIR's row
//   CATENAE_LAYOUT_CH      channels: 1, 2, 6 (5.1) or 12 (7.1.4)
//   CATENAE_LAYOUT_PLANAR  0: one configuration of Ch channels over interleaved frames
//                          1: Ch mono configurations, each with its own state, each over its
//                             own contiguous channel buffer
//                          2: the mono configuration in catenae::planar_section over the
//                             interleaved frames: what 1 costs with the copies in and out
//
// Both layouts know their channel count at compile time. 16,384 frames per channel in calls of
// 128. Each 128-frame step (one call interleaved, Ch calls planar) runs inside
// catenae_host_counted(), between tools/probe/call_marker.h's markers: under QEMU the count is
// the plugin's sum over the brackets (CATENAE_CALL_MAX total=), on the host callgrind's count
// inside that function, so the input, the checksum and start-up stay outside it on every key.
// The DONE line's brackets= says how many steps there are; scripts/icount.py refuses a count
// whose plugin line disagrees.
//
// The checksum folds each channel's output in frame order into its own FNV-1a-64 (a word at a
// time), then the channel hashes in order, so the two layouts of a pair print the same checksum exactly when
// their outputs are bit-identical. Nothing in the streaming path is double.
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "call_marker.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/map.h"
#include "catenae/planar_section.h"

#if CATENAE_LAYOUT_KIND == 4
#include "catenae/biquad.h"
#include "lowpass_table.h"
#endif
#if CATENAE_LAYOUT_KIND == 5
#include "catenae/fir.h"
#include "fir_tables.h"
#endif
#if CATENAE_LAYOUT_KIND == 6
#include "catenae/oversample.h"
#include "overdrive.h"
#endif
#if CATENAE_LAYOUT_KIND == 7
#include "catenae/fft_fir.h"
#include "catenae/steady.h"
#include "fft_fir_tables.h"
#endif

#ifndef CATENAE_LAYOUT_KIND
#define CATENAE_LAYOUT_KIND 0
#endif
#ifndef CATENAE_LAYOUT_CH
#define CATENAE_LAYOUT_CH 2
#endif
#ifndef CATENAE_LAYOUT_PLANAR
#define CATENAE_LAYOUT_PLANAR 0
#endif

extern "C" void catenae_host_counted() noexcept;

namespace {

    constexpr std::size_t k_ch     = CATENAE_LAYOUT_CH;
    constexpr std::size_t k_frames = 16384;
    constexpr std::size_t k_call   = 128;
    constexpr std::size_t k_steps  = k_frames / k_call;

    constexpr std::uint64_t k_fnv_basis = 14695981039346656037ull;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

#if CATENAE_LAYOUT_KIND == 2 || CATENAE_LAYOUT_KIND == 3
    struct halve {
        constexpr float operator()(float x) const noexcept { return x * 0.5f; }
    };
#endif

    template <std::size_t Ch>
    constexpr auto make_config() noexcept {
#if CATENAE_LAYOUT_KIND == 0
        return catenae::chain(catenae::gain<Ch>{0.5f, 96});
#elif CATENAE_LAYOUT_KIND == 1
        return catenae::chain(catenae::delay<Ch, 32>{});
#elif CATENAE_LAYOUT_KIND == 2
        return catenae::chain(catenae::map<Ch, halve>{halve{}});
#elif CATENAE_LAYOUT_KIND == 3
        return catenae::gain<Ch>{0.5f, 96} | catenae::delay<Ch, 32>{} | catenae::map<Ch, halve>{halve{}};
#elif CATENAE_LAYOUT_KIND == 4
        return catenae::chain(catenae::butterworth<Ch, 4>{example::lowpass_at_0_1});
#elif CATENAE_LAYOUT_KIND == 5
        return catenae::chain(catenae::fir<Ch, 63>{example::fir_lowpass});
#elif CATENAE_LAYOUT_KIND == 6
        return catenae::chain(catenae::oversample<4, catenae::map<Ch, example::soft_clip>>{
            catenae::map<Ch, example::soft_clip>{example::soft_clip{4.0f}}});
#elif CATENAE_LAYOUT_KIND == 7
        return catenae::chain(
            catenae::steady<catenae::fft_fir<Ch, 255>>{catenae::fft_fir<Ch, 255>{example::long_lowpass_fft}});
#else
#error "CATENAE_LAYOUT_KIND: 0 gain, 1 delay, 2 map, 3 strip, 4 biquad, 5 fir, 6 overdrive or 7 fft_fir"
#endif
    }

    /// After reset: the gain kinds are set to a new level once, so the first block ramps.
    template <typename Config>
    void prepare(Config& config) noexcept {
#if (CATENAE_LAYOUT_KIND == 0 || CATENAE_LAYOUT_KIND == 3) && CATENAE_LAYOUT_PLANAR == 2
        config.template stage<0>().mono().template stage<0>().set_level(0.75f); // in the section's mono chain
#elif CATENAE_LAYOUT_KIND == 0 || CATENAE_LAYOUT_KIND == 3
        config.template stage<0>().set_level(0.75f);
#else
        static_cast<void>(config);
#endif
    }

#if CATENAE_LAYOUT_PLANAR == 1
    /// N copies of one mono configuration: a configuration has no default constructor.
    template <typename T, std::size_t... I>
    constexpr std::array<T, sizeof...(I)> copies(const T& value, std::index_sequence<I...>) noexcept {
        return {{(static_cast<void>(I), value)...}};
    }

    using config_t                                        = decltype(make_config<1>());
    CATENAE_CONSTINIT std::array<config_t, k_ch> s_config = copies(make_config<1>(), std::make_index_sequence<k_ch>{});
    CATENAE_CONSTINIT std::array<catenae::memory_of<config_t>, k_ch> s_memory{};
    CATENAE_CONSTINIT std::array<std::array<float, k_call>, k_ch> s_in{};
    CATENAE_CONSTINIT std::array<std::array<float, k_call>, k_ch> s_out{};
#elif CATENAE_LAYOUT_PLANAR == 2
    using config_t                      = decltype(catenae::chain(catenae::planar<k_ch>(make_config<1>())));
    CATENAE_CONSTINIT config_t s_config = catenae::chain(catenae::planar<k_ch>(make_config<1>()));
    CATENAE_CONSTINIT catenae::memory_of<config_t> s_memory{};
    CATENAE_CONSTINIT std::array<float, k_call * k_ch> s_in{};
    CATENAE_CONSTINIT std::array<float, k_call * k_ch> s_out{};
#else
    using config_t                      = decltype(make_config<k_ch>());
    CATENAE_CONSTINIT config_t s_config = make_config<k_ch>();
    CATENAE_CONSTINIT catenae::memory_of<config_t> s_memory{};
    CATENAE_CONSTINIT std::array<float, k_call * k_ch> s_in{};
    CATENAE_CONSTINIT std::array<float, k_call * k_ch> s_out{};
#endif

    /// The input sample of the next channel and frame: xorshift, the same in both layouts.
    float next_input(std::uint32_t& s) noexcept {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return static_cast<float>(s / 256u) / 16777216.0f - 0.5f;
    }

    /// FNV-1a-64 over whole 32-bit words, as DspTap's layout bench folds: a byte at a time,
    /// this loop crashes the Hexagon compiler's software pipeliner (CodeLinaro clang 19.1.5).
    std::uint64_t fold(std::uint64_t hash, float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        hash ^= bits;
        hash *= k_fnv_prime;
        return hash;
    }

    float output(std::size_t frame, std::size_t channel) noexcept {
#if CATENAE_LAYOUT_PLANAR == 1
        return s_out[channel][frame];
#else
        return s_out[frame * k_ch + channel];
#endif
    }

} // namespace

// One step, between the markers: what is counted.
extern "C" CATENAE_PROBE_NOINLINE void catenae_host_counted() noexcept {
    catenae_call_begin();
#if CATENAE_LAYOUT_PLANAR == 1
    for (std::size_t c = 0; c < k_ch; ++c) {
        static_cast<void>(s_config[c].process(s_memory[c], s_in[c], s_out[c]));
    }
#else
    static_cast<void>(s_config.process(s_memory, s_in, s_out));
#endif
    catenae_call_end();
}

int main() {
#if CATENAE_LAYOUT_PLANAR == 1
    for (std::size_t c = 0; c < k_ch; ++c) {
        s_config[c].reset(s_memory[c].state);
        prepare(s_config[c]);
    }
#else
    s_config.reset(s_memory.state);
    prepare(s_config);
#endif
    std::array<std::uint64_t, k_ch> hash{};
    hash.fill(k_fnv_basis);
    std::uint32_t seed    = 0x2545F491u;
    bool          nonzero = false;
    bool          finite  = true;
    for (std::size_t step = 0; step < k_steps; ++step) {
        for (std::size_t i = 0; i < k_call; ++i) {
            for (std::size_t c = 0; c < k_ch; ++c) {
#if CATENAE_LAYOUT_PLANAR == 1
                s_in[c][i] = next_input(seed);
#else
                s_in[i * k_ch + c] = next_input(seed);
#endif
            }
        }
        catenae_host_counted();
        for (std::size_t c = 0; c < k_ch; ++c) {
            for (std::size_t i = 0; i < k_call; ++i) {
                const float v = output(i, c);
                hash[c]       = fold(hash[c], v);
                nonzero       = nonzero || v < 0.0f || v > 0.0f;
                finite        = finite && std::isfinite(v);
            }
        }
    }
    std::uint64_t all = k_fnv_basis;
    for (const std::uint64_t h : hash) {
        all ^= h;
        all *= k_fnv_prime;
    }
    const auto high = static_cast<std::uint32_t>(all / 4294967296ull);
    const auto low  = static_cast<std::uint32_t>(all % 4294967296ull);
    std::printf("CATENAE_ICOUNT_DONE ok=%d checksum=%08" PRIx32 "%08" PRIx32 " brackets=%u\n",
                finite && nonzero ? 1 : 0, high, low, static_cast<unsigned>(k_steps));
    return finite && nonzero ? 0 : 1;
}
