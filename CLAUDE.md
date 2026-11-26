# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

**catenae**: Chainable Audio Transforms for Embedded, Native & Accelerated Environments. A
C++17 library of audio processing *stages*, the *chain* that composes them, the *junctions*
that join chains across threads and clocks, and the *mailboxes* that carry a value between
threads.

`PLAN.md` is the specification. Its milestones (section 10) are done in order, C0a, C0b, C1,
CF, then C2 to C9. **C0a** (the builds, CI, the warning sets, the scripts and the harness) and
**C0b** (the contracts: the vocabulary, the checks and their handler, the traits a stage, a
junction and an analyzer meet, the compile-fail battery, the link tests and the generic
harness, with the contract pages in `docs/`) are done, and so is **C1**, in four pull requests:
the chain, its composition and its first stages (`map`, `gain`, `delay`, `interleave` /
`deinterleave`, `convert`); `steady` and `parallel`; `handoff`, `mailbox`, `feeding` and
`fed_by`; and the harness rows (the link-stage gate's first probe image, the layout benchmark,
the example chain's pinned numbers), with the guide's first five chapters. **CF** is done
too: throwaway skeletons of SampleRateTap's three engines and the first C ABI manifest in
`fit/`, whose findings (`fit/README.md`, F1 to F19) the maintainer decided, and the contract
changes they led to (`ratio_t`, `table_view` and `holds_view`, `outputs_from_rest` and the
exact largest pull, `fed_by`'s flush). **C2** is under way, in two pull requests: `unpack`
and `pack` over the ten wire formats of `catenae/wire.h`, their loops the first compiled code
past the handler (`src/wire.cpp`); then dither, the mix matrix and `sanitize`.

## How to work here

- **The decisions in `PLAN.md` section 1 are the maintainer's, and settled.** Do not reopen
  one, re-derive one, or work around one. If something cannot be built as the plan words
  it, stop and report what you tried and what you saw; do not change the decision to fit.
- **One home per fact.** `PLAN.md` says what is decided. `docs/audits/` says why, and holds
  the three audits and the response to each. `docs/sketches/config-state/` is throwaway
  evidence for some of the contracts: it is not library code, and nothing is copied from it
  unread.
- **A measured number is quoted with the command that reproduces it.** A number that cannot
  be reproduced is not written down.
- **A contract header has one owner** and changes only by a decision in section 1. Work that
  finds a contract wrong raises it; it does not patch it in passing.
- A milestone ends with its battery green on every CI leg and its numbers recorded.

## The rules that bind (each with its decision)

- Strict C++17, one code path, no C++20 feature even behind a test; the same files compile
  clean at C++17, 20 and 23. catenae writes its own `span` and `expected` (D2).
- No exceptions, no RTTI, no heap, ever: every build is `-fno-exceptions -fno-rtti`, and
  what can fail returns a `status` or an `expected`, outside the audio path (D21, D22).
- A stage is a *configuration*: a small, trivially copyable constant. What an instance
  remembers is a separate *state*, and what a call needs is a *workspace*; both are plain
  aggregates with no constructor, in `.bss` when static (D34, D50, D54).
- The configuration resets the state before its first use; all-zero storage means "not yet
  reset". A stage writes `reset(state&) const noexcept` or declares `rests_at_zero = true`,
  exactly one (D49).
- Channel counts, sample formats, ratios and layouts are compile-time; a chain is a type the
  caller writes with `a | b | c`, and joints are proven when that type is formed (D13, D6,
  D42, D29).
- A stage publishes how its output arrives (`input_group`, `max_shortfall`, `max_lead`,
  `output_quantum`); nothing is inferred from a missing member (D35, D10).
- Run-time checks exist only where a caller's buffers enter (the *boundary* level, always
  on); the library's own promises are checked at the *audit* level, a build option (D27).
- Two threads meet only through a junction or a mailbox, one thread per side (D19, D44).
- `float` is the default profile and accumulates in `float`; `double` is the golden model;
  fixed point is a tag over raw integers (D24, D36, D29).
- Not header-only: templates whose compile-time numbers matter are headers, the rest is one
  static library (D4). FFT back ends are chosen per sample type at build time (D28).
- Include guards, not `#pragma once`: `CATENAE_<PATH>_<FILE>_H` (D40). Tap naming and
  formatting; public constants have no `k_` prefix (D20, D30).
- Warnings are errors under the strict sets of section 7.3; a suppression is a recorded
  deviation (D31). Written with MISRA C++:2023 and ASIL B in mind; nothing is claimed (D23).
- catenae is not a Tap-family library and has no submodules. DspTap and SampleRateTap are
  sources: code is carried in by copy, ported, and recorded in `NOTICE.md` with the commit
  it came from (D3, D14).
- **No license is chosen yet (D33).** Add no license file and no SPDX or other license
  identifier. The banner is `Copyright 2026 Timothy Place and the catenae contributors`.
- Published literature only, cited; honest limits stated and pinned by a test (D12).

## The guide's chapters (D53)

Every chapter of the guide is written by a spawned agent on the Fable model, whatever model
this session runs on: use the Agent tool with `model: "fable"`, and give it the compiled
examples and the contract pages the chapter rests on. The chapter lands in the same change
as its code. Reference comments in headers and the per-stage contract cards stay with
whoever writes the code. The guide's reader and its three-step chapter shape are in
`PLAN.md` section 11.3.

## Git

- Commit messages follow the log: a one-line subject that says what changed, then a body in
  plain sentences. Work so far has gone straight to `main`.
- Push only when the session has been told it may, and never open a pull request unless asked.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release            # -DCATENAE_CXX_STANDARD=20 or 23,
cmake --build build                                       # -DCATENAE_CHECKS=audit
ctest --test-dir build --output-on-failure                # -L compile_fail, -L '^link$' for those
                                                          # -DCATENAE_BUILD_FIT=ON, -L fit: CF's skeletons

python3 waf configure --std=17 --gtest=/path/to/googletest   # waf from waf.io; GoogleTest at
python3 waf build --alltests                                 # f8d7d77 (v1.14.0); --checks=audit

scripts/tidy.sh                                              # clang-tidy, every finding an error
python3 -m unittest discover -s scripts/tests                # the scripts' own battery
python3 scripts/check_naming.py -p build-tidy                # D30 (needs a Clang compile database)
python3 scripts/check_shifts.py -p build-tidy                # raw signed shifts
python3 scripts/check_guards.py                              # D40
python3 scripts/check_slots.py                               # CAT-MAILBOX-003, DEV-013
python3 scripts/check_suppressions.py                        # D31, against docs/deviations.md
python3 scripts/check_requirements.py                        # the @req trace
python3 scripts/check_budgets.py                             # the length budgets
scripts/build_site.sh                                        # Doxygen + mdBook, links checked
```

The four bare-metal legs: `-DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m4-mps2.cmake` (with
`-DCATENAE_M4_FPU=ON` for the M4F), `arm-cortex-m33-mps2.cmake` or `arm-cortex-m55-mps3.cmake`,
`-DCMAKE_BUILD_TYPE=MinSizeRel`, with `gcc-arm-none-eabi` and `qemu-system-arm` installed; CTest
runs the battery once under QEMU and judges its printed summary. Hexagon:
`scripts/fetch_hexagon_toolchain.sh DIR` (with CI's two `HEXAGON_TOOLCHAIN_*` variables), then
`-DCMAKE_TOOLCHAIN_FILE=cmake/hexagon-linux-musl.cmake` with `HEXAGON_TOOLCHAIN_ROOT` set and
`qemu-hexagon` on `PATH`. Coverage: Clang, `-DCATENAE_COVERAGE=ON`, then
`scripts/coverage.py --build-dir build`. The amalgamation: `-DCATENAE_AMALGAMATION=ON`, or
`scripts/amalgamate.py --out DIR` alone.

The check level (`CATENAE_CHECKS`, `boundary` by default, or `audit`) is written into the
generated `catenae/config.h` and spelled into the inline namespace, so each level is its own
build tree. The library defines no `check_failed`: an image links `catenae::trap_handler` (CMake)
or `catenae_trap_handler` (waf), or its own; the battery's is `tests/support/catenae_test/`. A
compile-fail case (`tests/compile_fail/`) is a source with an `// Expect: ` regex that CTest
builds and must see fail, its control (the case without its defect) in the ordinary build; the
link tests (`tests/link/`) build the core and the handler again at both levels. Both are CMake's,
and run on every CMake leg; waf compiles the controls.

**What CI runs** (PLAN.md section 8: the combinations are written down). Every leg below runs
`boundary` and then `audit` in the same job, the battery, the compile-fail cases, the link tests
and the header checks each time; the measurements are of the shipping level, `boundary`.

| Leg | Compiler | C++ | Build | Levels |
|---|---|---|---|---|
| Linux GCC 13, Linux Clang 18, macOS AppleClang, Windows MSVC | each | 17, 20, 23 | CMake and waf, Release | both |
| ASan + UBSan; TSan | Clang 18 | 17 | CMake, RelWithDebInfo | both |
| Linux arm64, native and under TSan | the runner's GCC | 17 | CMake | both |
| Cortex-M4 (soft float), M4F, M33, M55, under qemu-system-arm | arm-none-eabi-gcc | 17 | CMake, MinSizeRel (-Os, --gc-sections) | both |
| Hexagon, under qemu-hexagon | CodeLinaro clang 19 | 17 | CMake, Release | both |
| The amalgamation | GCC 13 | 17 | CMake over `catenae.cpp`, and the compiler alone | both |
| Coverage | Clang 18 | 17 | CMake, Release | boundary |
| The ratchet and the ceilings (`bench.yml`, six keys) | each key's | 17, 20, 23 (counts); 17 (sizes) | Release; MinSizeRel | boundary |

macOS builds its three standards one after another in one job: hosted macOS runners are scarce,
and a job queued for one too long is cancelled without running.
The `constinit_dynamic` case is GCC's and Clang's only: on MSVC the macro is empty (DEV-001).
The battery is built with contraction off (`catenae_contraction`), as the compiled core is: the
numeric contracts are pinned there, and a header template under a consumer's flags may differ
by what a fused multiply-add moves. Two stages of another namespace join through
`catenae::chain(a) | b` or `using catenae::operator|;`: argument-dependent lookup finds
catenae's `|` only when one operand is a catenae type.

The harness (`bench/README.md`): `-DCATENAE_BUILD_BENCH=ON` builds the ratchet's workloads,
the size and stack probes (`version`; the example strip, whose memory, start-up and stack are
C1's pinned numbers; the strip as a heap object, the guide's strawman; and `c1`, every C1
stage in one float path, the link-stage gate's first image), the layout benchmark and the
link-gate fixtures; `scripts/icount.py` counts and gates the workloads against
`bench/baselines.json`, `scripts/icount.py --prefix catenae_layout_ --report-only` counts
the layout benchmark and gates nothing, and `scripts/ceilings.py measure` measures the probes
against `bench/ceilings.json`. A number is never typed in: it is copied from the CI run that
measured it, and the run is named in `bench/README.md`.

Where things are: `include/catenae/` and `src/` are the library (`src/handler/` the default
handler); `tests/` the battery, with `tests/fixtures/` (the fixture stages, junction and
analyzer) and `tests/support/` (the check trap and the generic harness every stage runs);
`examples/` what the guide includes (its strip, a stage of the reader's own with its battery,
the run-time graph the guide measures the chain against, a block stage bare and in
`steady<>`, whose trace `scripts/figure_steady.py` draws `guide/src/figures/steady.svg` from, a
CTest check failing when the committed figure is not what they draw, and a mailbox and a
`handoff` between threads; `racy_level.cpp`, the race they replace, is built and run only
under the thread sanitizer, by a check that passes only on its report); `tests/bare_metal/`
what only the Cortex-M legs build (a timer interrupt as the mailbox's poster and the
junction's producer); `fit/` CF's skeletons, which nothing under `include/` or `src/`
includes and C7 and C8 replace; `bench/` the harness's workloads
and records; `cmake/` and `platform/` the targets; `tools/warnings/` the warning sets, read by
both builds; `scripts/` the checks and the harness; `guide/` the mdBook source and
`docs/Doxyfile` the reference's, over the headers and `docs/contracts/` (the stage, chain,
junction, mailbox and analyzer contract pages, the glossary, the card and `@req` conventions),
`docs/assumptions.md` (AOU-nnn) and `docs/deviations.md` (DEV-nnn); `.github/workflows/` CI
(`ci.yml`, `style.yml`, `bench.yml`, `docs.yml`).
`docs/sketches/config-state/` is still throwaway evidence for the plan: none of the above
builds on it, and its own `verify.sh` and `measure.sh` still print what `PLAN.md` section 9
quotes.
