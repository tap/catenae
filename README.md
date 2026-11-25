<p align="center">
  <img src="assets/icon/catenae-icon-square.svg" width="160" alt="catenae: a cat sitting in a cocktail glass, its tail a decaying sinc wave">
</p>

# catenae

**C**hainable **A**udio **T**ransforms for **E**mbedded, **N**ative & **A**ccelerated **E**nvironments.

A C++17 library of audio processing *stages*, the *chain* that composes them, the
*junctions* that join chains across threads and clocks, and the *mailboxes* that carry a
value between threads. No heap, no exceptions, no RTTI; every contract is stated as numbers
and pinned by a test.

**Status: the contracts are written, and there is no chain to build yet.** The builds, CI on
every target and the measurement harness are in place, and so are the contracts the rest
is built on: the vocabulary (`span`, `expected`, the sample formats, the ports), the checks
and their handler, and the traits that read and check what a stage, a junction and an
analyzer declare. The chain and the first stages arrive with the next milestone.
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
#include "catenae/version.h"

// Whether the compiled core this program linked was built from the headers it included.
inline bool linked_these_headers() noexcept {
    return catenae::version() == CATENAE_VERSION;
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
