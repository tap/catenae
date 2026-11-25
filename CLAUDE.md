# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

**catenae**: Chainable Audio Transforms for Embedded, Native & Accelerated Environments. A
C++17 library of audio processing *stages*, the *chain* that composes them, the *junctions*
that join chains across threads and clocks, and the *mailboxes* that carry a value between
threads.

Nothing is built yet. `PLAN.md` is the specification. Its milestones (section 10) are done in
order, C0a, C0b, C1, CF, then C2 to C9; the next one is **C0a**: the builds, CI, the warning
sets and the test harness, with no library code beyond a first compiled unit.

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

The library's build and test commands arrive with C0a and are kept current here from then
on. Until then the only code is the sketch:

```sh
docs/sketches/config-state/verify.sh    # GCC and Clang at C++17, 20 and 23; sanitizers; the refusals
docs/sketches/config-state/measure.sh   # Cortex-M33 sizes, stacks and images; needs arm-none-eabi-g++
```

Both print what `PLAN.md` section 9 quotes. If one of them stops agreeing with the plan, the
plan's number is corrected in the same change.
