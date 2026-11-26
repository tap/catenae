<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# What arithmetic costs

On a desktop a multiplication is a multiplication. `float` or `double`, it is one
instruction, and the choice between them is a choice of precision. On an embedded core
the choice is of another kind: one of the two may not be in the hardware at all. This
chapter shows what that does to the plainest loop in audio, a filter's sum of products.

The example is a low-pass FIR filter of 63 taps: each output is 63 multiplications and 63
additions.

## What you would naturally write

You would write it in `double`. It costs nothing you can see, and the rounding error ends
up far below anything a converter can play. If the samples arrive as `float`, you would
keep them so and still add the products up in a `double`, to be safe: a long sum in
`float` loses bits, and the wider accumulator is free. The kernel that catenae's was
carried from did exactly that.

Fixed point, samples held as integers, looks like an optimisation from another decade.
You would reach for it later, if a profile ever asked.

On a desktop all of this is right. The host's column in the table below says so: the
`double` filter costs about three times the `float` one, and nobody would notice.

## What it costs on the target

The Cortex-M33's floating-point unit is of single precision. A `float` multiplication is
an instruction. A `double` multiplication is a call to a library routine that does the
work with integers, and so is every `double` addition, and every conversion between the
two.

The harness runs the one filter in four formats: the 63 taps, stereo, 1000 blocks of 64
frames, which is 128,000 outputs. It counts the instructions of each.

| Core | `float` | `double` | Q15 | Q31 |
|---|--:|--:|--:|--:|
| Cortex-M4, no FPU | 705,440,641 | 1,011,192,415 | 45,501,874 | 85,963,195 |
| Cortex-M33 | 35,779,220 | 997,625,632 | 45,373,890 | 85,963,211 |
| Cortex-M55 | 33,861,216 | 33,991,851 | 37,824,889 | 69,450,210 |
| x86-64 host | 11,711,178 | 36,540,301 | 76,985,103 | 77,115,167 |

The numbers are from
[bench run 45866666441](https://github.com/tap/catenae/actions/runs/45866666441), and are
recorded in `bench/README.md` with the Cortex-M4F and Hexagon beside them. The workloads
are `fir_f32`, `fir_f64`, `fir_q15` and `fir_q31`. This reproduces the M33's row:

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

[Measuring](measuring.md) explains the plugin and the script.

Read the natural filter against the table.

- **`double` costs 28 times `float` on the M33.** One output is 280 instructions in
  `float`, 4.4 a tap, and 7,794 in `double`. A `float` filter with a `double`
  accumulator does not escape: its additions and its conversions are those routines.
- **The cost depends on the core, not on your code.** The Cortex-M55's unit has `double`,
  and the two filters cost the same there, within half a percent. The same source is a
  different program on each core.
- **It arrives without being written.** A constant `0.1` with no `f`, or a rounding that
  passes through a 64-bit integer, is enough. The harness's *link-stage gate* reads a
  `float` image for software-`double` routines. Its first run refused catenae's own: one
  truncation in `convert` had linked 19 of them.
- **Fixed point is not the fast choice here.** Q15 is 354 instructions an output on the
  M33 and Q31 is 672, both dearer than `float`. The Q15 kernel is scalar so far: one
  multiplication into a 64-bit sum per tap. The paired and vector kernels that make it
  cheap arrive at a later milestone (C6).
- **Fixed point is the only choice somewhere else.** The first row is a core with no
  floating-point unit at all. `float` is software there, and costs about 15 times Q15.

A format also has a range. Q31 holds values from -1 to just under 1, and nothing else. The
example at the end of this chapter sends noise at +6 dBFS, twice full scale, through a Q31
filter: the output is 0.5 away from the right answer. A `float` would have carried it
without comment.

## What catenae does instead

catenae makes the arithmetic part of a stage's type, and names one format as the truth.

Every stage is a template over its sample type, `basic_fir<S, ...>`. The plain name,
`fir`, is the `float` one. `fir_f64`, `fir_q15`, `fir_q31` and `fir_q4_27` are the
others. Nothing converts behind your back: a path changes format where it says
`convert`, and nowhere else.

### The golden model

```cpp
{{#include ../../examples/arithmetic_costs.cpp:golden}}
```

The `double` filter is the *golden model*. It is not the one you ship to an M33. It is
what every other format is tested against, and the distance is a number the battery pins.
A profile is then a measured departure from the truth, not a separate implementation that
happens to sound alike.

### The float profile

```cpp
{{#include ../../examples/arithmetic_costs.cpp:float_profile}}
```

The `float` filter multiplies in `float` and adds in `float`. It never leaves the
single-precision unit. Its design is a table in flash, so no designer and no `double` is
linked: `link_gate.FirImageHasNoSoftwareDouble` refuses this image, on every Arm core, if
one such routine appears.

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
ctest --test-dir build-size -L link_gate
```

The bits that the `double` accumulator saved are won back by the order of the sum. One
running sum adds each small product to a large total, and rounds each time. catenae keeps
four partial sums, every fourth tap in each, and combines them as (s0 + s1) + (s2 + s3).
At 773 taps one running sum in `float` measured -131.4 dBFS from the exact answer, and
the four sums -142.7; a `double` accumulator gives -156.3. The order is part of the
numeric design. It was fixed before any floor was recorded, and it is one a vector unit
can keep.

The battery pins the result: at 63 taps, on noise at -6 dBFS, the `float` filter measured
within 6.4e-8 of the golden model, -143.9 dBFS, and must stay below 8e-8
(`fir.FloatFollowsTheDoubleFilterWithinItsFloor`).

That is a floor, not a promise of bits. Where the unit has a fused multiply-add the
compiler uses it, and the last bits move: the workload's `float` checksum is one value on
the cores that fuse and another on those that do not. `double` and the fixed formats
print the same checksum on every core.

### Q formats

A fixed-point sample is an integer with a binary point agreed in advance. Q15 is 16 bits
with the point after the sign bit, so 32767 is just under 1. catenae writes the agreement
into the type: `q15` is a tag over a raw `int16_t`, not a class that wraps one.

```cpp
{{#include ../../examples/arithmetic_costs.cpp:q15}}
```

```cpp
{{#include ../../examples/fir_tables.h:q15_taps}}
```

The taps have their point one place further right, Q1.14, so that 16384 is 1.0 and a tap
near 1 fits. Each product is exact, the 64-bit sum is exact, and the result is rounded
once and saturated at the format's limits. So the arithmetic is within half an LSB (the
smallest step, 1/32768) of the exact sum of these taps, on every core, bit for bit. What
remains is the rounding of the taps themselves: about 5 LSB from the `double` design.

Q31 is the same with 32-bit samples and Q1.30 taps. Each 64-bit product drops 16 bits
before it is added, so that a long sum has room.

### Where the binary point is

```cpp
{{#include ../../examples/arithmetic_costs.cpp:headroom}}
```

The kernel adds and shifts integers. It does not know where the point is. Q4.27 is the
same 32-bit word with the point four places further right: it holds -16 to 16, with 24 dB
less resolution. It runs Q31's arithmetic with the same taps, and no new code. Headroom
is bought with resolution, and the type says how much.

The example runs all five against the golden model:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/examples/catenae_example_arithmetic_costs
```

```text
float profile: 3.8e-08 from the golden model (-148.4 dBFS)
q15: 4.2 LSB from the golden model
q31 at +6 dBFS: 0.5 from the golden model
q4.27 at +6 dBFS: 6.2e-08 from the golden model
CATENAE_EXAMPLE_DONE ok=1
```

It runs on every CI leg, the Cortex-M ones included.

### What was given up

The `float` filter is noisier than the one with a `double` accumulator, by 13.6 dB at
773 taps, and its bits are not the same on every core. Fixed point is not yet faster than `float` where
there is a floating-point unit. And a format is in every type along the path: changing it
is an edit, not a flag.

The cards are on the pages of [`fir.h`](api/fir_8h.html),
[`sample_traits.h`](api/sample__traits_8h.html) and
[`sample_format.h`](api/sample__format_8h.html).
