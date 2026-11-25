# The response to the audit of the tidied plan

The audit is `2026-11-25-plan-tidied.md`: 53 findings on `PLAN.md` at commit `9f7eabb` and
on the sketch under `docs/sketches/config-state/`. This file records what was done about
it, so that `PLAN.md` can state only what is decided. It is a record: it is not kept up to
date with the plan, and where the two differ the plan is right.

## The decisions

The seventeen major findings were seven subjects. The maintainer accepted the
recommendation on six of them as made, and decided the seventh (how a state reaches its
starting value) differently from the recommendation, on 2026-11-25. Four smaller decisions
were accepted the same day. Each row is the problem, what was decided, and what was set
aside. Measurements are arm-none-eabi-g++ 13.2 for a Cortex-M33 at `-Os`, or the hosted
sketch, unless a row says otherwise.

| Decision | The problem | Decided | Set aside |
|---|---|---|---|
| D35, amended (findings 1, 5, 15) | **`is_frame_synchronous` was declared per stage and combined by AND, around an "input group" that nothing published.** A chain that goes down by 2 and then up by 2 is 1:1, passed the flag, compiled for fixed blocks, and returned 2, 4, 2, 4 frames for 3 in; by 3 at 64 frames it returned 63, 63, 66, the case D35 was made to exclude. The stricter wording of draft 32 failed the other way: it refused the plan's own overdrive chain, which really is n for n. And a block stage called in whole hops was refused the compile-time entry it is entitled to. | Each stage publishes `input_group`. The chain composes it as the smallest n for which the frames arriving at every stage are a whole number of that stage's groups. `is_frame_synchronous` is derived (`input_group == 1`). The fixed-block entry asserts a whole number of input groups. In the sketch: down then up has group 2, up then down 1; a 3-frame fixed block does not compile; a block stage of 128 takes 128-frame fixed blocks without `steady<>`. | Keeping a declared flag and fixing only its wording: no wording composes by AND. Deriving the flag as `input_group == rate_ratio.down`, which calls a lone decimator frame-synchronous: the word then no longer means "any n", which is what a callback and `steady<>` need. |
| D35, amended (findings 7, 18) | **`steady<>` primed its FIFO with the largest burst less one, which is not the worst backlog.** For two block stages whose sizes do not nest the FIFO ran short, the count wrapped, and stale frames were returned with every index in bounds: 22 of 4,096 frames wrong for a 96-frame block before a 128-frame one, 9,519 of 25,600 for 480 before 512, under the sanitizers with nothing reported. | Each stage publishes `max_shortfall`, a ratio in output frames. The chain's is the smaller of two bounds: the stages' shortfalls carried to the output rate and summed, and one frame short of an input group. `steady<>` primes with it, rounded up, and states it as its latency. In the sketch: 222 frames for 96 before 128, 990 for 480 before 512, 10 for 7 before 5, 127 for 128 before 64, each reproducing its input exactly at that delay; an audit check that the FIFO never runs short. | The exact backlog (191 and 959 for the first two): it is not a simple function of the stages' numbers, and a bound is all `steady<>` needs. Priming with `input_group - 1` alone: sufficient, but 383 and 7,679 frames for the same two chains. |
| D49, new (findings 2, 6, 13, 20) | **A state was "valid when all zero" and reset was "zero it", with no operation named.** The sketch's chain state was built on `std::tuple`: not trivially copyable, so `state = {}` put the whole state on the stack (6,192 bytes for the overdrive chain at every optimisation level) and `memset` was a warning that is an error. A state outside static storage was garbage with nothing to say so. And zero is not always rest: SampleRateTap's rational stage resets each channel to its design's row length with one pre-advance pending, its guard band filter and DspTap's decimator to their tap count less one, the asynchronous converter to its configured targets; in the sketch, the gain's `started` flag and `steady`'s signed `surplus` offset were the same thing, hidden in an encoding. | The configuration resets the state: `reset(state&) const`, in place, before the first call. All-zero storage means not yet reset. A stage whose rest state is all zero writes nothing (`stage_traits` supplies zeroing in place); the exceptions write their own. A chain's reset calls each stage's on its own part. A state is a plain aggregate (trivially copyable, standard-layout, no default member initialisers), and the chain's is a recursive aggregate. A missing reset is checked at the `audit` level only, through a `ready` word that is always in the state. A generic harness test resets over garbage and over zeros and compares output bit for bit. Measured on a state shaped like the rational stage's (6,160 bytes): `.bss`, 48 bytes of reset code, an 8-byte frame. | (A) All zero is the rest state, as recommended: `.bss`, 20 bytes of reset code and an 8-byte frame, but every non-zero start needs a flag or an offset encoding in the audio path, and no test can show a stage got it right. (B) Defaults in the state's type: the whole 6,160-byte state moves to `.data`, flash as well as RAM, for one non-zero member (32 bytes of code, 8 of stack), and a default cannot depend on the design. A boundary-level check for a missing reset: it would put a run-time test on the fixed-block entry, which otherwise has none. |
| D50, new (finding 12) | **A chain's scratch was part of its state, so every voice paid for it again.** Of the overdrive chain's 6,184-byte state, 40 bytes were history and 6,144 were buffers that carry nothing between calls. Sixteen voices cost 98,944 bytes where 6,784 would do. | State and workspace are apart: `process(state&, workspace&, in, out)`, one workspace per thread, `memory_of<Chain>` for a single instance. A stage declares a `workspace` as it declares a `state` (`no_workspace` for most), so a nested chain or a `steady<>` passes its own down. Measured: a voice 48 bytes, the workspace 6,152, sixteen voices 6,920. | Leaving it and relying on in-place sharing: that removes scratch only from all-1:1 chains. A workspace known only to the chain, not to stages: a nested chain and every combinator would then need a second form of `run`. |
| D43, amended (findings 4, 8, 17, 42) | **`parallel<A, B>` had one sentence of contract.** Its aligning delay is memory sized by the type, but a latency was a stored number that `set_design` changes (2.176, 11.239 and 225.078 frames at three cutoffs of one low-pass type), need not be whole (a bare four-times oversampler is k + 1/2), and may not be exact at all. The tidy had also dropped "ratios must match". | `parallel` accepts only arms that are frame-synchronous, of equal ratio, each with an exact latency that is a whole number of frames and a constant of its type, all asserted. So `exact_latency` is a constant of the type (D39). An arm with a designed recursive filter is refused and its delay is placed by hand. | A `MaxAlign` template argument with a delay re-read when a design changes: it needs a rule for the click and for a fractional remainder, and nothing yet asks for it. It is deferred, not ruled out. |
| D52, new (findings 9, 23) | **The exact pull took its input from the junction a chain block at a time, and the asynchronous converter cannot be driven that way.** Its servo runs once per pull and raises its fill target from the largest pull it has seen. SampleRateTap's real converter, measured twice independently: a 256-frame callback pulled whole locks with no underrun; pulled as four 64-frame pieces it underruns on every callback (3,750 in 20 s) and stays filling; at 128 frames in two pieces it never locks. | `fed_by` makes one pull on its junction per request, for every junction. The largest request is a template argument, and the staging is sized from it by the chain's ratio and shortfall. A request above it is a boundary failure. CF's `async` skeleton carries the real servo and must lock through `fed_by` at a request larger than the block. In the sketch a 700-frame request from a chain by 4 is one pull of 175 frames. | A two-step pull (declare the request once, then take frames in pieces): no staging, but a new servo interface and a new design for `async`. Deferred. Telling the junction the consumer's period as a design number: the reviewers showed the converter still does not lock when sub-pulled. One pull per request only for independent clocks: `handoff`'s own fill policy also counts pulls, and one rule is simpler than two. |
| D51, new (finding 10) | **`handoff` had no fill policy, so its promised nominal latency had no value.** A bare ring's delay was set by which thread ran first (0 or 64 frames) and grew for good with every underrun. | A target fill as a template argument: silence until it is first reached, filling again after an underrun, the newest frames dropped on an overrun; a consumer-side `restart` in the junction contract beside the at-rest `reset`. In the sketch the fill a pull sees is 128 frames whichever side runs first, and is back at 128 after the producer misses three periods (one underrun). Counted in frames the delay is 64 or 128: the one block by which the leading side is ahead, which is the range the contract states. | A run-time target: it can be wrong, so it would need a design; nothing needs it yet. |
| D19, amended (finding 24) | **A junction with configuration and history in one object lands in `.data`,** as a stage did before D34 (9,104 bytes measured for a ring with one configured number). | A junction is a configuration and a state. `handoff`'s configuration is its template arguments, so its object is empty; `async`'s will be its design. | A junction configured only by its type: `async` needs designs and servo constants that are numbers. |
| D38, amended (finding 16) | **D38 deferred block floating because an exponent would have to travel with each frame. That is true of the forward transform only.** With a fixed-scaling forward and a block-floating inverse, `istft` uses the inverse's exponent itself. Measured on DspTap's engine, white noise: -154.5, -152.0, -148.7, -142.8 dBFS at N = 256, 512, 1024, 4096, against -122.3, -116.7, -110.2, -98.2 with fixed scaling both ways. | The Q31 `istft` runs the own engine's block-floating inverse; its floor is an absolute number per length. Only a block-floating forward stays deferred. CMSIS-DSP has no such inverse, so on that back end the floor is the higher one and the contract number is per back end. | Fixed scaling both ways with the loss stated: honest, and about 16-bit grade at N = 4096 for a 32-bit path. |
| D9, amended (findings 14, 44) | **`flush` was one call into one buffer for the whole tail,** and a recursive stage's exact tail is 10^4 to 10^6 frames at a low cutoff (an order-8 Butterworth: 14,736 at 0.01 of the sample rate, 149,085 at 0.001). | `flush` is resumable: a call writes at most the buffer it is given and returns the count; a query on the state says what remains. A design also stores a practical tail (to -120 dB re full scale), which is what `flush` feeds unless asked for the exact one. | Dropping `flush` for "feed this many zero frames through `process`": it leaves the caller to walk the stages. |
| D36, amended (findings 22, 37, 43, 45, 51) | **The link-stage gate, as worded, rejected the plan's own example,** where a control path calls `make()`; it was in no milestone; and the asynchronous converter's servo is `double` with `exp` on every pull. Separately, float accumulation in strict tap order is the noisiest order: at 773 taps -131.4 dBFS, against -142.7 in four lanes and -151.6 from both ends toward the largest tap. | The gate runs on named probe images whose designs are all tables; an image that links a designer is outside it. The sketch's image is 3,084 bytes of text with no software-double symbol, and 11,400 with 21 once `make()` is reachable: the designer costs 8,316 bytes. The summation order is part of the numeric design and is chosen before a floor is recorded. The servo is new numeric work at C8. | An exemption for control-rate code named symbol by symbol: the gate would stop being a yes or a no. |

## Where each finding went

Numbers are the audit file's. "Plan" means `PLAN.md` at the commit that adds this file.

| Subject | Findings | Where |
|---|---|---|
| Input groups and the frame-synchronous flag | 1, 5, 15 | D35; plan 3.2, 3.4, 4, 9.5; sketch `composed_group`, the fixed-block asserts; `must_not_compile.cpp` cases 2 and 3 |
| `steady<>`'s priming | 7, 18 | D35; plan 3.2, 4, 9.5; sketch `composed_shortfall`, `steady`; the demo's four pairs of block stages |
| Reset and the shape of a state | 2, 6, 13, 20 | D49; plan 3.2, 3.4, 4, 8, C0b, C1; sketch `reset_state`, `pack`, `chain::reset`, `gain::reset`, `steady::reset`; the demo's reset test; `must_not_compile.cpp` case 7 |
| The workspace | 12 | D50; plan 3.2, 4, 9.4, 9.7; sketch `workspace_of`, `memory_of` |
| `parallel` | 4, 8, 17, 42 | D43, D39; plan 3.2, 4, C1. Not in the sketch. |
| One pull per request | 9, 23 | D52; plan 6.1, 9.6, CF; sketch `fed_by`, `fed_by_workspace` |
| `handoff`'s fill policy; the junction contract | 10, 24, 36, 52 | D51, D19; plan 6.1 (the read order of `report`, `restart`, a report type per junction, `latency()` from any thread); sketch `handoff` |
| Flush | 14, 44 | D9; plan 3.2, 4, C1. Not in the sketch. |
| Fixed-point resynthesis | 16 | D38; plan 5.2, 7.2, C4, C6 |
| The link-stage gate, the servo, summation order | 22 (part), 37, 43, 45, 51 | D36; plan 7.1, 7.3, 12, C0a, C1, C7, C8; the sketch's `measure.sh` links both images |
| The mailbox | 11, 19, 34, 35 | Plan 6.1 (cores), 6.3, 7.3, 8; D44. Sketch: placement new, a 32-bit atomic of its own, compares in place of division, lock-free asserts, `take` by value, `reset`. Finding 35's ARMv6-M and Hexagon points: ARMv6-M is stated as out of scope; the Hexagon hazard was unverified by both reviewers and is answered by giving the atomic a word of its own. Finding 11's in-place `post_with` was not taken: nothing yet posts a value large enough to need it. |
| Rate-changing stages take a design type | 25, 41 | D37; plan 3.3, 9.4, C3, CF. Finding 41's second half (reset values) is D49 and plan 12. |
| CF's place and its quality bar | 26 | Plan CF row, 10.1 |
| What the tidy lost | 3, 28, 36 | D47, D24 (Q4.27, the stages outside the two lists, "not its arithmetic", `basic_<name>`); plan 3.1 (a stage and a format it does not pin), 3.2 (`run`'s spans), 4 (`planar_section`, the fractional delay beyond two times), 6.1 (`starved`), 7.3 (the `k_` rule switched off), 12 (written new; the FFT's conventions). Finding 28's item on statuses is the section below. |
| The layout numbers | 29 | Plan 5.1; `layout-spike-tables.md` |
| Milestones, the graph, the documentation plan, names | 22 (part), 30 | Plan 10 (every gate and script in a row; fixtures; `sanitize`; a dynamics stage), 10.1 (new edges and nodes), 11.4, 7.1; `tap<Ch, Analyzer>`, `handoff<S, Ch, Capacity, Target>`, `steady{...}`, `delay<S, Ch, MaxFrames>` settled |
| The sketch's joint check | 31, 46 | Sketch: the chain derives from its joints; `must_not_compile.cpp` case 1. Plan 3.2. |
| What the sketch covers; stale and leaf-only stack figures | 27, 32, 40 | The sketch's README (a coverage table and what is absent); plan preamble and 9; `stack_path.py` gives each call's static worst case down its path; every quoted number is printed by `measure.sh` |
| `\|`, `expects`, `expected`, `from_table` | 33, 48 | Sketch: four overloads that always flatten, a message for a non-stage operand, a `constexpr` check, `expected`'s triviality asserted, `from_table` checked (`must_not_compile.cpp` cases 4 and 5). Plan 3.3, 4. |
| A configuration's size and constant initialisation | 39 | Plan 3.3, C0b; sketch `\|` by reference and one construction |
| What `make()` costs | 38 | Plan 3.3, 7.1, 7.3, C3 |
| One level per image | 47 | D27; plan 3.4, 7.4, 8; the sketch's handler is inside the namespace |
| Section 8's lists and the card | 49 | Plan 8, 11.2. The guideline names in the finding were from memory, as section 8's are; nothing is quoted. |
| D42 and junctions | 50 | D42; plan 6.1 |
| The C ABI | 21, 53 | D45; plan 7.5, CF, C6, 10.1 |

Parts of findings that the second reviewer called overstated, and that were not acted on
for that reason: 3 (that the timing of `convert`, `unpack` and `pack` had changed: only the
reason had); 8 (that constructing a `parallel` could fail); 11 (that a table could never be
replaced safely: a second mailbox is the acknowledgement); 13 (that a state could not be
reset at all); 21 (most of its list is detail for CF); 24 (that the signatures must
otherwise change); 26 (that CF cannot find such things); 35 (ARMv6-M as a target); 53
(parity with the handle-based C ABIs as a goal).

## Choices made while folding it in

These were not in the audit's recommendations as written. Each is small; each is a place
where the plan now says something a reader might take for the audit's.

- **`max_shortfall` is a ratio**, not a whole number of frames: a decimator by 2 trails by
  half an output frame, and rounding per stage would have made down-then-up's `steady<>`
  cost two frames where one is exact.
- **The chain's shortfall is the smaller of two bounds.** The sum alone gave 190 frames for
  a 128-frame block before a 64-frame one, where the old rule's 127 was right; adding "one
  frame short of an input group" restores it.
- **Every stage declares a `workspace`**, as it declares a `state`. The recommendation spoke
  of the chain's workspace only; a nested chain and `steady<>` need to pass theirs down.
- **One pull per request for every junction**, where the recommendation named
  independent-clock junctions.
- **`stage_traits` detects `reset`.** D10 says nothing probes for a member; supplying a
  default to a stage that writes none needs one detection, and D10 now names it as the one
  exception.
- **A configuration's ceiling is 256 bytes.** The finding asked for "a small stated
  ceiling"; an order-16 Butterworth design by value is about 170 bytes.
- **A practical tail** (to -120 dB re full scale) beside the exact one.
- **A mailbox is not reset before use.** It is an object with a constant constructor that
  starts empty; `reset()` empties it again at rest. A junction's state, like a stage's, is
  plain storage that its configuration resets.
- **`sanitize` at C2 and one dynamics stage at C3**, since the plan named both and placed
  neither.

## Statuses

The tidied plan says every decision is the maintainer's and settled. Two points in D46 were
recorded before the tidy as the draft's own reading of an accepted recommendation rather
than as the maintainer's words: that there is no print edition, and that notebooks arrive
with the C ABI. They stand as decided unless the maintainer says otherwise.

## What is still shown only on paper

The sketch does not contain `parallel`, `exact_latency`, `flush`, `oversample`, `feeding`,
in-place buffer sharing, `with_block`, analyzers, the traits headers, or any port but
interleaved `float`. The contracts for those are in the plan and are first compiled at the
milestone that delivers them (C0b for the traits, C1 for the rest but `oversample`, C3).
