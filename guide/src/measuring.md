<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Measuring

Every earlier chapter quoted numbers: instructions, bytes, frames. This chapter is about
where such a number comes from, and what keeps it true after the next change. Its
constraint is that a cost that is not measured is not known.

## What you would naturally write

You would time it. Read a clock, run the callback some thousands of times, read the clock
again, and divide. Run that a few times and take the best. To choose between two designs,
time both and keep the faster. All of this happens on the desktop, because the code is
already built there and the target is a board on another desk.

This is a reasonable habit, and it answers a real question: which design is faster on
this machine, today.

It gives nothing that can be written down. The machine is shared with a browser, an
editor and the operating system, and the clock counts their time as well as yours. A CI
runner is shared too. The time differs from one run to the next,
so a time recorded today cannot be checked tomorrow. This chapter quotes no wall-clock
time for that reason: the guide writes down only numbers that a command reproduces.

## What it costs on the target

The noise can be removed. Count executed instructions instead of reading a clock, and the
desktop gives the same number on every run. The remaining cost is larger: the desktop's
answer is not the target's. Three of catenae's own measurements show it.

**The ranking of two designs.** [Stages and chains](stages_and_chains.md) counted its
strip twice, as a chain and as a graph of virtual nodes joined at run time. On the x86-64
host the chain is 13.3 % cheaper. On the Cortex-M33 the two are within 0.5 %. On the
Cortex-M4 without a floating-point unit the chain is 2.5 % dearer. A team that timed the
two on a desktop would have a clear winner, and on the M4 it would be the wrong one. The
table is in that chapter, from
[bench run 45653902512](https://github.com/tap/catenae/actions/runs/45653902512).

**The layout of the channels.** A stage of several channels can run once over interleaved
frames, or once per channel over mono buffers. The layout benchmark counts both and
divides the mono count by the interleaved one, so a figure above 1 favours interleaved.

| Target | 12-channel delay | stereo gain |
|---|--:|--:|
| host (x86-64) | 5.792 | 1.156 |
| `m33` | 1.653 | 0.947 |

For the 12-channel delay the host and the M33 agree on the direction and disagree on the
size: 5.8 times on one, 1.65 on the other. For the stereo gain they disagree on the
direction. The mono form is dearer on the host and cheaper on the M33. These are from
[bench run 45704804502](https://github.com/tap/catenae/actions/runs/45704804502), at
commit `fed09b6`. The commands that reproduce all three findings are at the end of the
chapter.

**A cost with no desktop form at all.** `convert` rounds a scaled `float` to an integer.
Its first version truncated the value to `int64_t`. On the Cortex-M that conversion is
not an instruction. It is a library routine of the compiler's, `__aeabi_f2lz`, and the
routine is written in software double. So an image whose every sample is a `float` linked
19 software-double routines on each of the M4, the M4F and the M33. The source had no
`double` in it: the line was a cast of a `float` to `int64_t`. The double arithmetic was
inside the compiler's own routine, so no warning on the source could see it, the one for
a `float` promoted to `double` included. Only the linked image shows it, and nothing
measured on a desktop does.

The scaled value is below 2^31 in magnitude. `convert` now truncates to `int32_t`, which
is one FPU instruction each way, with the same results.

## What catenae does instead

It measures on the target, by methods that give the same answer every time, and it makes
each answer a gate. There are four instruments.

### The ratchet

A *workload* is one small program. Here is the counted part of the strip's.

```cpp
{{#include ../../bench/icount/strip_main.cpp:counted}}
```

The count is of executed instructions, which are exact where a time is not. Four
Cortex-M cores (the M4 without an FPU, the M4F, the M33 and the M55) run under
`qemu-system-arm` with a counting plugin. Hexagon runs under `qemu-hexagon`, which counts
packets. The host runs under callgrind. On the host the count is of
`catenae_host_counted()` alone. Under QEMU it is the whole run, start-up and `printf`
included, so two workloads are compared by their difference.

Each count is gated against `bench/baselines.json`, two-sided, at 3 %. A regression
fails. An improvement beyond 3 % fails too, and must be recorded as the new baseline, so
the gate stays tight around the current cost. That is the ratchet. The same binaries
built as C++20 and C++23 must count exactly what C++17 counts, checksums included. A
workload with no baseline fails, and so does a baseline with no workload.

### The size probes

A count says nothing about memory. A *probe* is a workload built as the target's test
legs build it, for size, and linked with a map. A second image of the same workload
paints the stack below each call and reads how deep the call went.

From these, `scripts/ceilings.py` reads the four sections apart: `.text`, `.rodata`,
`.data` and `.bss`. It reads them for the whole image, for catenae's compiled core, and
for the probe's own unit. It counts the start-up functions that catenae's objects add,
and bounds the stack of every start-up function in the image. For each call an
application makes it reads the stack, both the static worst case down the call's path and
the depth actually painted, and the bytes of code the call can reach.

Every figure has a *ceiling*: the recorded figure plus 3 %, rounded up. A figure of zero
is its own ceiling. The gate is one-sided, so a change that moves a figure past its
ceiling fails.

The example strip is one of the probes. On the M33 its unit has 804 bytes of `.bss` and 0
of `.data`, catenae adds no start-up function, and `process` uses 32 bytes of stack,
measured and static alike. [Where memory lives](where_memory_lives.md) explains what
those four numbers mean. This chapter's point is that each is a ceiling in
`bench/ceilings.json`, and a change that adds a start-up function or a byte of `.data`
fails.

### The link-stage gate

`scripts/link_gate.py` reads a linked image and refuses it when it contains a
software-double routine or a double `libm` symbol. It runs on named probe images, one for
each family of `float` stages. The first holds every stage of the library's first
milestone in one `float` path, and its first run is the `convert` finding above.

The gate has a limit, and a test pins it. On the M55 the compiler's default FPU has
double precision, so a double product there is an instruction and not a symbol. The gate
cannot see it. That is why the `convert` image passed on the M55 and failed on the other
three.

### The layout benchmark

The layout benchmark is informational by decision. It has no baselines and no tolerance,
and it never fails a build. It exists so that the layout a stage prefers has a number on
every core. Both layouts of a pair print one checksum when their outputs are
bit-identical, and in the run above every pair was.

It showed that from 6 channels up, interleaved is the cheaper layout for every stage with
state, on every target, by 1.06 to 5.8 times. Stereo is where it is close.

### The rule under all four

A number is never typed in. Each CI run uploads what it measured. A change that moves a
count or a size copies the new figure from that run, in the same change, with the reason.
`bench/README.md` names the run behind every recorded number.

The M33's ratchet, with the counting plugin built as the workflow builds it:

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

The layout counts, from the same build:

```sh
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so --prefix catenae_layout_ --report-only
```

The sizes, the stack and the link-stage gate, from a build for size:

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
scripts/ceilings.py measure --key m33 --build-dir build-size
ctest --test-dir build-size -L link_gate
```

The host's figures come from the same script under callgrind:

```sh
scripts/icount.py --key host --build-dir build-host
```
