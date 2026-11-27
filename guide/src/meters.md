<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Meters

A meter reads a signal and changes nothing. Its reading is wanted somewhere else: on a
display, in a log, by a control loop on another thread. This page is about both halves:
where a meter sits in a chain, and how its reading leaves the audio thread.

The example is a stereo bus at 48 kHz with four meters: peak, RMS, the EBU's peak
programme meter (PPM), and the correlation of left with right. The PPM's detector is from
Zolzer, "Digital Audio Signal Processing" (2nd ed., 2008, ch. 7), and its ballistics are
Type IIb of EBU Tech 3205-E: an integration time of 10 ms, and a return of 24 dB in
2.8 s. Nothing is taken from a product.

## What you would naturally write

You would write a meter object with two methods. The audio callback calls `process`,
which keeps a running peak and a running sum of squares. The display thread calls
`level_db`, which reads those members and returns `20 * log10(peak)`. When the readings
flicker, you would put a mutex around the members.

The other natural shape is a metering callback. The audio thread converts to decibels at
the end of each block, and calls a function the display registered.

Both read well, and both are how a desktop meter is commonly written.

## What it costs on the target

- **A lock the callback can wait on.** The display thread holds the mutex while it reads,
  and the scheduler can stop it there. The callback then waits past its deadline.
  [Threads and clocks](threads_and_clocks.md) has the case against it, and against the
  flag you would try next.
- **A reading torn between two blocks.** Without the lock, the display reads while the
  callback writes. The peak is from one block and the sum from the next. In a fixed-point
  format the sum is 64 bits, which is two words on a 32-bit core, and the reader can see
  one new and one old. It is a data race, as the flag of that chapter is.
- **A `log10` on the audio thread.** The metering callback moves the conversion into the
  deadline. `log10` is `double` unless you say otherwise, and on a core whose
  floating-point unit is single precision it is a library routine that works with
  integers. No number is claimed for it here: none was measured.
  [What arithmetic costs](what_arithmetic_costs.md) has what `double` costs on these
  cores. The display redraws perhaps 30 times a second, and most of those conversions
  are never seen.

What remains is the cost of the meters themselves, with no lock and no logarithm. The
harness runs them over 1000 callbacks of 64 stereo frames of noise, 128,000 samples, a
result every 480 frames over a window of 4,800. The workloads are in
`bench/icount/meters_main.cpp`.

<!-- NUMBERS: meters_f32, meters_q15, meters_q31 from Bench -->

These reproduce the counts on the Cortex-M33; `bench/README.md` says how the plugin is
built, and [Measuring](measuring.md) explains the script.

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

## What catenae does instead

### A meter is an analyzer in a tap

An *analyzer* reads a signal and writes nothing. `tap<Ch, Analyzer>` is the stage that
holds one. It is 1:1 and in place, with a latency of exactly 0, and its output is its
input bit for bit. So a meter goes anywhere in a chain, and the chain's audio is the same
with it or without it.

```cpp
{{#include ../../examples/meters.h:meters}}
```

Each meter's numbers are in its type. `peak_meter<2, 480, 4800>` has two channels, a
*hop* of 480 frames and a *window* of 4,800: a result every 10 ms, over the last 100 ms.
The window is a whole number of hops, or the type does not compile. The correlation
reads channels 0 and 1 at lags of up to 16 frames either way. The PPM has a hop and no
window, because its memory is its ballistics.

A meter's state is part of the chain's state: plain storage, all zero at rest, in `.bss`
when static. The assertion under the chain is the tap's promise for all four:
frame-synchronous, and no latency.

### Results are raw, and whole

A result is what the audio thread can make cheaply. The peak meter's is each channel's
largest magnitude. The RMS meter's is each channel's *energy*, the sum of its squares
over the window, with no division and no root. The correlation's is its lagged sums and
the two channels' energies. The PPM's is the detector's level. Each frame costs a
comparison, or a few multiplications and additions, and none costs a logarithm.

A result is complete at the end of its hop, and stays the same until the next hop ends.
The chain's thread reads it between calls, through `result()`. `results_ready()` is a
count of the hops completed since reset. The reader of the state keeps the last count it
saw, so reading changes nothing.

### A reading leaves through a mailbox

The four results go into one struct, by value.

```cpp
{{#include ../../examples/meters.cpp:reading}}
```

After each callback, the audio thread looks at the count. When a hop has completed, it
posts the struct.

```cpp
{{#include ../../examples/meters.cpp:post}}
```

`stage<I>()` reaches a tap, and `stage_state<I>()` its part of the chain's state. All
four meters have the same hop, so one count serves for all.

The mailbox has one poster and one taker, and neither waits. The four results are posted
together, so they arrive together, from the same hop. A reading never taken is replaced
by the next: a display that redraws slowly sees the newest, and the audio thread does
not know or care.

### Decibels are the reader's

```cpp
{{#include ../../examples/meters.cpp:read}}
```

`dbfs()` and `normalized()` are static functions of the meter's type. They take a result
and need no state, so they run wherever the reading is wanted, as often as it is shown.
`rms_t::dbfs` divides the energy by the window and takes the logarithm.
`correlation_t::normalized` divides a lagged sum by the root of the product of the two
energies, which at lag 0 is Pearson's coefficient.

The example plays a 1 kHz sine at half of full scale on both channels, then with the
right channel inverted. It prints this, on the host and under QEMU alike:

```text
left: peak -6.02 dBFS, RMS -9.03 dBFS, PPM -6.23 dBFS; correlation +1.00
left: peak -6.02 dBFS, RMS -9.03 dBFS, PPM -6.23 dBFS; correlation -1.00
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target catenae_example_meters
build/examples/catenae_example_meters
```

Half of full scale is -6.02 dBFS, and a sine's RMS level is 3.01 dB below its peak.

### The ballistics are a table

The PPM's two coefficients come from a design. `make()` finds the attack by simulating
the standard's 5 kHz burst, about ten simulations of 25 bursts each, in `double`. That
is work for a host, or for a thread with no deadline. The chain holds the result as two
numbers in flash.

```cpp
{{#include ../../examples/meters.h:ppm}}
```

`from_table` checks them as it compiles: an attack in (0, 1] and a release in (0, 1). A
table that fails does not initialise a constant. The battery checks the two numbers
against `make()` on every CI leg (`ExampleTableIsTheEbuDesign` in `tests/test_ppm.cpp`).

## What was given up

- **A reading on demand.** A result arrives every hop, and it is up to one hop old, 10 ms
  here. Between hops there is nothing newer to ask for.
- **A history.** The mailbox carries the newest reading. A reader that sleeps through
  ten hops sees one result, so a peak that must be held on a display is held by the
  display.
- **A window of any length.** It is a whole number of hops, and the state keeps each
  hop's sums: ten hops for these meters.
- **The EBU's table, for short bursts.** Zolzer's detector meets it at 100, 10 and 5 ms.
  At 48 kHz a 1.5 ms burst reads -11.1 dB where the table asks for -9 ± 1, and a 0.5 ms
  burst -19.3 where it asks for -17 ± 2. A test pins all five rows.
- **The peak of a steady tone.** The PPM falls between a sine's peaks, so it reads below
  them: 0.21 dB for 1 kHz at 48 kHz. That is the -6.23 beside the -6.02 above.
- **The PPM in fixed point, for now.** Peak, RMS and correlation run in `float`,
  `double`, Q15, Q31 and Q4.27. The PPM runs in `float` and `double`, and its fixed-point
  recursion waits for milestone C6.

The cards are on the pages of [`tap.h`](api/tap_8h.html),
[`peak_meter.h`](api/peak__meter_8h.html), [`rms_meter.h`](api/rms__meter_8h.html),
[`ppm_meter.h`](api/ppm__meter_8h.html), [`ppm_design.h`](api/ppm__design_8h.html) and
[`cross_correlation.h`](api/cross__correlation_8h.html). The analyzer contract is
[its own page](api/analyzer_contract.html).
