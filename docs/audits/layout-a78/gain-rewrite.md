# The gain stage, rewritten

Measured locally on 2026-11-25, not in CI. `layout-spike-tables.md` shows a stateless gain
far cheaper planar than interleaved on wide vector units (0.28 on the M55, 0.36 on the
Cortex-A78, 0.47 on x86-64 at 6 channels). That was the benchmark's loop, not the layout: its
interleaved gain kept the per-channel gains in the stage and wrote its output through a
pointer of the same type, so the compiler had to assume each write might change a gain.

Instructions for 16,384 frames in calls of 128, every variant's output bit-identical:

| Loop | ch | x86-64 | Cortex-A78 | M55 | M33 | M4F |
|---|---:|---:|---:|---:|---:|---:|
| Interleaved, as first benchmarked | 2 | 66,560 | 28,800 | 33,536 | 198,528 | 198,528 |
| | 6 | 363,648 | 461,184 | 444,800 | 460,800 | 460,800 |
| | 12 | 658,816 | 854,528 | 838,016 | 854,016 | 854,016 |
| Interleaved, gains copied to locals | 2 | 52,992 | 53,504 | 35,968 | 166,144 | 166,144 |
| | 6 | 111,488 | 87,296 | 118,528 | 363,520 | 363,392 |
| | 12 | 217,216 | 169,216 | 232,064 | 659,200 | 659,072 |
| Interleaved, one flat loop over the gain pattern tiled to lcm(Ch, 16) samples | 2 | 40,576 | 30,848 | 40,064 | 108,800 | 108,800 |
| | 6 | 92,160 | 66,176 | 188,800 | 670,592 | 670,592 |
| | 12 | 178,176 | 127,616 | 315,776 | 1,276,800 | 1,276,800 |
| The same, walked in fixed runs of 16 | 6 | | | 248,832 | 697,216 | 697,216 |
| | 12 | | | 439,296 | 1,330,048 | 1,330,048 |
| Planar, as first benchmarked | 2 | 57,600 | 54,784 | 41,856 | 200,832 | 200,832 |
| | 6 | 171,008 | 163,328 | 122,880 | 599,680 | 599,680 |
| | 12 | 339,200 | 324,608 | 244,096 | 1,197,952 | 1,197,952 |
| Planar, each channel in fixed runs of 16 | 2 | 44,288 | 33,152 | 72,192 | 110,592 | 110,464 |
| | 6 | 129,280 | 99,072 | 103,808 | 328,704 | 328,576 |
| | 12 | 255,232 | 195,840 | 205,184 | 655,872 | 655,744 |

Each layout's best loop on each core gives planar over interleaved of 1.09 / 1.40 / 1.43 on
x86-64, 1.15 / 1.50 / 1.53 on the A78, 1.25 / 0.88 / 0.88 on the M55 and 1.02 / 0.90 / 1.00 on
the M33 and M4F, at 2, 6 and 12 channels. No one form is best everywhere, and Hexagon was not
measured.

The same idea on the spike's FFT FIR (the write position and buffer pointers in locals, the
spectrum and the response declared not to overlap) moved its host count by under 1 %:
interleaved 5,619,712 to 5,571,584, 16,545,792 to 16,544,640 and 32,885,632 to 32,834,432 at
2, 6 and 12 channels, planar 5,823,616 to 5,792,128, 17,559,680 to 17,367,680 and 35,002,496
to 34,418,816; output bits unchanged.

## How

- **x86-64 and the A78:** the file below, built `-O3 -DNDEBUG -DCH=<n> -DVAR=<v>` with
  `g++-13` 13.3 and counted by callgrind (`--toggle-collect='*layout_step*'`), or with
  `aarch64-linux-gnu-g++` 13.3 `-mcpu=cortex-a78 -static` and counted as in this directory's
  `README.md` (`-cpu cortex-a76`). `call_marker.h` is DspTap's `bench/call_marker.h`.
  VAR 0 is the benchmark's loop, 1 gains in locals, 3 the tiled loop (its tile built once),
  9 planar as benchmarked, 8 planar in runs of 16.
- **The M-class cores:** DspTap `origin/main` (`b26a7db`), `bench/layout/layout_main.cpp`
  with `bench_gain::process` replaced by each variant in turn (the tiled ones build their
  tile at the top of each call; a mono stage is the run-of-16 loop), built per key as
  `bench.yml` does with `-DTAP_DSP_FFT_CMSIS=OFF`, and counted with
  `scripts/icount.py --target <key> --prefix tap_dsp_layout_gain --report-only` under the
  packaged `qemu-system-arm` 8.2.2. Unchanged, this reproduced run 173's gain figures exactly.

```cpp
// Gain over interleaved frames, several ways, against Ch mono gains (planar).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <vector>
#include "call_marker.h"
#ifndef CH
#define CH 6
#endif
#ifndef VAR
#define VAR 0
#endif
constexpr std::size_t Ch = CH, k_frames = 16384, k_call = 128;
#define NOINLINE __attribute__((noinline))

template <std::size_t C> struct gain_base {            // V0: the benchmark's loop
    std::array<float, C> g{};
    NOINLINE std::size_t process(const float* in, std::size_t n, float* out) noexcept {
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t c = 0; c < C; ++c) out[i * C + c] = in[i * C + c] * g[c];
        return n;
    }
};
template <std::size_t C> struct gain_locals {          // V1: gains copied to locals first
    std::array<float, C> g{};
    NOINLINE std::size_t process(const float* in, std::size_t n, float* out) noexcept {
        const std::array<float, C> k = g;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t c = 0; c < C; ++c) out[i * C + c] = in[i * C + c] * k[c];
        return n;
    }
};
template <std::size_t C> struct gain_restrict {        // V2: locals and restrict
    std::array<float, C> g{};
    NOINLINE std::size_t process(const float* __restrict in, std::size_t n, float* __restrict out) noexcept {
        const std::array<float, C> k = g;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t c = 0; c < C; ++c) out[i * C + c] = in[i * C + c] * k[c];
        return n;
    }
};
template <std::size_t C> struct gain_tiled {           // V3: one flat loop over a tiled pattern
    static constexpr std::size_t P = std::lcm(C, std::size_t{16});
    std::array<float, C> g{};
    alignas(64) std::array<float, P> tile{};
    void prepare() noexcept { for (std::size_t j = 0; j < P; ++j) tile[j] = g[j % C]; }
    NOINLINE std::size_t process(const float* __restrict in, std::size_t n, float* __restrict out) noexcept {
        const std::size_t total = n * C;
        std::size_t s = 0;
        for (; s + P <= total; s += P)
            for (std::size_t j = 0; j < P; ++j) out[s + j] = in[s + j] * tile[j];
        for (std::size_t j = 0; s < total; ++s, ++j) out[s] = in[s] * tile[j];
        return n;
    }
};
template <std::size_t C> struct gain_uniform {         // V4: one gain for every channel (lower bound)
    std::array<float, C> g{};
    NOINLINE std::size_t process(const float* __restrict in, std::size_t n, float* __restrict out) noexcept {
        const float k = g[0];
        for (std::size_t s = 0; s < n * C; ++s) out[s] = in[s] * k;
        return n;
    }
};
struct gain_mono_chunked {                              // V8's stage: a mono gain written as carefully
    float g = 1.0f;
    NOINLINE std::size_t process(const float* __restrict in, std::size_t n, float* __restrict out) noexcept {
        const float k = g;
        std::size_t s = 0;
        for (; s + 16 <= n; s += 16)
            for (std::size_t j = 0; j < 16; ++j) out[s + j] = in[s + j] * k;
        for (; s < n; ++s) out[s] = in[s] * k;
        return n;
    }
};
template <typename F> NOINLINE void layout_step(F&& f) { tap_dsp_call_begin(); f(); tap_dsp_call_end(); }

int main() {
    std::vector<float> in(k_frames * Ch), out(k_frames * Ch);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.001f * static_cast<float>((i * 2654435761u) % 2001u) - 1.0f;
    std::array<float, Ch> g{};
    for (std::size_t c = 0; c < Ch; ++c) g[c] = 0.5f + 0.01f * static_cast<float>(c);
#if VAR == 8   // planar, the mono stage written as carefully as V3
    std::vector<float> pin(k_frames * Ch), pout(k_frames * Ch);
    for (std::size_t c = 0; c < Ch; ++c) for (std::size_t f = 0; f < k_frames; ++f) pin[c * k_frames + f] = in[f * Ch + c];
    std::array<gain_mono_chunked, Ch> st{};
    for (std::size_t c = 0; c < Ch; ++c) st[c].g = g[c];
    for (std::size_t f = 0; f < k_frames; f += k_call)
        layout_step([&] { for (std::size_t c = 0; c < Ch; ++c) st[c].process(pin.data() + c * k_frames + f, k_call, pout.data() + c * k_frames + f); });
    for (std::size_t c = 0; c < Ch; ++c) for (std::size_t f = 0; f < k_frames; ++f) out[f * Ch + c] = pout[c * k_frames + f];
#elif VAR == 9   // planar: Ch mono stages over contiguous channels
    std::vector<float> pin(k_frames * Ch), pout(k_frames * Ch);
    for (std::size_t c = 0; c < Ch; ++c) for (std::size_t f = 0; f < k_frames; ++f) pin[c * k_frames + f] = in[f * Ch + c];
    std::array<gain_base<1>, Ch> st{};
    for (std::size_t c = 0; c < Ch; ++c) st[c].g[0] = g[c];
    for (std::size_t f = 0; f < k_frames; f += k_call)
        layout_step([&] { for (std::size_t c = 0; c < Ch; ++c) st[c].process(pin.data() + c * k_frames + f, k_call, pout.data() + c * k_frames + f); });
    for (std::size_t c = 0; c < Ch; ++c) for (std::size_t f = 0; f < k_frames; ++f) out[f * Ch + c] = pout[c * k_frames + f];
#else
#if VAR == 0
    gain_base<Ch> st{g};
#elif VAR == 1
    gain_locals<Ch> st{g};
#elif VAR == 2
    gain_restrict<Ch> st{g};
#elif VAR == 3
    gain_tiled<Ch> st{g, {}}; st.prepare();
#elif VAR == 4
    gain_uniform<Ch> st{g};
#endif
    for (std::size_t f = 0; f < k_frames; f += k_call)
        layout_step([&] { st.process(in.data() + f * Ch, k_call, out.data() + f * Ch); });
#endif
    std::uint64_t h = 1469598103934665603ull;
    for (float v : out) { std::uint32_t u; __builtin_memcpy(&u, &v, 4); h = (h ^ u) * 1099511628211ull; }
    std::printf("TAP_DSP_ICOUNT_DONE ok=1 checksum=%016llx\n", static_cast<unsigned long long>(h));
}
```
