<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Long filters

A filter with hundreds of taps costs hundreds of multiplications for every sample if it
is run one frame at a time. Run a block at a time, through a transform, it costs a
fraction of that. The price is that its output arrives in blocks. This page is about that
trade: what the long filter costs each way, and what the blocks do to a callback.

The example is a stereo low-pass of 255 taps in `float`: its cutoff at 0.1 cycles per
sample, 80 dB down in its stopband. The method is overlap-save, from Oppenheim and
Schafer, "Discrete-Time Signal Processing" (3rd ed., section 8.7.3), and Stockham (1966).
Nothing is taken from a product.

## A filter of 255 taps

### What you would naturally write

You would run the filter as its definition reads: each output sample is the sum of the
last 255 inputs, each times its tap. catenae's `fir` is that stage, and the taps are a
table in flash.

```cpp
{{#include ../../examples/fft_fir_tables.h:direct}}
```

```cpp
{{#include ../../examples/long_filter.h:direct}}
```

This is correct, frame-synchronous, and late by 127 frames, which is (255 - 1) / 2: the
delay of any linear-phase filter of that length.

### What it costs on the target

Every tap is touched for every sample of both channels. The harness runs 1000 callbacks
of 64 stereo frames of noise, 128,000 output samples, on the Cortex-M33 under QEMU. The
counts are instructions executed.

| Workload | What it runs | Instructions |
|---|---|--:|
| `fir_f32_255` | the direct form, `long_direct` | 134,348,070 |
| `fft_fir_f32` | the block form of the next section, in `steady<>` | 24,911,709 |

The direct form is about 1,050 instructions an output sample, and about 134,300 every
callback: the totals divided by 128,000 and by 1000. The cost is the same on every call,
and it grows in step with the length of the filter.

The numbers are from
[bench run 46021806011](https://github.com/tap/catenae/actions/runs/46021806011). The
workloads are in `bench/icount/fft_fir_main.cpp`, and `bench/README.md` says how the
plugin is built. These reproduce the counts:

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

### What catenae does instead

`fft_fir` is the same filter by fast convolution. It collects a block of 256 frames,
transforms it together with the block before, multiplies by the filter's spectrum,
transforms back, and keeps the last 256 samples. Those are the direct form's output.

```cpp
{{#include ../../examples/long_filter.h:blocks}}
```

`long_blocks` is the stage bare. It works in blocks: its input group is 256, and it
writes nothing until a block's last frame arrives. In calls of 64 frames, three calls
write nothing and the fourth writes 256. A callback must hand back 64 frames every time,
so the chain wraps the stage in `steady<>`, which evens the output out, 255 frames
later. [Bursts and buffering](bursts_and_buffering.md) has the terms and the reason.

It goes in a callback like any other chain.

```cpp
{{#include ../../examples/long_filter.cpp:callback}}
```

The example runs all three forms over the same noise and prints this, on the host and
under QEMU alike:

```text
blocks of 256 in calls of 64: 0, 0, 0 and 256 frames
in steady<>: 64 a call; latency 127 frames bare, 382 in steady<>
against the direct form: largest difference 1.2e-07; steady<> the same, 255 frames later: yes
memory: a voice 8204 bytes and the workspace 4620; the direct form's voice 4092
```

**The mean.** In the table above, `fft_fir_f32` is about 195 instructions an output
sample. By the ratio of the two totals it is 5.4 times cheaper than the direct form.

**The worst call.** The work does not spread. All of a block's transforms land on the
call that completes it, one callback in four here, and the other three only copy. The
mean callback is 23,349 instructions; the worst, the workload `callmax_fft_fir_f32` of
the same run, is 89,815. A callback's deadline is met by its worst call, not its mean,
so that is the number to budget. It is still less than the 134,300 the direct form
spends on every call.

**The latency.** The filter itself is 127 frames late either way. In `steady<>` the
output is 255 frames later still: 382. Both are `exact_latency`, compile-time constants
of the types, and the third line of the output is the check that the 255 frames are a
pure delay, bit for bit. A host whose callback is a whole block of 256 runs
`long_blocks` bare, gets a block out for each block in, and pays 127 frames and no more.

**The same filter.** The two forms differ by rounding only: 1.2e-07 at most over the
example's 16 blocks. Against a direct convolution in `long double` over noise in
[-1, 1), the battery holds the `float` stage within 1.8e-7 of full scale and the
`double` one within 3.2e-16 (`tests/test_fft_fir.cpp`).

**The same bits.** The transforms and the product by the spectrum are in the compiled
core, built with fused multiply-add off, and the header only copies. So the output's
bits are the same on every target CI runs, and the battery pins a checksum of them.

## The spectrum is a design

### What you would naturally write

The block form multiplies by the taps' spectrum, so something must transform the taps.
You would do it at start-up, or whenever the cutoff changes, wherever the code happened
to be.

### What it costs on the target

`make()` does that work. It widens the taps to `double`, runs one `double` transform of
512, and rounds each bin once to `float`. The M33's floating-point unit is single
precision, so the `double` arithmetic is software. Its worst call there is 587,013
instructions and 872 bytes of stack (`callmax_design_fft_fir_f32_255`, the same run).
That is more than six of the block form's worst callbacks, by the ratio of the two
counts: no callback has room for it.

### What catenae does instead

A design is made off the audio path, in the caller's frame and into the caller's table,
and `set_design()` applies it between calls. It takes effect from the next block.

When the filter is known at build time, nothing is made on the target at all. The
spectrum is a table in flash, and `from_table` checks it as it compiles: every part
finite, and the DC and Nyquist bins real, as a real filter's are. A table that fails
does not initialise a constant.

```cpp
{{#include ../../examples/fft_fir_tables.h:spectrum}}
```

`tools/tablegen` writes the table from the same `make()`, and a CTest check fails when
the committed file is not the generator's output. An image built this way runs no
designer.

## What was given up

- **Latency, or a callback of the block's size.** In `steady<>` the filter is 255 frames
  later, 382 in all. Without it, the host's callback must be 256 frames.
- **An even load.** The worst callback is about four times the mean, by the ratio of the
  two counts above. The direct form costs the same on every call.
- **Memory.** On the M33 a voice is 8,204 bytes against the direct form's 4,092, about
  twice, and the thread needs a workspace of 4,620 bytes. `process` reaches 560 bytes of
  stack by the static bound, 544 painted (the `fft_fir` probe, `scripts/ceilings.py
  measure`).
- **Flash.** The spectrum is 257 bins, 2 KB in `float`, where the direct form needs only
  its taps.
- **Fixed point, for now.** The stage runs in `float` and `double`. Its fixed-point
  arithmetic is new design, and waits for milestone C6.
