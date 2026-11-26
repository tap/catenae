<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# fit: the fit milestone's skeletons (PLAN.md section 10, CF)

Throwaway skeletons of SampleRateTap's three engines, written against C0b's and C1's
contracts to find where those contracts do not fit, before anything from C2 to C6 rests on
them. Nothing under `include/` or `src/` includes them; C7 and C8 replace them. Built only with
`-DCATENAE_BUILD_FIT=ON` (which needs the battery), into one GoogleTest target labelled `fit`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCATENAE_BUILD_FIT=ON
cmake --build build
ctest --test-dir build -L fit --output-on-failure
```

CI builds and runs it on the Linux GCC and Clang legs at C++17, 20 and 23 and both check levels,
and on the Cortex-M33 under QEMU, where the whole target takes 10 to 14 s of wall clock on a
four-core x86-64 container (`ctest --test-dir build-m33 -L fit`, after configuring with
`cmake/arm-cortex-m33-mps2.cmake`; a range, not a pin).

| Directory | What it is | Tests |
|---|---|--:|
| `rational/` | `stage<S, Ch, Ratio, Design>` on SampleRateTap's two machines, designs as types, named chains, pull and flush | 76 |
| `bridge/` | One 160/147 stage over a table design made into the caller's storage | 14 |
| `async/` | The asynchronous converter as a junction, carrying SampleRateTap's real servo | 9 |
| `capi/` | The first C ABI manifest (PLAN.md 7.5) | 7 |
| `MEMBERS.md` | Every public member of SampleRateTap's engines, and where it lands or why it is dropped | 647 rows |

What was carried from where is in `NOTICE.md` (SampleRateTap at `4496888`, DspTap at
`b26a7db`).

## What fits as written

- **`stage_traits` expresses every machine.** The rational stage's pre-advance, schedule and
  mixed-ratio lead, and bridge's input group of 147 with a lead of 146/147, are published
  numbers the generic harness checks unchanged; `max_outputs_for` is a tight ceil(n L/M).
  Every stage matches a plain upfirdn reference (bridge bit for bit, rational within 4e-6 of
  double and bit for bit across chunkings), and its counts are exact from every position.
- **Chains, `flush` and `fed_by` work with rate changers.** The 3/2 converter behind a
  `handoff` at 64-frame requests makes one pull per request (43, 42, 43, ...), with no
  underrun, and its output equals a straight run. All 36 two-stage pairs compose safely.
- **The junction contract carries the asynchronous converter**: plain-storage state at rest
  all zero, `latency(state)` breathing within a push of the target, `restart`, `report`.
- **D54 and D37 hold**: rational's configuration is one byte, bridge's one pointer to its
  table (a 2,560-byte table held by view), async's 160 bytes; designs come from `make()` as an
  `expected`.

## CF's exit test for `async`

"The `async` skeleton carries the real servo and must lock, with no underrun, behind
`fed_by` at a request larger than the chain's block and at one that leaves a holdover."
Simulated on one thread with two clocks at +200 and -200 ppm (`async/test_async.cpp`):

| Behind | Requests | Pulls | Pushes | Result |
|---|---|---|---|---|
| a 128-frame block | 64 | 128, 0 | 32 | locks at 1.62 / 1.17 s, no underrun, estimate within 1 ppm |
| a chain by 4 | 102 | 26, 25 | 1 | locks at 0.74 / 0.98 s, no underrun |
| a 3/2 chain | 64 | 43, 42, 43 | 1 | locks at 0.62 / 0.68 s, no underrun |
| a 128-frame block | 160 | 256, 128, 128, 128 | 32 | **never locks** (mean -124 / -623 ppm), no underrun |
| the same in `steady<>` | 160 | 160 | 32 | locks at 1.84 / 1.40 s |

SampleRateTap's own converter, run with the same bank and the same pulls, gives the same
numbers to the printed digit. So a pull that varies by a whole block, which a request larger
than the block produces, defeats the carried servo; PLAN.md 6.1's own example (a block larger
than the request, pulls 128, 0) locks. Separately, the holdover patterns lock only with
one-frame pushes: with 32-frame pushes, 26/25 and 43/42 swing between the servo's clamps, and
so do regular 24, 25, 26 and 48-frame pulls on SampleRateTap's converter. Both are inputs to
C8, which rewrites the servo anyway (section 12); finding F1 asks what CF and the contracts
make of the first.

The servo runs in `double` and calls `exp` on every pull, as carried: a probe of push and
pull is refused by the link-stage gate on the M33 (48 symbols) and would be on the M4, M4F
and M55. On the M33 a servo update averages 4,383 instructions, 12.6 % of a 64-frame stereo
pull.

## The first C ABI manifest

PLAN.md 7.5's form, tried: `capi/catenae_manifest.h` is one plain-C X-macro list expanded into
`catenae_c.h` (capacities, storage macros that carry the alignment, declarations) and into
`catenae_c.cpp` (the definitions, each capacity `static_assert`ed where it is compiled). Every
size, alignment, frame count and pointer is checked, and every failure is a status. Five
entries (C1's strip in stereo and mono, rational's 3/2 converter, bridge's stage, async's
junction), **54 functions**, which the shared target `catenae_c` exports exactly
(`nm -D --defined-only build/fit/libcatenae_c.so | grep -c " T catenae_c_"`). Its compile and
link takes **about 1.5 s** with GCC 13 or Clang 18, one job, on a four-core x86-64 container
(`rm build/fit/CMakeFiles/catenae_c.dir/capi/catenae_c.cpp.o; time cmake --build build --target catenae_c -j1`;
a wall-clock time, so a range, not a pin).

What the skeletons added to 7.5's form: a junction list (push, pull, the report copied out); a
design list for a table made into the caller's storage, with capacity columns for the table and
the designer's scratch and the spec's parameters as a parenthesised list expanded into both the
declaration and the call; a maker list for a configuration made from numbers; and
`configuration_copy`, which constructs the shipped configuration in the caller's storage so that
a setter and `init` act on a real object.

## Findings, and what was decided

Each is a finding of the skeleton named. The maintainer decided each; what was made, and
where, is in the last column. The skeletons use what was made, and each test that pinned a
finding now pins its answer.

| # | Finding | Decided, and made |
|---|---|---|
| F1 | A pull that varies by a whole block defeats the async servo (above); CF's test as worded fails | CF's test is read as 6.1's example; the whole-block swing and the 32-frame-push swing are C8's input; the async junction's card says to wrap a chain whose block is smaller than the request in `steady<>` |
| F2 | A run-time-made configuration cannot be constant-initialised: no design exists before `make()`, and a design has no default (bridge) | A `constexpr design::bind(storage)`, the design over the caller's storage before `make()` fills it in place (`AMadeTableLeavesNothingToRunTimeButMake`); PLAN.md 3.3's wording is proposed below |
| F3 | `catenae::ratio` is a value; C++17 takes no class as a template argument, so `stage<S, Ch, Ratio, Design>` needs a type (rational; MEMBERS.md open question 8) | `catenae::ratio_t<L, M>`, its `value` reduced, its `type` the reduced spelling (`ratio.h`, CAT-RATIO-004); the rational skeleton names its ratios with it |
| F4 | A named chain cannot be default-constructed even when every stage can (rational) | A `constexpr` default constructor when every stage has one (`chain.h`, CAT-CHAIN-014); the skeleton's `make<Chain>()` is gone |
| F5 | `check.h` has no code for a table that fails its check at run time (bridge) | `check::table_rejected`, 34; bridge's `from_table` uses it |
| F6 | `expected`'s accessors are not ref-qualified: `*make(...)` gives `T&` (bridge) | Of an rvalue, `value()`, `operator*` and `error()` return a copy, not `T&&` as `std::expected` does, so `const auto& d = *make(...)` does not dangle (CAT-EXPECTED-006) |
| F7 | `span<const T>` binds a temporary array, so D54's "a view-taking constructor refuses a temporary" cannot be written over it (bridge) | `catenae/table_view.h`: `table_view<T, N>`, made from an lvalue array or written storage only (CAT-TABLE-VIEW-001, 002); bridge's design holds one and its stage takes one |
| F8 | `mailbox<bridge \| gain>` compiles though the chain holds a view (bridge; AOU-004) | `holds_view<T>`, false unless specialised, composed by a chain, `steady<>` and `parallel<>` (CAT-TABLE-VIEW-003); a mailbox of such a type does not compile (CAT-MAILBOX-004, `compile_fail/mailbox_holds_view.cpp`); bridge's design and stage specialise it |
| F9 | `fed_by` has no end of stream: the skeleton flushed by reaching into its state (rational) | `flush(chain, st, ws, out)`: the holdover, then the chain's resumable flush, pulling nothing; `flushed(chain, st)` (CAT-FED-BY-004); the rational pull test drains through them |
| F10 | `fed_by`'s largest pull counts a lead a first-frame writer never realises: 389 sized, 384 reached behind a decimator by 6 at 64-frame requests; async's target, configured from it, costs 94 frames over the run-time raise (rational, async) | A stage member, `outputs_from_rest(n)`: declared when `input_group` is above 1, derived as n L/M otherwise, composed by a chain (CAT-STAGE-013, `compile_fail/stage_group_without_count.cpp`), and checked by `counts_are_exact`. `largest_pull` is enumerated over one period of that count, the bound from any state where the count does not repeat (CAT-FED-BY-003): 384 behind the decimator by 6, and 128, not 191, behind a 128-frame block at 64-frame requests, where async's configured target is now 192, the raise itself. PLAN.md's wording is proposed below |
| F11 | The async junction keeps SampleRateTap's run-time raise of its target, and `serves()` asks whether the raise for the largest pull stays within the ring; `quantum` means nothing to a resampler; the junction contract is silent on a configuration made by `make()` (async) | Said on the junction contract page, "What the members leave to the junction"; no member changes |
| F12 | `compiler.h`'s atomic operations take a 32-bit word only; the report's `float` fields went through `memcpy` (async) | `float` loads and stores beside them (CAT-COMPILER-003, DEV-004); async publishes its estimate and fill as floats |
| F13 | A junction has no workspace: the resampler's blended row is on the stack (64 B here, 320 B at 80 taps) (async) | Recorded; C8 measures |
| F14 | SampleRateTap's 64 spare history frames per line become per-instance `.bss` (1,112 B for the 3/1 stereo state) (rational) | Recorded; a template argument of the stage at C7 |
| F15 | Designs are keyed by band, taps per branch and attenuation (SampleRateTap's 140 keys are 46 tables), not by profile and divisor; the rows a kernel runs belong to the stage (rational) | Left for C7; PLAN.md 3.3 is unchanged |
| F16 | Composition is safe but loose for 4 of 36 pairs; a chain's flush length differs from SampleRateTap's, so C7 compares flushes as a prefix; `tail_frames` need not be the least (rational, bridge) | Recorded for C7 |
| F17 | SampleRateTap's `up2` is a ratio; PLAN.md 9.4 and C3 spell `up2<Ch>` as a stage (rational) | The stage keeps the name; the skeleton names no ratio `up2` and writes `catenae::ratio_t<2, 1>` |
| F18 | The C ABI needs an exported `make()` for a table design and a status-returning table check (bridge) | The design and maker lists above; a checked `from_table` returning an `expected` at C6 |
| F19 | On the M33 the 64-bit count arithmetic links `__aeabi_uldivmod` into bridge's audio path; the gate lists software double only (bridge) | Recorded; C7 measures |

### Proposed wording for PLAN.md

The plan is the maintainer's to edit; these are the sentences the decided changes would put
there.

- **Section 3.3, "Two kinds"** (F2), after "with `constexpr` size functions": "A table
  design also has `constexpr bind(storage)`, the design over the caller's storage before
  `make()` fills it, so a configuration made at run time is constant-initialised and
  `make()` fills its table in place. Until then the table is all zero, a filter that writes
  silence."
- **Section 3.2's member table** (F10), a row after `max_outputs_for(n)`:
  "`outputs_from_rest(n)` | Exactly what the first n frames after reset write,
  `static constexpr`. Declared by a stage whose `input_group` is above 1; `stage_traits`
  derives n L/M for any other, and a chain composes it. `fed_by` sizes its pull from it."
- **Section 6.1, `fed_by_workspace`** (F10), for "publishes its size as the largest pull:
  the largest request plus the chain's `max_shortfall` and `max_lead`, at the chain's input
  rate": "publishes its size as the largest pull: the most input frames a request of
  `MaxRequest` needs from the states `fed_by` reaches, enumerated at compile time over one
  period of the chain's count from rest (`outputs_from_rest`), or, where that count does not
  repeat in the input group, the request plus the chain's `max_shortfall` and `max_lead` at
  its input rate."
- **Section 6.1** (F9), after the paragraph on `fed_by`: "`flush(chain, state, workspace,
  out)` ends the stream on the pull side without pulling: the holdover, then the chain's
  resumable flush; `flushed(chain, state)` says when it is finished."

`MEMBERS.md` closes with the ten questions its 53 unplaced rows raise; F3 answers one, F11 and
the async skeleton's all-zero rest (its state stores only the raise, 0 for none) another two.

Found and fixed in passing: GCC 13's `-Warray-bounds` on `fed_by`'s holdover copies when inlined
at `-O3` with audit checks (`include/catenae/fed_by.h` now bounds the count by the holdover's
room).
