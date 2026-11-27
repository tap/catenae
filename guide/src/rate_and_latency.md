<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Rate and latency

Most stages hand back a frame for every frame they are given, at the rate they were given
it. Some cannot do their work at that rate, and run part of the path faster. Every filter
that makes this possible holds the signal back a little. This chapter is about two
numbers that follow: how many frames come out for the frames that went in, and how late
they are. Its constraint is a second path that must arrive at the same moment as the
first.

The example is a stereo overdrive, `tanh(4x)` on every sample, with the untouched signal
mixed back in beside it.

## What you would naturally write

You would start with the `tanh` alone, one call a sample. It sounds wrong. A curve like
this one adds harmonics above the input, and a harmonic above half the sample rate cannot
be stored at that rate: it folds back down as an *alias*, a tone at a frequency that is
no multiple of the input's. A tone through the plain clip leaves aliases 19.0 dB below
itself.

The known cure is to *oversample*: raise the rate to four times, clip there, where the
harmonics have room, and filter back down. Each doubling and each halving is a filter, a
*half-band*. You would chain two up, the clip, and two down.

```cpp
{{#include ../../examples/overdrive.cpp:bare}}
```

`up2<2>` is a stereo half-band that doubles the rate, and `down2<2>` halves it. The
second argument relaxes the inner pair: at the doubled rate the audio is a smaller part
of the band, so a shorter filter does. This is a sound design, and it removes the
aliases.

Then the dry path. The four filters delay the overdriven signal, so the dry signal has to
wait for it. You would work out the delay, or feed the chain a click and count, put a
delay line of that many frames on the dry path, and add the two.

Look at the last line of the listing. The filters' delays are 19/2, 9/4, 9/4 and 19/2
frames at the base rate, and their sum is 47/2: 23.5 frames. A delay line holds whole
frames. Take the clip out and feed the chain a click: its two largest output samples are
frames 23 and 24, equal. You would pick one.

## What it costs on the target

Oversampling does what it promised. The example measures the aliases of one tone, 69
cycles in 512 frames at 0.9 of full scale: the power below 3/8 of the rate that is no
harmonic of the tone, against the tone's own. At the base rate that is -19.0 dB. At four
times the rate it is -92.9 dB.

The price is arithmetic and memory. The harness runs the overdrive of the next section,
which is this chain and one short delay, over 1000 callbacks of 64 stereo frames: 128,000
output samples. The counts are instructions executed, or packets on Hexagon.

| Target | `overdrive` |
|---|--:|
| `m4-softfp` | 2,143,113,838 |
| `m4f` | 206,448,666 |
| `m33` | 206,284,021 |
| `m55` | 248,344,025 |
| `hexagon` | 150,069,607 packets |
| `host` | 199,631,655 |

The numbers are from
[bench run 45922339828](https://github.com/tap/catenae/actions/runs/45922339828), and are
recorded in `bench/README.md`. These reproduce the M33's count and then its sizes:

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

- **It is 1,612 instructions a sample on the M33.** Each output sample is four half-bands
  and four `tanhf` calls at four times the rate. A half-band alone, stereo, at the base
  rate, is 106 instructions for each sample it writes going up (`up2_f32`, 27,050,511 for
  the run) and 242 coming down (`down2_f32`, 15,498,006).
- **A voice is 3,888 bytes.** The overdrive remembers the recent input of each of its four
  filters, and of its one short delay, for each channel. That history is its state, and it is RAM.
- **A call needs 4,616 bytes of scratch.** The blocks at twice and four times the rate
  have to be somewhere between the filters. That is the workspace. `process` itself
  reaches 352 bytes of stack and `reset` 56.
- **It is about ten times dearer without a floating-point unit.** The first row is
  `float` in software, as [What arithmetic costs](what_arithmetic_costs.md) showed.

None of that is a surprise: it is the price of the second number against the first. The
half frame is the cost nobody chose.

The wet path is 23.5 frames late. A dry path delayed by 23 frames is half a frame early,
and by 24 it is half a frame late. No whole delay lines the two up, and they are added
together half a sample apart. Nothing reports it. The program runs, both paths are heard,
and the number that was wrong was a comment beside a delay line, or a click counted by
eye.

## What catenae does instead

catenae makes the ratio and the latency numbers of a stage's type, and refuses a pair of
paths that cannot line up.

### A ratio is part of the type

A stage that changes rate is `resample<Ch, Ratio, Design>`. The ratio is a type,
`ratio_t<L, M>`: L frames out for every M in, where L and M are each a product of twos
and threes. Any other ratio does not compile. `up2` and `down2` are the two names for its
half-bands.

The filter is a type too. A host tool, `tools/tablegen`, designs it and writes it down as
a constant, so no image designs a filter as it runs. The overdrive's image links no
software `double`: the link-stage gate, `link_gate.OverdriveImageHasNoSoftwareDouble`,
passes on every Arm core.

A chain multiplies its stages' ratios. Two up and two down is 1:1, and because the up
comes before the down, n frames come back for n on every call. Both are settled when the
chain's type is formed.

### A latency is a number of the type

Every stage that delays all frequencies alike publishes `exact_latency`, an exact
fraction of frames and not a measurement. A chain adds them up. That is where the 47/2 of
the first listing came from: nobody measured it.

`oversample<4, Stage>` is that chain with one thing added.

```cpp
{{#include ../../examples/overdrive.h:overdrive}}
```

It works out from its parts that they come to 47/2, and adds the smallest delay that
makes a whole number: one frame at the doubled rate, which is half a frame at the base
rate. Its latency is 24 frames exactly, and the third assertion says so.
`oversample<2, Stage>` around the same clip is 19 frames, and needs no delay. If no delay
at an inner rate can make the sum whole, the type does not compile.

The battery checks the number against the signal. Through `oversample<4, ...>` around a
stage that changes nothing, a click peaks at frame 24, and the response is the same on
both sides of it (`AnImpulseIsSymmetricAboutTheLatency`).

### The dry path

```cpp
{{#include ../../examples/overdrive.cpp:dry_wet}}
```

`parallel<A, B>` runs two *arms* side by side on one block: here four channels in, the
first two through the overdrive and the last two through a gain of 1. It reads each arm's
`exact_latency`, and delays the arm that is ahead by the difference. The dry arm gets a
delay line of 24 frames. Nobody typed the 24. Change the oversampling to two times and
the delay becomes 19 in the same build. `mix<4, 2>` then adds the arms, half of each. The
battery pins it: the dry arm's click lands on frame 24, on the wet arm's peak
(`ADryArmLinesUpByType`).

Put the bare chain of the first listing in the overdrive's place and GCC 13 prints:

```text
error: static assertion failed: catenae: parallel<> aligns by whole frames: each arm's exact latency is a whole number of frames
```

The half frame is now a build that fails. The case
`tests/compile_fail/parallel_latency_not_whole.cpp` pins the message.

### Two voices, one workspace

```cpp
{{#include ../../examples/overdrive.cpp:voices}}
```

The overdrive is a constant, and one constant serves every voice. A voice is a state:
3,888 bytes. The workspace holds nothing between calls, so a thread needs one, however
many voices it runs in turn. Two voices and the workspace are 12,392 bytes of `.bss`, of
the probe image's 13,408. [Where memory lives](where_memory_lives.md) explains the three.

The example runs the clip both ways and prints the latencies and sizes:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/examples/catenae_example_overdrive
```

```text
aliases below 3/8 of the rate, the soft clip at the base rate: -19.0 dB
the same at four times the rate: -92.9 dB
latency: 24 frames; the bare chain 23.5; with the dry path 24.0
a voice: 3888 bytes; the shared workspace: 4616 bytes
CATENAE_EXAMPLE_DONE ok=1
```

It fails unless oversampling buys 50 dB, and it runs on every CI leg, the Cortex-M ones
included.

### What was given up

The half-bands are pinned at one design: 70 dB of rejection, the passband to 3/8 of the
chain's lowest rate, 39 taps for the outer pair and 19 for the inner. Other profiles
arrive at a later milestone (C7). `oversample` is two times or four, and nothing else.
`parallel` aligns by whole frames only, and has two arms.

A rate changer on its own does not return n frames for n, and a call may not bring it a
whole number of its steps. [Bursts and buffering](bursts_and_buffering.md) is about that.

The cards are on the pages of [`resample.h`](api/resample_8h.html),
[`oversample.h`](api/oversample_8h.html) and [`parallel.h`](api/parallel_8h.html).
