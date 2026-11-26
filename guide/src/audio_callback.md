<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# The audio callback

An audio device asks for its samples a block at a time. The driver calls a function of
yours, the *callback*, with a block of input and room for a block of output, and it needs
the output before the device runs out of the last one. That is a deadline, and it comes
round again every block, for as long as the device runs.

A late block is not a slow frame that the user forgives. It is a click, or a gap. So the
constraint is strict: nothing in the callback may take longer than its worst case allows,
and that rules out anything that can fail, wait or allocate. This chapter shows what that
does to a filter whose cutoff the user can turn.

The example is a stereo strip: a low-pass filter (a Butterworth of order 4) and a level.

## What you would naturally write

You would keep the cutoff as a parameter, and look at it at the top of the callback. When
it has changed, you call the designer, which turns a cutoff into the filter's
coefficients, and store what it returns. Then you filter the block. It is four lines
before the loop.

The designer is given a number from a control, so it can be given a bad one: a cutoff
above half the sample rate has no filter. A designer written for the desktop throws, or
returns an error code that the four lines pass on or drop.

On a desktop this is correct, and you would not see it in a profile. The design is calls to `tan` and `sin` and some matrix arithmetic, in `double`, and it
runs only when the control moves.

## What it costs on the target

The target is again the Cortex-M33, whose floating-point unit has `float` and no
`double`. The designer works in `double`, so on this core every one of its additions and
multiplications is a library routine. The harness counts the instructions of the worst single call of the
designer, `make()`, and measures its stack.

| On the Cortex-M33 | Instructions | Stack, bytes |
|---|--:|--:|
| The strip's callback: 64 stereo frames through the filter and a gain | about 4,300 | |
| One `make()`, Butterworth of order 4 | 16,757,532 | 2,008 |
| One `make()`, Butterworth of order 16 | 74,826,914 | 22,008 |
| One `make()`, a single equaliser section (the cookbook's) | 28,189,561 | 1,380 |

The numbers are from
[bench run 45837885047](https://github.com/tap/catenae/actions/runs/45837885047), and are
recorded in `bench/README.md`. The callback's figure is the `recursive` workload: 1000
blocks cost 4,325,126 instructions. The stack is the static worst path; the measured
high-water at order 4 is 2,000 bytes. These reproduce the counts and then the stack:

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

Read the four lines against the table.

- **One design costs about 3,900 callbacks.** The block in which the control moves does
  the work of 3,900 blocks. No deadline sized for the filter survives that.
- **The cost depends on the core, not on your code.** The Cortex-M55 has `double` in
  hardware, and the same order-4 `make()` is 481,214 instructions there. Code that met
  its deadline on one core misses it on the other, unchanged.
- **The stack is the callback's.** 2,008 bytes at order 4, and 22,008 at order 16, land
  on whatever stack the callback runs on, often an interrupt's, which is sized for the
  filtering and not for the design.
- **It can fail where nothing can be done.** If the designer refuses the cutoff, the
  callback still owes a block. It has no one to report to and no time to ask.

The first three are costs. The fourth is a matter of structure: the failure is real, and
it is in the wrong place.

## What catenae does instead

catenae splits the four lines by where each may run. Making a design can fail and is
slow, so it happens off the audio path. Applying one cannot fail and is a copy. The
callback only processes.

```cpp
{{#include ../../examples/audio_callback.cpp:strip}}
```

The callback is one call. It returns a count of frames, never an error: no function of
the audio path returns one. It is `noexcept`, and catenae is built without exceptions.

`process` does check what the caller hands it, in every build: that the input is whole
frames, and that the output has room. Those are the *boundary* checks, at the edge where
a caller's buffers enter. Here the blocks are `std::array`s, so the compiler makes both
checks and the callback runs none.

### A design is a value

The filter stage does not hold a cutoff. It holds a *design*: the coefficients, already
in the sample type the filter runs in, and the numbers the stage reports (its latency and
its tails). A design is a small constant, and there are only two ways to get one.

The strip starts from the first, a table:

```cpp
{{#include ../../examples/lowpass_table.h:table}}
```

These are the coefficients `make(0.1)` gives, printed once and kept in flash. No designer
runs at start-up, so none need be linked. `from_table` still checks them, as `constexpr`
arithmetic: every coefficient finite, every pole inside the unit circle. A table that
fails does not initialise a constant, so it does not compile.
`tests/compile_fail/biquad_table_unstable.cpp` is that case.

An image whose designs are all tables links no software `double` at all. The harness
proves it for this strip: `link_gate.RecursiveImageHasNoSoftwareDouble` refuses the image
if one software-`double` routine is linked.

```sh
ctest --test-dir build-size -L link_gate
```

### `make()` can fail, and says so

The second way is `make()`, when the cutoff is known only at run time.

```cpp
{{#include ../../examples/audio_callback.cpp:redesign}}
```

`make()` returns an `expected`: a design, or a `status` that says why there is none.
`catenae::expected` has `std::expected`'s interface, written for C++17. It and `status`
are `[[nodiscard]]`, so the compiler warns if the answer is dropped, and warnings are
errors in catenae's builds.

`retune` runs wherever 16.8 million instructions and 2 KB of stack are affordable: a
control thread, or the main loop. A failure there has somewhere to go. The caller can
show it, clamp the control, or ignore it. The callback is not involved, and keeps the
design it has.

`set_design` copies the design into the configuration. It takes a finished design, so it
has nothing to check and nothing to refuse. The new coefficients apply from the next
block, to the filter's history as it stands. That is a small transient, and the stage's
card says so; smoothing it is not this stage's work.

The example runs the strip, retunes it, and asks for a cutoff that does not exist:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/examples/catenae_example_audio_callback
```

```text
a tone at 0.05 through the cutoff at 0.1: 1.00
the same tone after retune(0.02): 0.02
retune(0.7): out_of_range
CATENAE_EXAMPLE_DONE ok=1
```

Frequencies are in cycles per sample. 0.7 is above one half, so there is no such filter,
and `make()` says `out_of_range`.

`set_design` is called *between* callbacks, never during one. In this example one thread
does everything, so that holds by construction. When the control is on another thread,
the design crosses in a mailbox, and the audio thread applies it at the top of its block.
[Threads and clocks](threads_and_clocks.md) shows the mailbox.

### One thing the host must do

A recursive filter fed silence decays toward zero, and on the way its state passes through
*subnormals*, the very small values just above zero. Some processors compute with them
much more slowly than with ordinary values. That is a callback whose cost depends on
its data, which a deadline cannot allow.

The cure is a mode of the processor, set per thread: flush subnormal results to zero. It
belongs to the host, because the host owns the thread. catenae never sets it.

```cpp
{{#include ../../examples/flush_to_zero.h:host}}
```

A host calls this once on each audio thread, before the first callback. The build states
what the host does, with `CATENAE_FLUSH_TO_ZERO`, on by default. At the *audit* check
level, a build option for testing, every recursive stage compares the thread's setting
with that statement on each run, and stops in the check handler (`check::flush_to_zero`)
if they differ. The shipping level reads nothing.

### What was given up

A cutoff cannot be set in one call from anywhere. It is two steps in two places, and the
program must have a place off the audio path to run the first. A table design is a
constant someone generated and must regenerate when the filter changes. And the filter
does not ramp between designs.

The cards are on the pages of [`biquad.h`](api/biquad_8h.html) and
[`biquad_design.h`](api/biquad__design_8h.html), with `make()`'s cost at orders 2, 4, 8 and 16,
and `expected` is on [`expected.h`'s](api/expected_8h.html).
