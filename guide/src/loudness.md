<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Loudness and true peak

[Meters](meters.md) read a level. A broadcaster asks for two numbers that a level meter
does not give: how loud a whole programme is, and how high its waveform goes between its
samples. This page is about meters that remember a programme of any length, in memory of
a fixed size.

The example is a stereo programme at 48 kHz with two meters. Loudness is ITU-R BS.1770-4's
measure, gated, with the three readings of EBU Tech 3341: momentary over 400 ms,
short-term over 3 s, and integrated since reset. True peak is BS.1770-4's Annex 2: the
signal interpolated four times by the Annex's own filter. Nothing is taken from a
product.

## What you would naturally write

You would write a function over a whole recording. It filters the samples with the
standard's two sections, K-weighting, and takes the mean square of each 400 ms block.
BS.1770-4 gates twice: blocks below -70 LUFS are dropped, then blocks more than 10 LU
below the mean of the rest. The second gate needs every block again, so you would keep
them.

```cpp
std::vector<float> blocks;                               // one loudness per 100 ms, for ever
blocks.push_back(-0.691f + 10.0f * std::log10(power));   // in the callback
```

Three more things come naturally. The coefficients are pasted from the standard's
tables, and used at whatever rate the device runs. The peak reading is the largest
sample. Each block is turned into LUFS where it is made, on the audio thread.

## What it costs on the target

- **A record that grows.** The gating blocks step every 100 ms, ten a second, so an hour
  of programme is 36,000 of them: 144,000 bytes as `float`, and more for a longer
  programme. That is arithmetic, not a measurement. The target has no heap for the
  vector to grow in, as [Where memory lives](where_memory_lives.md) explains, and a meter
  left running has no last block.
- **A peak that is not the peak.** The waveform goes higher between samples than at
  them. EBU Tech 3341's signal 16 is a sine at a quarter of the rate, of amplitude 0.5,
  at a phase of 45 degrees. Its samples peak at -9.03 dBFS. The true-peak meter reads
  -5.98 dB TP, where the Tech expects -6.0, +0.2/-0.4. The sample peak is 3 dB low.
  `Tech3341SignalsReadTheCardsNumbers` pins both the sample peak and the reading
  (`ctest --test-dir build -R true_peak`).
- **A table for one rate.** The standard's coefficients are for 48 kHz. At another rate
  they are a different filter.
- **A design in `double`.** Making the K-weighting at another rate is design arithmetic.
  `k_weighting_design<S>::make(rate)` costs 44.3 million instructions on the Cortex-M33,
  whose `double` is software, and 1.3 million on the M55. It reaches 2,240 bytes of
  stack on the M33 (`callmax_design_k_weighting` and the `designers` probe).
- **A `log10` in the deadline**, as on the page before.

What remains is the cost of the meters themselves. The harness runs them over 1000
callbacks of 64 stereo frames of noise, 128,000 samples
(`bench/icount/loudness_main.cpp`).

| Workload on the Cortex-M33 | Instructions | A sample |
|---|--:|--:|
| `loudness_f32`: loudness and true peak together | 20,600,829 | 161 |
| `true_peak_q15`: true peak alone, in Q15 | 27,480,547 | 215 |
| `true_peak_q31`: the same in Q31 | 27,501,256 | 215 |

True peak is 48 products a frame for each channel. In a fixed-point format each is one
integer multiply-add.

The numbers are from
[bench run 46089381559](https://github.com/tap/catenae/actions/runs/46089381559), and are
recorded in `bench/README.md`. These reproduce the M33's counts;
[Measuring](measuring.md) explains the script.

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

## What catenae does instead

### Two analyzers, two taps

Both meters are analyzers, each in a `tap`, as the meters of the page before are. The
audio passes through both unchanged.

```cpp
{{#include ../../examples/loudness.h:meters}}
```

`true_peak_meter<2, 960, 48000>` has two channels, a hop of 960 frames and a window of
48,000: a result every 20 ms, over the last second. `loudness_meter<2, 960, 5>` has the
same hop, and five hops to the standard's step of 100 ms.

The step would do as the hop. The example's is shorter because a window is read only
where a hop ends. With a hop of 100 ms, a 400 ms tone can sit up to 50 ms off the
nearest window. That window then holds 350 ms of it, and reads 0.58 dB low, which is
arithmetic. At 20 ms the meter reads Tech 3341's momentary tests 13 and 14 within their
0.1 LU (`Tech3341LargestReadingsAtTwentyMilliseconds` in `tests/test_loudness.cpp`).

### The K-weighting is a table

```cpp
{{#include ../../examples/loudness.h:k_weighting}}
```

These are the standard's two sections in `float`, with the numbers the design stores
beside them, in ROM. `from_table` checks them as it compiles, and the battery checks
them against `make(48000.0)` on every CI leg (`ExampleTableIsTheStandards`).

With the table in ROM and the gating's `log10` in `float`, an image of these two meters
links no software `double`. The link-stage gate checks it on every Arm core:
`link_gate.LoudnessImageHasNoSoftwareDouble` passed in the same bench run
(`ctest --test-dir build-17 -L link_gate`).

For another rate, `k_weighting_design<S>::make(rate)` carries the design there. It
inverts the bilinear transform of each section, and makes the section again at the new
rate. At 44.1 kHz the result departs from the 48 kHz response by at most 0.0016 dB
between 20 Hz and 20 kHz. It runs outside the audio path: on a host that prints the
table, or on a thread with no deadline. `loudness_meter::make(rate)` takes only a rate
whose 100 ms is the type's step.

### Every block, in 4,000 bytes

The meter does not keep its blocks. It counts them. Each block above -70 LUFS adds one
to a histogram of 1,000 bins, each 0.1 LU wide. The integrated loudness is two passes
over the bins: one for the mean above the absolute gate, and one for the mean above
that less 10 LU.

The histogram is 1,000 counts of 32 bits, 4,000 bytes, for an hour or a year. On the M33
the two meters' state is 5,304 bytes and their workspace 516, and `process` reaches 220
bytes of stack static and 172 painted (the `loudness` probe,
`scripts/ceilings.py measure`). The state is all zero at rest, and in `.bss` when
static.

Choosing a block's bin takes one logarithm, ten times a second. It is the only one on
the audio thread.

### Powers out, LUFS at the reader

A loudness result is three mean squares. A true-peak result is each channel's largest
interpolated magnitude. Neither is in decibels.

```cpp
{{#include ../../examples/loudness.cpp:reading}}
```

The audio thread posts the struct through a mailbox when a hop has completed, as
[Meters](meters.md) shows. The reader converts.

```cpp
{{#include ../../examples/loudness.cpp:read}}
```

`lufs()` and `dbtp()` are static functions of the meters' types, and need no state. The
example plays a 1 kHz tone on both channels, five seconds at -23 dBFS and then five at
-13. It prints this:

```text
momentary -23.0 LUFS, short-term -23.0 LUFS, integrated -23.0 LUFS; true peak -23.0 dBTP
momentary -13.0 LUFS, short-term -13.0 LUFS, integrated -15.6 LUFS; true peak -13.0 dBTP
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target catenae_example_loudness
build/examples/catenae_example_loudness
```

The momentary and short-term readings follow the tone. The integrated reading holds the
whole programme since reset, the quiet half with the loud.

### Subnormals, once

The K-weighting is recursive, as a biquad is, and its state decays through subnormals
when the programme goes silent. The example's `main` sets the processor's flush-to-zero
mode once, before the first callback. [The audio callback](audio_callback.md) explains
the mode. The build states it with `CATENAE_FLUSH_TO_ZERO`, and at the audit level the
meter checks that the host does as the build says.

## What was given up

- **The exact gated mean.** Each block is read at its bin's centre, so the integrated
  value is within 0.05 LU of BS.1770-4's own. A block a rounding from a bin's edge may
  fall either side on another target.
- **A peak on a sample.** The Annex's interpolated points fall an eighth of a frame off
  the samples, so a peak that lies on a sample reads below it. Signal 15 reads -6.22 dB
  TP where its samples peak at -6.02. That is the Annex's filter as published, and a test
  pins it.
- **A rate chosen at run time.** The hop and the step are in the type.
- **Loudness in fixed point, for now.** True peak runs in `float`, `double`, Q15, Q31
  and Q4.27. The Annex's taps are multiples of 2^-13, so its fixed-point sums are exact
  integers. Loudness runs in `float` and `double`, and its fixed-point recursion waits
  for milestone C6.

The cards are on the pages of [`loudness_meter.h`](api/loudness__meter_8h.html),
[`loudness_design.h`](api/loudness__design_8h.html) and
[`true_peak_meter.h`](api/true__peak__meter_8h.html). The analyzer contract is
[its own page](api/analyzer_contract.html).
