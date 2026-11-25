# The response to the third audit

The audit is `2026-11-25-plan-folded.md`: 23 findings on `PLAN.md` and the sketch at commit
`4b1ff30`. This file records what was done about it, so that `PLAN.md` can state only what
is decided. It is a record: it is not kept up to date with the plan, and where the two
differ the plan is right.

## The decisions

The five major findings were two subjects. The maintainer accepted the recommendation on
both, and on three smaller points, on 2026-11-25 ("I think the recommendations are fine").
Each row is the problem, what was decided, and what was set aside. Measurements are
arm-none-eabi-g++ 13.2 for a Cortex-M33 at `-Os`, or the hosted sketch, unless a row says
otherwise.

| Decision | The problem | Decided | Set aside |
|---|---|---|---|
| D35, amended (findings 1, 5, 8) | **The contract bounded how far a stage's output can trail what it is owed, and nothing bounded how far it can lead.** The sketch's toy decimator wrote a pair's output on its last frame, so it only trailed. DspTap's decimator and SampleRateTap's L/M stage write on a group's first frame: one frame in from rest gives one frame out. With such a stage inside `steady<>` the FIFO was one frame short: one 1-frame call and then forty 64-frame calls gave 40 wrong frames of 2,561, every index in bounds, no check firing. | Each stage publishes `max_lead` beside `max_shortfall`, both counted from reset, and the chain composes both. `steady<>` primes by the shortfall and its FIFO holds the priming, the lead and the largest burst; the audit level checks it for running over as well as short. The pull side's staging is sized by shortfall plus lead. The sketch's `down2` now uses the carried convention (`down2_late` keeps the other, for the tests), and C1's toy 2:1 fixture exists in both. | A rule that no stage may lead: it changes the carried decimators' phase and latency, and so the vectors C7 compares bit for bit. |
| D35, amended (finding 17) | **The composed shortfall was sound and loose,** and C1 was about to pin its numbers as `steady<>`'s latency: 222 frames for a 96-frame block before a 128-frame one where the true worst backlog is 191, 990 for 480 before 512 where it is 959. | The composition carries the quantum in which frames reach each stage, and a stage fed in multiples of q trails or leads by its number scaled by (group - gcd(q, group)) / (group - 1). A stage publishes `output_quantum`. Adopted because the brute force below never found it too small. | Keeping the plain sum and stating the slack. |
| D51, amended (findings 4, 7) | **`handoff` started paying out when its fill reached the target and never brought the fill down to it.** A producer 2, 5, 12 and 20 periods early left the fill at 192, 384, 832 and 1,024 for good, with no underrun to show it; a producer late by 3 or 10 periods that then delivered its backlog left 256 or 704. The lower bound on the target was also wrong for unequal blocks (480-frame pushes beside 64-frame pulls underrun at a target of 480), and the plan's own 9.6 example broke it. | On every change from filling to running the consumer discards the oldest frames above the target. The target is at least the largest pull plus the largest push, less a number that divides every push and every pull. `handoff` takes the largest push as a template argument and publishes `serves()`; `fed_by` asserts it at compile time. A stalled consumer raises the latency until an overrun or `restart`, and the plan says so. | Stating the nominal latency as "target to capacity". A high-water trim for a stalled consumer: deferred. |
| D49 and D10, amended (findings 3, 15, 21) | **A `reset` that was mistyped was silently replaced by zeroing.** Reset was the one member `stage_traits` detected; a `gain` whose `reset` lacked `const` was not seen, started from zero (first sample 0.0104 at a level of 1.0), and the generic garbage-against-zero test still passed. | A stage writes `reset(state&) const noexcept` or declares `rests_at_zero = true`, and `stage_traits` asserts exactly one. Zeroing is chosen by a declaration, never by a missing member. A chain's state carries the `ready` word, and `fed_by` and `flush` test it; a junction's state and a mailbox rest at zero and carry none. | Detecting any member named `reset` and keeping the default. |
| D43, amended (findings 2, 10) | **"Its delay is placed by hand" named no construct.** `parallel<A, B>` refuses an arm with a recursive filter whatever the other arm is, and nothing else runs two chains on two channel groups. | `parallel<A, B, align::by_caller>` asserts only frame-synchrony and equal ratios, inserts no delay and reports no exact latency. `delay<S, Ch, Frames>` is a fixed delay whose exact latency is `Frames`, a constant of its type; a delay set at run time is deferred. | A `parallel` that re-aligns at run time: still deferred. |
| D54, new (finding 11) | **The 256-byte ceiling on a configuration, chosen while folding in the second audit, refused the plan's own types:** every chain as the sketch built it (`std::tuple` is not trivially copyable), and an order-16 Butterworth in `double`, 320 bytes of coefficients. | A stage's own configuration is trivially copyable and at most 512 bytes. A chain holds its stages' configurations in a plain aggregate and is exempt from the size, so it is trivially copyable and can be posted through a mailbox. | Holding the `double` design by view while the `float` one is held by value. |
| D19, amended (findings 6, 23) | **A state that holds `std::atomic` members has a constructor from C++20 on.** The sketch's junction state got start-up code at C++20 and 23 and none at C++17, against D2; `memory_of` and `fed_by_workspace` got it at every standard, because `std::tuple` zero-fills (20 bytes of start-up code and a `memset` of 512 bytes for the strip). | A workspace, a chain's configuration, `memory_of`, the pull side's memory and a junction's state are plain aggregates, asserted. A junction's shared words are plain 32-bit words read and written through atomic operations in the one compiler-extension header. `measure.sh` counts start-up sections at C++17, 20 and 23: none. Every static object in the embedded example is declared with the constant-initialisation macro. | A `constexpr` constructor on the junction state: it zero-fills the ring wherever the state is not static. |

## Brute force

**The count model** (`docs/sketches/config-state/pace_model.py`). Each stage is a function
from frames in to frames out since reset: interpolators by 2 and 3; decimators by 2, 3 and 4
that write on a group's first frame; decimators by 2 and 3 that write on its last; L/M
stages 3/2 and 2/3 of both kinds; block stages of 2 to 12 frames. For a chain, the true
worst trail and lead are found by evaluating every N over at least eight periods.

| Family of chains | Chains | Plain sum too small | Quantum rule too small | Quantum rule exact |
|---|---:|---:|---:|---:|
| Pairs of block stages, sizes 1 to 40 | 1,600 | 0 | 0 | 1,600 |
| Triples of block stages, sizes 1 to 16 | 4,096 | 0 | 0 | 3,363 |
| Every chain of 1, 2 and 3 stages over 18 kinds | 6,174 | 0 | 0 | 4,639 |
| Random chains of 4 stages | 6,000 | 0 | 0 | 3,492 |
| Random chains of 5 stages | 3,000 | 0 | 0 | 1,351 |

Both rules bound shortfall and lead separately, counted from reset. The quantum rule gives
191, 959, 951, 383, 255, 10 and 127 for 96\|128, 480\|512, 441\|512, 160\|256, 128\|192, 7\|5
and 128\|64, each the true worst backlog. Its largest slack was 24 frames, on a five-stage
chain of three block stages and two interpolators.

**The real stages** (`pace.cpp`, address and undefined-behaviour sanitizers, audit checks
on). Thirty chains of the sketch's stages: decimators of both conventions, interpolators,
block stages, a chain nested in a chain, `steady<>` inside a chain. For each, by counting
one frame at a time over 2,047 frames: the published `max_shortfall` and `max_lead` are
never below what was found, and equal it in 27 of the 30. `steady<>` over each 1:1 chain,
in nine call patterns (1, 2, 3, 7, 37, 63, 64 and 65 frames and random sizes, alternate
patterns starting with one odd call): no frame differs from one straight run delayed by the
stated latency. The pull side over each chain, random requests of up to 300 frames: every
request exact and bit-equal to a straight run, at most one pull each, the largest pull
never above the staging.

**Lead does have to be in the pull side's staging.** With only the shortfall, a chain of
leading decimators publishes 0 and the staging is too small by the lead: the audit's
verifier found 27 frames needed against a staging of 9.

## Re-measured

Cortex-M33, `-Os`, from `measure.sh`. Rows not listed did not move.

| | Before | Now |
|---|---:|---:|
| Overdrive configuration (`.rodata`) | 8 bytes | 16 bytes: the chain holds its stages in a plain aggregate, and a stage with no data takes a byte |
| `reset` of two voices | 36 bytes of code | 44 |
| Audio callback, static worst case down its call path | 176 bytes | 184 |
| `handoff` `push`: code, frame | 100, 20 bytes | 96, 24 |
| `handoff` `pull`: code, frame | 148, 24 bytes | 168, 24 |
| Linked image with table designs only | 3,084 bytes of text | 3,080 |
| The run-time designer's cost | 8,316 bytes | 8,320 |
| Start-up code in the embedded example at C++17, 20, 23 | present at all three: 20, 40 and 40 bytes and an `.init_array` entry (the audit's measurement; `measure.sh` did not look) | none at any of the three |
| `steady<>` latency, 96 \| 128 and 480 \| 512 | 222 and 990 frames | 191 and 959 |
| `steady<>` latency, down by 2 then up by 2 | 1 frame | 0 with the carried decimator; 1 with one that writes on a pair's last frame |

## Choices made while folding it in

These are not the maintainer's decisions; they are how the accepted ones were written down.

- **Shortfall and lead are counted from reset,** not "from any state". It is the definition
  under which `steady<>`'s priming is exactly the shortfall, and it makes the carried
  decimator's numbers 0 and (M - 1) / M. The per-call bound a pull needs is the two added.
- **The scaling law is a requirement on a leaf stage,** checked by the harness's count test
  for every divisor of the group; a chain is composed through exactly, not by the law.
- **`output_quantum` is a fourth published constant.** Without it a block stage's output
  could not be told from a stage that trickles, and 96\|128 would stay at 222.
- **The one-frame-short-of-a-group bound was dropped.** The quantum rule is never looser
  where it applied (128\|64 is 127 by either).
- **`rests_at_zero` is read through two small detections** (is the constant declared true;
  is `reset` callable on a const stage) in order to assert exactly one. D10's rule is kept
  in the sense that matters: nothing is chosen because a member is absent.
- **`handoff` takes its largest push as a template argument** (`handoff<S, Ch, Capacity,
  Target, Push>`), with a boundary check on `push`; without it the junction could not
  publish `serves()`.
- **`fed_by` asserts the target with a quantum of 1,** the conservative form, because its
  pulls vary in size. So 9.6's example needs a target of 320 for requests of up to 256
  beside 64-frame pushes, where a caller pulling one fixed 256-frame block by hand could
  use 256. The rule is therefore checked by the library wherever it makes the pull, and
  remains the caller's arithmetic only for hand-written fixed-block pulls.
- **A junction's shared words use the compiler's atomic operations** on plain words, not
  `std::atomic`. This is one more compiler extension, in the one header that holds them.
- **`with_block<N>(...)` returns a class template of its own,** and as an operand of `|` it
  is one stage, not flattened.
- **The configuration ceiling is 512 bytes.**
- **`results_ready` is a running count** the reader differences, not a consuming read.
- **A flush buffer must hold one burst,** and the exact tail is asked for with a fourth
  argument, `tail::exact`.
- **The unstable-table compile-fail test moved to C3,** where the first checked table
  design arrives, instead of adding a fixture design to C1.
- **Finding 14's item on latency composition missing from the C1 row** was called pedantic
  by the verifier; the row names it now anyway.

## Where each finding went

Numbers are the audit file's. "Plan" means `PLAN.md` at the commit that adds this file.

| # | Finding, in short | Where |
|---:|---|---|
| 1, 5, 8 | No bound on lead; the carried decimators lead | D35; plan 3.2 (rows and "Trailing, leading, and the law"), 3.4, 4, 9.5, 12; C1's fixtures and `steady` battery; sketch `max_lead`, `down2`, `down2_late`, `steady`, `pace.cpp` |
| 2, 10 | Nowhere for a hand-placed delay; what `delay`'s argument means | D43; plan 4, 9.7, 13 |
| 3, 15, 21 | A mistyped `reset` silently zeroed; where the ready check is | D49, D10; plan 3.2, 3.4, 8, C0b, 11.2's card; sketch `reset_state`, `must_not_compile.cpp` cases 9 to 11, the ready check in `fed_by`. Finding 21's claim that the triviality and size assertions "cannot be written" was overstated and is not acted on beyond D54 |
| 4, 7 | `handoff` never trimmed to its target; the bound; 9.6 | D51; plan 6.1, 3.4, 8, 9.6, 13, C1's pins; sketch `handoff`, the demo's link runs, `must_not_compile.cpp` case 12, `handoff_stress.cpp` |
| 6, 23 | Start-up code; a junction state with atomics; `latency()` without a state | D19, D50; plan 3.2, 3.3, 6.1, 7.3, 8, 9.2, 9.6; sketch `pack` for workspaces and configurations, `detail::word`, `is_plain`, `CATENAE_SKETCH_CONSTINIT`, `measure.sh`'s start-up count. The lifetime point about caller-supplied storage is moot with plain words |
| 9, 12 | Flush: a small buffer, termination, the cursor, the exact tail | D9, D27; plan 3.4, 4, 7.5. Not in the sketch |
| 11 | The configuration ceiling | D54; plan 3.3; sketch: a chain's configuration is trivially copyable (asserted in the demo) |
| 13, 16 | The mailbox's compile-time lifetime test cannot be built | plan 7.3, 8 |
| 14 | Leftovers | (1) the unstable-table test to C3; (2) C1 row; (3) probe images in C3 and the harness-rows workstream; (4) `CATENAE_CONSTINIT` in 9.2 and 3.3, with MSVC stated; (5) 9.1 and 9.2 marked as excerpts; (6) 5.1's stereo x86-64 figure; (7) the section 9 preamble narrowed, and 9.4 says where its other numbers came from |
| 17 | The shortfall rule was loose | D35; plan 4, 9.5, C1; sketch `detail::arrival`, `composed_pace`; `pace_model.py` |
| 18 | "One pull per request" is at most one | D52; plan 6.1, CF; the demo's holdover-covered request |
| 19 | `oversample`'s padding | plan 4 |
| 20 | The in-place condition; `with_block` | plan 3.2, 4. Sized by the largest bound in the run, as the verifier suggested |
| 22 | The analyzer's "since the last read"; reaching a tap's state | plan 6.2, 4, 9.6; sketch `chain::stage_state` |

## What is still only on paper

The sketch still has no `stage_traits` or `junction_traits` as headers, no `latency()` or
`exact_latency`, no `parallel`, `oversample`, `flush`, `feeding`, in-place sharing,
`with_block` or analyzers. The third audit's third reviewer wrote each of them once in a
scratch directory, which is where findings 2, 9, 10, 19, 20 and 22 came from; that code was
not kept. They are first compiled at the milestone that delivers them.
