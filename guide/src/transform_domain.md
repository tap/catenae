<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# The transform domain

Some processing is easier on a signal's spectrum than on its samples. The signal is cut
into overlapping windowed frames, each frame is transformed, its bins are changed, and
the frames are transformed back and added together. This page is about what that region
does to a callback, and the numbers it should state.

The example is a mono spectral gate in `float`: frames of 512 samples every 128, under
the periodic Hann window, 257 bins a frame. A bin whose power reaches a threshold
passes, and one below it is turned down 20 dB. The gate is the noise gate applied bin by
bin, as in Zölzer (ed.), "DAFX: Digital Audio Effects" (2nd ed., chapter 7). The
synthesis is the least-squares rule of Griffin and Lim, "Signal estimation from modified
short-time Fourier transform" (1984). The windows are those of Harris, "On the use of
windows for harmonic analysis with the discrete Fourier transform" (1978). Nothing is
taken from a product.

## What you would naturally write

You would write a class. Its constructor makes the window with `std::cos`, asks a
desktop FFT library for a plan, and sizes a handful of vectors. Its callback collects
samples, and processes a frame whenever a hop is full.

```cpp
std::vector<float> window, fifo, overlap;          // sized in the constructor
std::vector<std::complex<float>> bins;
if (++filled == hop) {                             // in the callback
    plan.forward(frame, bins);
    for (auto& b : bins) { if (std::norm(b) < threshold) b *= floor; }
    plan.inverse(bins, frame);
    for (int i = 0; i < n; ++i) { overlap[i] += frame[i] * window[i] / 1.5f; }
}
```

The 1.5 is the sum of the squared Hann windows at a hop of a quarter frame. You worked
it out once, for this window and this overlap. The gate decides each bin afresh every
frame.

## What it costs on the target

- **A heap, and a plan made at run time.** The vectors and the plan are allocations, and
  the target has no heap for them, as [Where memory lives](where_memory_lives.md)
  explains. The window is 512 calls of `cos` at start-up, for numbers known when the
  program was written.
- **A burst.** The hop is 128 frames. Under a callback of 64, one call only collects,
  and the next runs two transforms of 512 points. The frames it returns are as uneven
  as the work, unless the class also keeps a FIFO of its own, whose delay nobody wrote
  down. [Bursts and buffering](bursts_and_buffering.md) has the terms.
- **A latency that is nowhere.** The region is late by its frame less its hop, plus
  whatever the FIFO adds. A dry path beside it must be late by the same amount, and the
  class does not say what that is.
- **A constant for one window.** Change the window or the hop and the 1.5 is wrong. The
  output is then scaled, or it ripples at the hop's rate, and nothing fails.
- **A gate that chatters.** A bin of noise near the threshold passes in one frame and
  not in the next. That switching is audible.

Nothing removes the burst's work. The harness runs the region at the end of this page,
the one inside `steady`, over 1000 callbacks of 64 frames: a tone in noise, 64,000
samples, 500 hops (`bench/icount/denoise_main.cpp`). `denoise_f32` is the whole run, and
`callmax_denoise_f32` its worst single callback. The counts are instructions executed,
and on Hexagon packets.

| Key | `denoise_f32` | `callmax_denoise_f32` |
|---|--:|--:|
| `m4-softfp` | 523,166,052 | 1,021,300 |
| `m4f` | 29,519,587 | 55,913 |
| `m33` | 29,475,064 | 55,814 |
| `m55` | 27,938,592 | 53,117 |
| `hexagon` | 20,153,120 | 39,322 |

On the Cortex-M33 that is 461 instructions a sample, and 29,475 a callback on average:
the total divided by 64,000 and by 1000. The worst callback is 55,814, about 1.9 times
the average by the ratio of the two. It is the one that completes a hop. A transform of
512 points is 17,151 instructions at worst on the M33 (`callmax_fft_f32_512`), so the two
transforms are 34,302 of it. A deadline is met by the worst callback, so 55,814 is the
number to budget.

The numbers are from
[bench run 46102211148](https://github.com/tap/catenae/actions/runs/46102211148), and are
recorded in `bench/README.md`. These reproduce the M33's counts;
[Measuring](measuring.md) explains the script.

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

## What catenae does instead

### A spectrum is a port

`stft<512, 128, 1>` takes a port of samples and writes a `spectrum<float, 1, 257>`: one
frame of 257 complex bins for every 128 samples. `istft` is the reverse. A stage between
them reads and writes spectra, and is otherwise a stage like any other. The gate is one,
written outside the library, as [Writing a stage](writing_a_stage.md) describes.

```cpp
{{#include ../../examples/spectral_gate.h:card}}
{{#include ../../examples/spectral_gate.h:stage}}
```

A spectrum joins only a spectrum. `stft | gain` stops the compiler with "a spectrum
cannot feed a time-domain port" (`tests/compile_fail/stft_spectrum_into_gain.cpp`).

### The region says what it is

```cpp
{{#include ../../examples/spectral_gate.h:region}}
```

The asserts are the facts the class kept to itself. The region is 1:1 and its input
group is the hop. It is not frame-synchronous, and the type says so. Its latency is 384
frames, which is 512 less 128, a constant of the type.

A host whose callback is a hop runs the region bare, 128 frames out for 128 in.

```cpp
{{#include ../../examples/denoise.cpp:bare}}
```

The same call with two arrays of 64 does not compile: "a fixed block must be a whole
number of the chain's input groups; wrap the chain in steady"
(`tests/compile_fail/stft_fixed_block_half_hop.cpp`). So a callback of any other size
runs the region inside `steady`, which returns n frames for n.

```cpp
{{#include ../../examples/denoise.cpp:steady}}
```

`steady` costs 127 frames, the hop less one, and the type says that too: 511 in all.
Because both latencies are `exact_latency`, `parallel` can delay a dry arm to match, as
[Rate and latency](rate_and_latency.md) shows. `steady` evens out the frames, not the
work. The worst callback in the table above is this chain's.

### The synthesis fits any window

`istft` multiplies each inverse frame by the window, adds the overlapping frames, and
divides each sample by the sum of the squared windows that cover it. That is Griffin
and Lim's rule: the least-squares signal of a changed spectrum, and the input itself of
an unchanged one. Its `reset` computes the divisors from whichever window it holds, one
for each sample of a hop. No constant is typed in.

So every window reconstructs at every legal hop. For noise within [-1, 1], `stft | istft`
returns its input within 4.2e-7 in `float` and 8.9e-16 in `double`, over every window at
512 by hops of 256, 128 and 64 (`ctest --test-dir build -R stft`). A legal hop divides
the frame and is at most half of it, so every sample is in two frames at least. Any
other does not compile (`stft_hop_not_dividing.cpp` and `stft_hop_above_half.cpp`, in
`tests/compile_fail/`).

### The window is in ROM

`window::hann` names a constant of the compiled core. A periodic window is symmetric,
so half of it is stored: 257 floats at 512, which is 1,028 bytes. The core has such a
table for five windows at every power of two from 16 to 8192, 100 tables and 491,640
bytes in all. Each is in a section of its own, and section garbage collection keeps only
the one a chain names.

On the M33 the image keeps 2,964 bytes of the core's constants: the window, and the
transform's tables at 512, 1,936 bytes. The region's state, inside `steady`, is 6,684
bytes and its workspace 5,148. Both are plain storage, all zero at rest, in `.bss` when
static. `process` reaches 648 bytes of stack by the static bound, 632 painted. These are
the `denoise` probe's figures.

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
scripts/ceilings.py measure --key m33 --build-dir build-size
```

Nothing in the region is in `double`. The link-stage gate finds no software `double` in
its image on the M4, the M4F and the M33: `link_gate.DenoiseImageHasNoSoftwareDouble`
(`ctest --test-dir build-17 -L link_gate`).

### A gate with ballistics

```cpp
{{#include ../../examples/spectral_gate.h:design}}
```

Each bin has an openness between 0 and 1. It rises by the attack step in a frame where
the bin reaches the threshold, and falls by the release step in a frame where it does
not. The bin's gain follows it, from the floor to 1. The example's gate opens over 2
frames and closes over 8, so a bin near the threshold drifts and does not switch. The
state at rest is all zero, which is every bin closed, so the stage needs no `reset` of
its own.

The threshold is a power, set against the bin a full-scale sine reaches under this
window. The design is a constant.

### What it does

The example plays a tone at the centre of bin 32, which is 3 kHz at 48 kHz, in noise. It
runs the bare region in hops and the steady one in callbacks of 48, and prints this:

```text
region latency 384 frames bare, 511 in steady
steady is the bare region 127 frames later, bit for bit: yes
signal to noise 23.7 dB in, 40.8 dB out: the noise 17.1 dB down
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target catenae_example_denoise
build/examples/catenae_example_denoise
```

The floor predicts the last line. The tone holds 3 of the 257 bins open, and the other
254 close to 20 dB down, a hundredth of their power. So (3 + 2.54) / 257 of the noise's
power is left, which is 16.7 dB down. The battery's comment in
`examples/spectral_gate_battery.cpp` states the same arithmetic.

### What was given up

- **Latency.** 384 frames bare and 511 in `steady`: 8 and 10.6 ms at 48 kHz, by
  arithmetic.
- **An even load.** The callback that completes a hop does the work of two.
- **A geometry chosen at run time.** The frame, the hop and the channel count are in the
  type.
- **Fixed point, for now.** The region runs in `float` and `double`. Fixed-point spectra
  wait for milestone C6.

The cards are on the pages of [`stft.h`](api/stft_8h.html) and
[`window.h`](api/window_8h.html).
