<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Dynamics

A dynamics stage turns the signal down, or up, by an amount that follows the signal's own
level. A compressor narrows the distance between loud and quiet, an expander widens it,
and a limiter promises a ceiling. This page takes the two decisions that shape catenae's
three: where the level is taken, and what a ceiling costs.

The stages are written from Giannoulis, Massberg and Reiss, "Digital Dynamic Range
Compressor Design: A Tutorial and Analysis" (JAES 60(6), 2012), and from Reiss and
McPherson, "Audio Effects" (2014). Nothing is taken from a product.

## One level for the frame

The example is a stereo bus compressor: a threshold of -18 dB, a ratio of 4, a knee of
6 dB.

### What you would naturally write

You would write a compressor for one channel and make two of them. Each takes its sample's
magnitude to decibels with `log10`, finds the gain from the curve, smooths it, and comes
back with `pow`. catenae can say the same thing, as a mono stage for each channel.

```cpp
{{#include ../../examples/dynamics.cpp:unlinked}}
```

### What it costs on the target

- **The stereo image moves.** The example feeds a tone with the left channel at 0 dBFS and
  the right at -12 dBFS. Each compressor cuts by its own channel's level, so the louder
  channel is cut more. The 12.0 dB between them comes out as 3.0 dB.
- **`log10` and `pow` are `double` unless you say otherwise.** On a core whose
  floating-point unit is single precision, each is a library routine that works with
  integers, and two channels call each twice a frame. No number is claimed here: none
  was measured. [What arithmetic costs](what_arithmetic_costs.md) has what `double` costs
  on these cores.
- **A design needs arithmetic.** A coefficient made from a time is an `exp`, in `double`.
  Made inside the callback, it is part of the deadline.

These reproduce the 3.0 dB:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target catenae_example_dynamics
build/examples/catenae_example_dynamics
```

```text
left over right, 12.0 dB in: linked 12.0 dB, each channel its own 3.0 dB
a +6 dBFS burst: no look-ahead, 5.6 dB over the ceiling; the limiter's peak 0.891186, its ceiling 0.891251
the expander on a -70 dB floor: 40.0 dB cut
the strip: latency 48 frames; a voice 1028 bytes; the workspace 516 bytes
CATENAE_EXAMPLE_DONE ok=1
```

The output is the same on the Cortex-M33 under QEMU, and the example runs on every CI
leg.

### What catenae does instead

A dynamics stage has a channel count, and takes one level for each frame: the logarithm of
the largest magnitude among the frame's channels. Every channel is multiplied by the one
gain that follows. `compressor<2>` is a stereo compressor, not two mono ones, and the
first line of the output is its result: 12.0 dB in, 12.0 dB out.

What the stage runs is a *design*, a small constant, as a filter's is.

```cpp
{{#include ../../examples/dynamics_tables.h:designs}}
```

Three designs make a strip: the expander, the compressor, and the limiter of the next
section.

```cpp
{{#include ../../examples/dynamics_tables.h:strip}}
```

It goes in a callback like any other chain. A voice's state and the thread's workspace
are plain storage in `.bss`.

```cpp
{{#include ../../examples/dynamics.cpp:callback}}
```

### Nepers, and one `log` a frame

The paper works in decibels. The stages work in *nepers*, the natural logarithm of an
amplitude: 1 Np is 8.686 dB. A frame then costs one `log` and one `exp`. In `float` they
are `logf` and `expf`, and the stage's own arithmetic is `float`. On a core whose unit is
single precision only, as the M33's is, newlib's `logf` and `expf` stay in `float` too; on
the M55, whose unit has `double`, they take the `double` path in hardware, which is part of
why it is the cheaper core below. The strip's probe image links no software `double`:
`link_gate.DynamicsImageHasNoSoftwareDouble` passes on every Arm core.

### Designs are made off the audio path

The tables above are designs in flash, with the spec each came from in its comment.
`from_table` checks them: a table out of range does not initialise a constant.

`make()` is the other way, for a spec known only at run time. It works in `double`, a few
`exp` and `expm1`, and it can refuse. On the M33 the worst call is 11,225 instructions
and 452 bytes of stack for a compressor, 6,617 and 300 for a limiter, and 11,262 and 476
for an expander. That belongs outside the callback, and `set_design()` applies the result
between calls.

Times are in frames, not milliseconds. The caller knows its rate, and converts: 48 frames
is 1 ms at 48 kHz.

### The expander rests closed

The expander is the compressor's mirror, downward: below its threshold the level is
pushed down further. It has a *range*, the most it will cut, and a *hold*, the frames it
waits before closing. At a ratio of infinity it is a gate. In the example it cuts a
noise floor at -70 dB by its whole range, 40.0 dB.

A compressor rests at zero: no reduction. An expander rests closed, at its range.

## A ceiling that holds

The last stage before a converter has one job: no sample above the ceiling, here -1 dB,
which is 0.891 of full scale.

### What you would naturally write

You would use the compressor you have, with a ratio of infinity and a short attack.

```cpp
{{#include ../../examples/dynamics.cpp:fast}}
```

### What it costs on the target

The example plays a quiet tone, then 480 frames at +6 dBFS. That compressor lets the
burst out 5.6 dB over the ceiling. It is the second line of the output above, from the
same commands.

The reason is the order of events. The gain follows the level, and the level is known
only when the loud sample has arrived. An attack of 1 ms is 48 frames in which the gain
is still on its way.

### What catenae does instead

`limiter<2, 48>` delays the signal by 48 frames, and takes its level from the frames that
have not come out yet. The gain is down before the peak arrives. On the same burst its
largest output sample is 0.891186, under the ceiling of 0.891251.

The look-ahead is in the type, from 1 frame to 4,096, and it is the stage's exact
latency. The strip's second assertion is that number: 48 frames, the limiter's alone,
because the compressor and the expander have a latency of exactly 0. A dry path beside the
strip is lined up by `parallel`, as in [Rate and latency](rate_and_latency.md).

### Why the ceiling holds

The ceiling is not a tuning. It follows from four steps, with a look-ahead of L frames:

1. The reduction each frame needs, rounded up to a grid of 2^-12 neper.
2. The largest of those over the last L + 1 frames. This is Lemire's streaming maximum
   filter (Nordic J. Computing 13(4), 2006).
3. The mean of the last L + 1 of those maxima.
4. The release.

Every maximum in that mean covers the frame about to leave, the one L frames back. So the
mean is at least that frame's need, and the mean is what makes the gain move smoothly.

A last step holds the output at the ceiling, against the rounding of `log` and `exp`. In
the battery it moved no sample, and it is pinned at 4 ULP at most
(`NoSampleExceedsTheCeiling` in `tests/test_dynamics.cpp`). Below the threshold the
limiter is a delay of L frames, bit for bit (`BelowTheThresholdItIsADelay`).

### What a frame costs

The harness runs each stage alone, and the strip, over 1000 callbacks of 64 stereo
frames: 128,000 samples, whose level steps between -70, -30, -12 and +6 dBFS. Every
design is a table. The counts are instructions executed, or packets on Hexagon.

| Target | `compressor_f32` | `expander_f32` | `limiter_f32` | `dynamics` |
|---|--:|--:|--:|--:|
| `m4-softfp` | 228,107,705 | 131,446,575 | 222,777,497 | 510,653,113 |
| `m4f` | 15,314,779 | 9,510,705 | 21,174,406 | 41,814,042 |
| `m33` | 15,374,501 | 9,635,643 | 20,778,127 | 41,866,374 |
| `m55` | 9,426,046 | 6,823,259 | 15,139,026 | 28,863,894 |
| `hexagon` | 33,974,218 packets | 34,965,907 packets | 39,548,056 packets | 107,403,921 packets |
| `host` | 11,244,041 | 8,048,683 | 16,380,395 | 31,630,736 |

The numbers are from
[bench run 46001165800](https://github.com/tap/catenae/actions/runs/46001165800), and are
recorded in `bench/README.md`. These reproduce the M33's counts and then its sizes:

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
scripts/ceilings.py measure --key m33 --build-dir build-size
```

[Measuring](measuring.md) explains the plugin and both scripts.

- **The strip is 327 instructions a sample on the M33.** The compressor is 120, the
  expander 75 and the limiter 162. The one `log` and one `exp` of a stereo frame are
  shared by its two samples. The M55 runs them 27 to 39 % cheaper.
- **A voice is 1,028 bytes and the workspace 516.** `process` reaches 96 bytes of stack by
  the static measure, 84 painted.

The layout benchmark of [Measuring](measuring.md) does not cover these stages. A planar
form would be one mono stage for each channel, which is the 3.0 dB of the first section.

### What was given up

`float` follows the golden model, `double`, within 8.4e-6 relative for the compressor,
5.0e-6 for the expander and 2.0e-6 for the limiter. The floor is the detectors' one-pole
recursions.

A NaN sample passes through, and is not read as a level. An infinite one holds the
compressor's gain at 0 until a reset, so a caller who cannot vouch for its input puts
[`sanitize`](edges.md) first. At a ratio of infinity the expander's curve is a step, and a
level within rounding of the step may fall on either side. `q31` declares its contract,
and its arithmetic arrives with a later milestone (C6).

The cards are on the pages of [`compressor.h`](api/compressor_8h.html),
[`expander.h`](api/expander_8h.html), [`limiter.h`](api/limiter_8h.html) and
[`dynamics_design.h`](api/dynamics__design_8h.html).
