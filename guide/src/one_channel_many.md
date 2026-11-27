<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# One channel, many

A stage written for one channel can run over every channel of a port. It costs two
copies of every sample, out of the interleaved frames and back. For a light stage the
copies are most of the work; for a heavy one they do not show. This page is about when
to pay them.

The example is a small strip, a gain that ramps and a delay of 32 frames, in `float`,
over a six-channel port.

## A mono stage over six channels

### What you would naturally write

You have a stage for one channel. Perhaps it came from another codebase, or it was
simpler to write that way: one sample in, one out, no channel index anywhere.

```cpp
{{#include ../../examples/planar.cpp:mono}}
```

The port has six channels, and its frames are interleaved: the six samples of one
instant sit side by side. So you would write a loop around the stage. It copies each
channel's samples out of the block into a buffer of its own, runs the stage over each
buffer with that channel's state, and copies the results back into frames. Each place
that needs the stage gets the loop again, with its buffers and its six states.

### What it costs on the target

The stage's own work is the same. What is added is the copying, and it does not shrink
with the stage.

The [layout benchmark](measuring.md#the-layout-benchmark) counts it. It runs a stage as
one configuration of C channels over interleaved frames, and as a mono configuration
with the copies out and back, 16,384 frames per channel in calls of 128. The table is
the second count over the first, on the Cortex-M33 under QEMU. Above 1, the mono stage
with its copies is the dearer.

| Stage | 2 channels | 6 | 12 |
|---|--:|--:|--:|
| The gain | 3.59 | 3.62 | 3.69 |
| The strip: a ramping gain, a delay of 32, a halving map | 1.50 | 1.87 | 1.96 |
| The Butterworth biquad | 1.44 | 1.40 | 1.40 |
| The FIR of 63 taps | 1.03 | 1.02 | 1.02 |
| The FFT FIR | 1.08 | 1.11 | 1.12 |

- **A light stage is mostly copies.** A gain is one multiplication a sample, and the
  two copies around it cost more than it does: 3.6 to 3.7 times the interleaved gain.
- **The copies are the cost, not the layout.** The same benchmark runs the mono
  configurations over buffers that are already split by channel, with no copies. There
  the gain is 1.100, 1.185 and 1.208 times interleaved at 2, 6 and 12 channels, and the
  strip 0.997, 1.281 and 1.344. At six channels the gain with its copies is about three
  times the gain without them, by the ratio of 3.62 to 1.185.
- **A heavy stage hides them.** The FIR does 63 multiplications a sample, and two
  copies beside them are within 3 %. The FFT FIR is within 12 %.
- **The output is the same.** Every pair in the run printed one checksum: the mono
  stage's output over each channel was the interleaved stage's, bit for bit.

The other targets agree in kind. Across the six the benchmark runs on, the copies cost 1.3
to 8.5 times interleaved for a gain, a delay or a map, and a few per cent for the FIR
(1.00 to 1.09) and the FFT FIR (1.00 to 1.13). A stage written for C channels is the
cheaper on every one, or within 0.4 %.

The first table is from
[bench run 46037804664](https://github.com/tap/catenae/actions/runs/46037804664), and
the counts without copies from
[bench run 45715344934](https://github.com/tap/catenae/actions/runs/45715344934).
`bench/README.md` has every core's table and the configure line;
[Long filters](long_filters.md) shows its shape. This reproduces the M33's counts:

```sh
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so --prefix catenae_layout_ --report-only
```

### What catenae does instead

Two things, and the choice between them is the caller's.

**`planar_section` is the loop, written once.** It takes the mono chain and a channel
count, and is one stage of that many channels.

```cpp
{{#include ../../examples/planar.cpp:section}}
```

It does what the loop did. In pieces of 64 frames it copies each channel's samples out,
runs the mono chain over them on that channel's state, and copies the output back
interleaved. What it adds to the loop is in its type:

- **One configuration, a state per channel.** The mono chain is stored once, and every
  channel runs it. `mono()` reaches it, to change a parameter between calls. Each
  channel's state is reset by it.
- **The mono chain's behaviour is the section's.** Its ratio, its input group, its
  latency and its tail are the mono chain's own. So a rate changer works in a section,
  and so does a stage that works in blocks, such as the FFT FIR of
  [Long filters](long_filters.md).
- **The channel count is in the type.** The section joins a chain as any six-channel
  stage does, and a joint that does not hold does not compile
  ([Stages and chains](stages_and_chains.md)). A stage of more than one channel does
  not compile as a mono stage either.

**A stage written for C channels is the cheaper way, where one exists.** Interleaved is
catenae's native layout, and its own stages take a channel count. The same strip, for
six channels:

```cpp
{{#include ../../examples/planar.cpp:interleaved}}
```

The example runs both over the same noise and compares every sample. It prints this, on
the host and under QEMU alike:

```text
six channels from the mono strip, against the six-channel strip: the same, bit for bit
latency 32 frames either way; a voice 944 bytes from the mono strip, 796 interleaved
```

The outputs are the same and so is the latency. The memory is not: six states of the
mono strip are 944 bytes, and the one six-channel state is 796. That is about 1.19
times, by the ratio of the two.

## What was given up

With `planar_section`:

- **The copies.** They are the numbers of the table: most of a gain's cost, a few per
  cent of an FIR's.
- **A larger voice.** 944 bytes against 796 here.
- **A workspace.** A thread that runs the section holds a piece of 64 frames of every
  channel's input, and of its output, beside whatever the mono chain needs.

So use the interleaved stage where one exists. Use `planar_section` for a stage that
is written for one channel, or one heavy enough that the copies do not show.
