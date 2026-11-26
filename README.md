<p align="center">
  <img src="assets/icon/catenae-icon-square.svg" width="160" alt="catenae: a cat sitting in a cocktail glass, its tail a decaying sinc wave">
</p>

# catenae

**C**hainable **A**udio **T**ransforms for **E**mbedded, **N**ative & **A**ccelerated **E**nvironments.

A C++17 library of audio processing *stages*, the *chain* that composes them, the
*junctions* that join chains across threads and clocks, and the *mailboxes* that carry a
value between threads. No heap, no exceptions, no RTTI; every contract is stated as numbers
and pinned by a test.

**Status: the chain, its first stages, the first junction and the mailbox.** The builds, CI
on every target, the measurement harness and the contracts are in place, and a chain of
stages written as a type runs on every target: `map`, a `gain` that ramps, a fixed `delay`,
the layout converters and `convert` among `float`, `q15`, `q31` and Q4.27; at its edges,
`unpack` and `pack` move samples between ten wire formats and those. A `biquad` cascade runs
a design made off the audio path, one of the cookbook's nine responses or a Butterworth of even
order up to 16, by a `make()` that can fail or from a table in ROM that is checked when it is
compiled. `steady` makes a
stage that works in blocks frame-synchronous, and `parallel` runs two chains on two groups of
a port's channels. `handoff` joins two threads of one clock with a fill policy stated as
numbers, `feeding` and `fed_by` run a chain on either side of it, and a `mailbox` carries a
value from one thread to another; none of them waits. The example chain's memory, start-up
and stack are pinned numbers ([`bench/README.md`](bench/README.md)), and on the Cortex-M the
link-stage gate finds no software `double` in a `float` image of these stages.
The design, its decisions and the milestones are in [`PLAN.md`](PLAN.md); the guide and the
reference are published at <https://tap.github.io/catenae/>.

## Using it

catenae is built from source in your build, with your target's flags, and never shipped
prebuilt. It needs a C++17 compiler; it compiles the same at C++20 and C++23.

- **CMake**: `add_subdirectory(catenae)`, then link `catenae::catenae`.
- **waf**: the `wscript` builds the same files (`waf configure build`).
- **Anything else**: `scripts/amalgamate.py --out DIR` writes the compiled core as one
  `catenae.cpp` beside a copy of the headers and a `catenae/config.h`. Compile it with
  floating-point contraction off (`-ffp-contract=off`).

A failed check calls `catenae::check_failed(check)`, which must not return, and which the
library leaves to you to define: it is where your system enters its safe state. A trapping
one ships apart, as `catenae::trap_handler` (CMake), `catenae_trap_handler` (waf) or
`check_failed_trap.cpp` (the amalgamation). The checks of your buffers' sizes run in every
build; the library's checks of its own promises run when you build with
`-DCATENAE_CHECKS=audit` (`waf configure --checks=audit`, `amalgamate.py --checks audit`),
and code built at one level does not link with code built at the other.

```cpp
#include <array>

#include "catenae/delay.h"
#include "catenae/gain.h"

// A stereo strip, as a type: a level that ramps over 96 frames when it changes, then a
// delay of exactly 32 frames. The configuration is a constant; joints are checked here.
constexpr auto strip = catenae::gain<2>{0.5f, 96} | catenae::delay<2, 32>{};

// Its memory: one instance's state and the thread's workspace, plain storage.
inline catenae::memory_of<decltype(strip)> strip_memory{};

// strip.reset(strip_memory.state) once, then every callback. The blocks are arrays, so
// every size is checked by the compiler.
inline void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(strip.process(strip_memory, in, out));
}
```

The build compiles that block on every leg, so it cannot drift from the library.

## Targets

CI builds and runs the battery on Linux (GCC, Clang, arm64), macOS (AppleClang) and Windows
(MSVC) at C++17, 20 and 23, under the address, undefined-behaviour and thread sanitizers, and
under emulation on the Cortex-M4 (soft float and M4F), Cortex-M33, Cortex-M55 and Hexagon.
How each is built and measured is in [`CLAUDE.md`](CLAUDE.md) and
[`bench/README.md`](bench/README.md).

## License

The license has not been chosen yet. [`NOTICE.md`](NOTICE.md) records the files carried in
from DspTap and SampleRateTap and the license each had there.
