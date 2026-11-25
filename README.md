<p align="center">
  <img src="assets/icon/catenae-icon-square.svg" width="160" alt="catenae: a cat sitting in a cocktail glass, its tail a decaying sinc wave">
</p>

# catenae

**C**hainable **A**udio **T**ransforms for **E**mbedded, **N**ative & **A**ccelerated **E**nvironments.

A C++17 library of audio processing *stages*, the *chain* that composes them, the
*junctions* that join chains across threads and clocks, and the *mailboxes* that carry a
value between threads. No heap, no exceptions, no RTTI; every contract is stated as numbers
and pinned by a test.

**Status: the ground is laid, and nothing to build a chain with yet.** The builds, CI on
every target, the warning sets and the measurement harness are in place around one compiled
unit, `catenae::version()`. The first stages and the chain arrive with the next milestones.
The design, its decisions and the milestones are in [`PLAN.md`](PLAN.md); the guide and the
reference are published at <https://tap.github.io/catenae/>.

## Using it

catenae is built from source in your build, with your target's flags, and never shipped
prebuilt. It needs a C++17 compiler; it compiles the same at C++20 and C++23.

- **CMake**: `add_subdirectory(catenae)`, then link `catenae::catenae`.
- **waf**: the `wscript` builds the same files (`waf configure build`).
- **Anything else**: `scripts/amalgamate.py --out DIR` writes the compiled core as one
  `catenae.cpp` beside a copy of the headers. Compile it with floating-point contraction off
  (`-ffp-contract=off`).

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
