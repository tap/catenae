# Audit of PLAN.md after the tidy

2026-11-25. The plan at commit `9f7eabb` and the compiled sketch under `docs/sketches/config-state/`
were read by seven independent reviewers, each through one lens and each required to verify a
claim before reporting it (by compiling, by running the sketch or the source libraries, by
measuring on a Cortex-M33 build). Two further reviewers then tried to refute every finding.
This file is the record: every finding as reported, with the verdict it received. It is not
edited for length; `PLAN.md` is where the consequences are drawn. The first audit is
`2026-11-24-plan-draft-19.md`. Paths under `/tmp/` are the reviewers' scratch files and do not
survive the session; the commands and outputs quoted with them are the record.

## Result

53 findings. No blockers. None refuted.

| Verdict | Major | Minor |
|---|---:|---:|
| confirmed | 17 | 24 |
| partly | 0 | 12 |

"Partly" means the second reviewer found a real point that the finding overstated or misstated;
its note says what is true. The severity shown is the second reviewer's. Ten findings reported
as major were rated minor after verification, and one reported as minor (42) was rated major.

## The major findings are seven subjects

Several reviewers reached the same defect from different lenses. The 17 major findings are:

| Subject | Findings |
|---|---|
| `is_frame_synchronous` is declared per stage and composed by AND, around an "input group" that no member publishes; a down-then-up chain passes it and returns uneven counts | [1](#f1), [5](#f5), [15](#f15) |
| The stage contract lost `reset()`; a chain state built on `std::tuple` is not trivial, so zeroing it costs a state-sized stack frame or a warning that is an error, and a state outside static storage is not zero | [2](#f2), [6](#f6), [13](#f13), [20](#f20) |
| `parallel<A, B>` must size its aligning delay from the type, but latency is a stored value a setter can change and need not be whole | [4](#f4), [8](#f8), [17](#f17), [42](#f42) |
| `steady<>` primes from the largest burst, which is not the worst backlog for two block stages whose sizes do not nest: silent wrong audio | [7](#f7), [18](#f18) |
| `fed_by` pulls a junction one chain block at a time; the asynchronous converter, driven that way, never locks and underruns on every callback above the block | [9](#f9), [23](#f23) |
| `handoff` has no fill policy, so its latency has no nominal value | [10](#f10) |
| A chain's scratch is part of its state, so every voice pays for it again: 93 to 99 % of `state_of<Chain>` carries nothing between calls | [12](#f12) |

## Reviewers

| Lens | Findings |
|---|---:|
| Nothing lost, nothing contradicted by the tidy | 8 |
| The sketch as evidence: C++17 correctness, and does it match the plan | 7 |
| Mailbox, handoff and the junction contract under the C++ memory model | 7 |
| Configuration and state on real targets | 8 |
| DSP: the accepted decisions measured | 7 |
| Build, safety posture, documentation, milestones after the tidy | 8 |
| Do SampleRateTap's three engines fit the contracts as they now are | 8 |

## Index

| # | Severity | Verdict | Lens | Where | Finding |
|---:|---|---|---|---|---|
| [1](#f1) | major | confirmed | tidy | Section 3.2 (is_frame_synchronous row), section 3.4 (Fixed blocks), section 4 ("is_frame_synchronous compose"), section 9.5, D35 | The tidy redefined is_frame_synchronous around an undefined "input group"; a chain that passes it still returns uneven counts through the fixed-block entry |
| [2](#f2) | major | confirmed | tidy | Section 3.2 (state row), section 3.3 (the audio-path list), section 6.2, section 8 (assumptions of use), C0b | reset() was dropped from the stage contract; "zeroing its state" costs a state-sized stack frame and the sketch's state cannot be memset |
| [3](#f3) | minor | partly | tidy | D47, D29, D24, section 10 (C1, C2, C6 rows), section 12, section 9.1 and 9.2 | D47 and the C6 row no longer cover Q4.27 or the stages outside D47's two lists, and D47 was restated differently from what was accepted |
| [4](#f4) | major | confirmed | tidy | D43, D39, D34, D22, section 3.2 (latency row), section 4, C1 | Latency is a value of the configuration that a setter changes, but parallel<> must align latencies with storage sized at compile time; D43 also lost its ratio rule |
| [5](#f5) | major | confirmed | sketch | PLAN.md D35; section 3.2 (is_frame_synchronous row); section 3.4 'Fixed blocks'; section 4 ('rate_ratio and is_frame_synchronous compose'); sketch.h:375, 509, 562-571 | is_frame_synchronous composed by && is unsound: a decimate-then-interpolate chain is labelled frame-synchronous, passes the fixed-block entry, and traps at run time on a host block that is not a multiple of its hidden input group |
| [6](#f6) | major | confirmed | sketch | PLAN.md section 3.2 (state row: 'Resetting a stage is zeroing its state'), D34, D22, section 9.7 ('a caller's arena'), D45; sketch.h:513-516 (chain::state holds std::tuple), 807-818 (fed_by_memory) | 'Resetting a stage is zeroing its state' has no clean mechanism: st = {} costs the whole state in stack on the M33, memset is a -Wall error, and a state not in static storage is not zero |
| [7](#f7) | major | confirmed | sketch | PLAN.md D35; section 9.5 ('The FIFO starts with one frame fewer than the largest burst'); section 4 (steady<Config>); sketch.h:651-652, 681-690 | steady<> primes its FIFO from max_outputs_for(1), which is not the worst shortfall of a chain of two block stages: the FIFO underruns silently and returns wrong audio |
| [8](#f8) | major | confirmed | sketch | PLAN.md D43, D39, D9, D22; section 3.2 (latency(), tail_frames() rows); section 4 (flush, flush_output_frames, parallel); milestone C1 | Latency and tail are values of a configuration (D34, D37) but parallel<> and flush need them to size memory fixed by the type (D22); the sketch has neither latency() nor flush nor parallel, so the conflict is not visible |
| [9](#f9) | major | confirmed | concurrency | Section 6.1 (pull, fed_by, fed_by_state "the frames in transit"), D19, D35, D16; sketch.h line 836; milestones C0b (junction_traits), C1 (fed_by), CF, C8 | fed_by pulls from a junction in 64-frame pieces, and SampleRateTap's async converter then underruns on every callback of 256 frames or more |
| [10](#f10) | major | confirmed | concurrency | Section 6.1 (latency() {nominal, range}; report() "state"; pull "always fills out"; reset(); "Latency across a junction composes as nominal only"), D19; sketch.h lines 709-744; C0b, C1 | handoff has no fill policy, so its latency has no nominal value: the delay is set by which thread ran first and grows by every underrun |
| [11](#f11) | minor | partly | concurrency | Section 6.3 (T "trivially copyable (asserted): a design, a level, a reading"), D44; section 6.2 (result_type "a value, or a view of storage in the state"; "leave it through a mailbox"); section 3.3 and D37 (table designs in the caller's storage); section 9.7 ("holds a view") | A mailbox carries bytes, not ownership: a posted view races, and nothing tells the poster when the audio thread has let go of the old table |
| [12](#f12) | major | confirmed | embedded | D34 ("state_of<Chain> is the memory", "One configuration serves many states"), D42 ("runs one mono configuration over as many states as there are channels"), section 4 ("Its state holds its stages' states, the scratch between them and a zero block for flushing"), section 9.4 (two voices), 9.7 | A chain's scratch is part of its state, so every voice and every channel-state pays for it again: 93 to 99 % of state_of<Chain> is scratch that carries nothing between calls |
| [13](#f13) | major | confirmed | embedded | Section 3.2 (`state` row: "Resetting a stage is zeroing its state"), D34 ("valid when all zero"), D45 (C functions over storage the caller supplies), D31, sketch.h chain::state (lines 513-516) | "Resetting a stage is zeroing its state" has no way to be done: the sketch's chain state is not trivial, `st = {}` needs a stack frame the size of the state at every optimisation level, and memset is a -Werror |
| [14](#f14) | minor | confirmed | embedded | Section 4 (`flush(state&, out)`, `flush_output_frames()`, the zero block in the state), section 3.2 (`tail_frames()`), section 3.4 (boundary check on the caller's entry), D9, C1 (`flush` is a deliverable); first-audit findings 7 and 12 item 6 | flush(state&, out) is one call into one buffer sized by a run-time tail: 60 KB of output for one order-4 Butterworth at 480 Hz, and a trap in the boundary check if the buffer is a callback block |
| [15](#f15) | major | confirmed | dsp | D35; section 3.2 (is_frame_synchronous row); section 3.4 (Fixed blocks); section 4 ("rate_ratio and is_frame_synchronous compose"); section 9.5 last sentence; sketch.h lines 375, 422, 509, 565-568 | is_frame_synchronous, as defined and as composed, passes a down-then-up chain (the audit's own 66/63/63 case) and cannot tell an stft from a decimator |
| [16](#f16) | minor | confirmed | dsp | D38; section 5.2 (fixed-point bullet, "a Q31 one states its loss", "overall gain (1)"); section 7.2 (own fixed-point engine: "fixed scaling only"; back ends "within a stated tolerance"); C6 | D38 rests on "block floating needs an exponent to travel"; a block-floating inverse does not, and it is 32 to 45 dB better than the floor the plan is about to state |
| [17](#f17) | major | confirmed | dsp | D43; section 4 (combinators); section 3.2 (latency(), has_exact_latency); D37 (a design stores its latency); D34 (a parameter is configuration); D22; C1; section 11.3 ("a dry path that must line up with a wet one") | parallel<A, B> (D43, due at C1) has no rule for the latencies it cannot align: inexact, fractional, changed by a setter, or bursting |
| [18](#f18) | major | confirmed | dsp | D35; section 9.5 ("The FIFO starts with one frame fewer than the largest burst: that is the added latency"); sketch.h lines 651-652, 659, 681-690 | steady<>'s prefill of (largest burst - 1) is not the worst backlog: two block stages whose sizes do not nest corrupt the output silently |
| [19](#f19) | minor | confirmed | build | D44, D37, section 6.3 (`T`: trivially copyable (asserted): a design, a level, a reading), section 9.3, section 8 (no undefined behaviour relied on; the deviation list); sketch.h lines 763 and 793-796 | The mailbox's post is undefined behaviour for exactly the types the plan posts through it (a design, a stage's configuration); no sanitizer sees it and section 8's deviation list has no entry for the fix |
| [20](#f20) | major | confirmed | build | D34, section 3.2 (`state` row: 'Resetting a stage is zeroing its state'), section 4, section 6.3/D44 ('a new configuration reaches the audio thread this way'), section 8 (assumptions of use), section 9.7; sketch.h chain::state and std::tuple | A state is 'valid when all zero' but nothing says how it gets or returns to zero: reset() was dropped in the tidy, the natural spellings cost a state-sized stack frame or a -Wall error, and a state outside static storage is silently garbage |
| [21](#f21) | minor | partly | build | D45, D46, section 3.4 (one level per image), section 7.4, section 10 (CF, C6 and the graph), section 11.1 (notebooks) | D45's C ABI needs things no build system or milestone lists: per-target sizes without running target code, an init for the configuration, a shared library, a size check the inline namespace cannot give, and a manifest that can name designs |
| [22](#f22) | minor | confirmed | build | Section 10 (C0a, C1, C2, C6 rows; graph and workstream table), D36 and section 7.3 (double promotion), D47, D29, section 2 (charter), section 3.4 (`sanitize`) | Three gates settled in sections 1 and 7 land in no milestone, one of them rejects the plan's own example as worded, and C1's stages cannot exercise half of what C1 delivers |
| [23](#f23) | major | confirmed | fit | Section 6.1 (fed_by, fed_by_state 'frames in transit', pull(out)); D19, D35, D16; sketch.h fed_by (line 836); CF | fed_by pulls a junction in sub-blocks; the real async converter never locks that way, and underruns on every callback above the block size |
| [24](#f24) | minor | partly | fit | D34, D19, D37, D44; section 6.1 table (push(in), pull(out), report(), reset()); feeding / fed_by signatures; 9.6; sketch.h handoff | The junction contract has no configuration/state split, so a configured junction (async) lands in .data, or the C1 signatures change |
| [25](#f25) | minor | confirmed | fit | D37 and section 3.3 ('Where a profile fixes a tap count that sizes storage, the profile is a template argument'); 9.4 (`up2<2>{} \| up2<2>{} \| ...`, 'latency 18.5 and 23.5'); C3 (up2/down2, the L/M machine, oversample); C7 | D37 on rational: the design divisor sizes storage too and belongs to the chain, so up2<2> \| up2<2> cannot be SampleRateTap's chain; custom and searched designs have no form |
| [26](#f26) | minor | partly | fit | Section 10 (CF row), 10.1 ('CF follows C1 at once and runs beside the stage work'; workstream table), C1, C3; first-audit finding 34 | CF runs beside the work it protects and has no quality bar, so it cannot find these; the three contract changes it would force are known now |
| [27](#f27) | minor | confirmed | tidy | Preamble table ("The contracts of sections 3 to 6 as code that compiles and runs"), section 9 opening sentence, section 9.2 (the 20-byte stack frame), section 9.3 | The new plan claims more for the sketch than it contains, and one quoted measurement no longer reproduces |
| [28](#f28) | minor | partly | tidy | Sections 3.2, 5.1, 5.2, 6.1, 7.2, 7.3, 9.6, 12; D36, D37, D39; the preamble's "settled" sentence | Contract details and porting facts that were in draft 32 and are now in neither PLAN.md nor the response file |
| [29](#f29) | minor | confirmed | tidy | Section 5.1 (second paragraph), D5 | The layout numbers the tidy added to section 5.1 misquote the spike's tables |
| [30](#f30) | minor | confirmed | tidy | Section 10 (C0a, C0b, C1, C6), section 10.1 (graph and table), section 11.4, sections 6.2, 7.1, 7.2, 9.5 to 9.7 | Milestones, the parallel-work graph and the documentation plan disagree in small ways after the rewrite |
| [31](#f31) | minor | confirmed | sketch | PLAN.md D27, section 3.4 (first row), section 9.5 ('What does not compile'); sketch.h:481-491 | The sketch's joint proof is never instantiated: a 2-channel stage joined to a 1-channel stage compiles on all three compilers and overruns the caller's buffer |
| [32](#f32) | minor | confirmed | sketch | PLAN.md opening table (line 16); section 9 opening sentence; sections 3.2, 3.3, 3.4, 4, 6.1, 6.2; sketch README | 'The contracts of sections 3 to 6 as code that compiles and runs' overstates the sketch: about half the named contract members are absent, and three of the absences hide costs |
| [33](#f33) | minor | confirmed | sketch | PLAN.md section 4 ('it flattens, so the type lists the stages once'; stage<I>() by position); section 3.3 (expected; 'A design is a class'); sketch.h:26-30, 215-241, 272, 630-637 | operator\| flattens only its left operand, and four smaller defects in the sketch's vocabulary types |
| [34](#f34) | minor | confirmed | concurrency | Section 6.3, D44, D37 ("a design is a class, not an open aggregate"), section 8 ("no undefined behaviour relied on"); sketch.h lines 763 and 793-796 | mailbox::post is formally undefined behaviour in C++17 for every design type: assignment cannot start the lifetime of a union member that has no trivial default constructor |
| [35](#f35) | minor | partly | concurrency | Section 6.3 ("three slots and one atomic byte"; "an interrupt handler counts as a thread"), section 6.1 report() counters, section 7.3 (sanitizers), C0a ("the Cortex-M and Hexagon QEMU legs"); sketch.h lines 764, 777, 798-801 | The mailbox's atomic byte does not link on Cortex-M0/M0+, and on Hexagon it is a locked update of the whole word that also holds each side's private index |
| [36](#f36) | minor | confirmed | concurrency | Section 6.1, report() row ("the fill is clamped to the capacity"); first-audit finding 22 | The tidy dropped report()'s read order: a clamp alone reports a nearly empty ring as full |
| [37](#f37) | minor | confirmed | concurrency | D36 and section 7.3 (the link-stage gate), section 7.1 ("the asynchronous converter's servo" as compiled per storage type), section 12 ("not mechanical in five places"), C8, CF | The async junction's pull calls exp and about fifty software-double routines on a Cortex-M33, which D36's link-stage gate rejects; section 12 does not list the servo as new numeric work |
| [38](#f38) | minor | confirmed | embedded | D37 (a design "stores its latency and tail as numbers"), section 7.1 table row 3 ("the tail computation" compiled, parameterized on nothing), section 9.2 and 9.3 (make() called on a control path), section 7.3 (`-Wstack-usage=` at each target's stack ceiling); first-audit finding 12 item 5, not carried through the tidy | What make() costs is gone from the plan: the carried tail computation needs a 1.4 to 20.6 KB stack frame and cannot be "compiled, parameterized on nothing" without a workspace or the worst-case frame |
| [39](#f39) | minor | confirmed | embedded | D34 ("a small constant value"), D37 (small designs by value, table designs viewed), section 4 (`a \| b \| c`), section 9.2 ("a configuration whose parameters will change is a variable"), section 9.7 pattern 2, section 6.3 (mailbox T) | Nothing bounds a configuration's size or keeps it constant-initialised, and the by-value chain makes both mistakes expensive and silent |
| [40](#f40) | minor | confirmed | embedded | Section 9.2 ("its callback's stack frame 20 bytes"), section 9.4 table ("Stack of the callback 64 bytes"), docs/sketches/config-state/README.md (20 and 64), measure.sh | The stack figures quoted for the audio callbacks are leaf frames, one is stale, and the real depth is 1.5 to 5 times larger |
| [41](#f41) | minor | partly | embedded | Section 3.3 ("Where a profile fixes a tap count that sizes storage, the profile is a template argument"), D22, D34, section 9.4 (`up2<2>{}`), C3 ("`up2` / `down2` and the L/M stage machine with one pinned design"), C7, CF, section 12 | "The profile is a template argument" understates what sizes a rate-changing stage's state, and two carried reset values are not zero |
| [42](#f42) | major | confirmed | embedded | D43 ("with their latencies aligned"), D39 and section 3.2 (`latency()` "stored numbers from the design"; `exact_latency()`), D22 and section 9.7 pattern 1, section 9.2 (set_design), C1 (`parallel` is a deliverable) | parallel<A, B> must size its alignment delay at compile time, but a stage's latency is a stored run-time number that set_design can change and that need not be whole |
| [43](#f43) | minor | confirmed | dsp | D36; section 12 ("The float accumulator: a new numeric design with new floors"; fir_kernels.h carried); section 4 (bit-identical at contraction off); section 7.1 (FIR dot kernels are header templates); C7 ("to a tolerance") | D36 measured: float accumulation costs 8 to 26 dB, no source stage falls below its profile except async's 140 dB tier, but summation order (up to 20 dB) and contraction are undecided and the tolerance cannot be in ulps |
| [44](#f44) | minor | confirmed | dsp | Section 4 (flush); section 3.2 (tail_frames, flush_residue); D9; D22; section 11.2 card ("Tail ... fc >= 0.002"); C1, CF, C3 | flush() is one call for the whole tail, and the carried recursive tail is 10^4 to 10^6 frames; flush_output_frames() needs the state |
| [45](#f45) | minor | confirmed | dsp | D36; section 7.3 (Double promotion bullet); section 9.2 and 9.3; section 7.1 (designers are compiled double arithmetic) | D36's link-stage gate, as worded, rejects the plan's own run-time design example |
| [46](#f46) | minor | confirmed | build | D6, D27, section 3.2 (`joint<Index, OutputPort, InputPort>`), section 9 ('all of this is in the sketch'), the header table ('the contracts of sections 3 to 6 as code that compiles and runs'), C0b and C1; sketch.h lines 480-491 | The sketch's joint proof never fires: a mono output feeding a stereo input compiles on three compilers and runs |
| [47](#f47) | minor | confirmed | build | D27, section 3.4 (one level per image; a failed check), section 7.1 (the numeric contract is the library's), section 7.4 (amalgamation), C0b's link-fail test; sketch.h line 22 | 'An image cannot mix levels' holds only if the handler is inside the inline namespace, and not at all for a header-only file with no live check; the amalgamation cannot carry the contraction policy |
| [48](#f48) | minor | confirmed | build | D37, section 3.3 (a design is where failure lives; two kinds), section 9.2 (`from_table`), the response file's D37 row ('only make() and the table generator can produce one') | A public from_table() lets unvalidated numbers become a design, so 'a stage is built from a design and cannot fail' still has the hole the first audit's finding 5 reported |
| [49](#f49) | minor | partly | build | Section 8 (deviation log, assumptions-of-use register), section 11.2 (the card), section 11.3 and 11.4 (chapters against milestones), sections 9.2 to 9.4 (static storage idiom) | Section 8's lists and section 11's card were not updated for D34, D37, D44 and D45 (rule names recalled from memory) |
| [50](#f50) | minor | partly | fit | D42; section 2 ('Run-time channel counts' out); section 6.1; D45 | D42's fallback (one mono configuration over as many states as channels) is unsound for every junction |
| [51](#f51) | minor | confirmed | fit | D36; section 7.3 (link-stage gate); section 7.1 table row 2 ('the asynchronous converter's servo ... explicitly instantiated for the four'); C8 | The async servo is double arithmetic with exp() on every pull: D36's link gate rejects the float async junction, and 7.1 misdescribes the servo |
| [52](#f52) | minor | partly | fit | Section 6.1 (reset(), report(), latency()); first-audit finding 34 (second reviewer); CF | The junction contract still has no place for async's consumer-side reset, its resync count, or a latency that changes at run time |
| [53](#f53) | minor | partly | fit | D45; C6 ('The C ABI from its manifest, and the executed notebooks'); CF ('a first C ABI manifest'); section 11.1 (notebooks) | D45's manifest: workable for the three C ABIs, but its size and what a named instantiation fixes are not stated |

## What was checked and held

### Nothing lost, nothing contradicted by the tidy

- Every MAJOR finding of the first audit is still answered somewhere in the new PLAN.md: 1 and 26 (D36, the link-stage gate in 7.3, section 12), 5 and 13 (D37, 3.3), 7 (flush in section 4, analyzers in 6.2, D45), 8, 10 and 17 (D34, C1's pinned .data/.bss/start-up stack), 11 and 35 (table designs and profile-as-template-argument in 3.3), 15 and 28 (inline namespace and C0b's link-fail test in 3.4), 22 (report(), the thread-sanitizer and arm64 legs), 23, 33 and 37 (D35, steady, the holdover), 25 (D38), 27 (D39, oversample), 29 (C0a/C0b split), 34 (CF, D16). The one partial exception is finding 3 (Q4.27 and the new fixed-point helpers), reported above.
- The decision table is complete and its references resolve: a script over PLAN.md finds D1 to D48 each defined exactly once, no decision cited that is not defined, and every 'section N' or '9.x' reference pointing at a heading that exists and holds the subject named (3.4 checks, 5.2 spectrum, 6.3 mailboxes, 7.2 back ends, 7.3 warnings and shifts, 9.2 redesign, 9.5 the figure and the gate).
- What the tidy says it moved to the response file is there and faithful: the six rows D34 to D39 are byte-identical to draft 32's (cmp on the extracted rows), and the findings map and 'what moved in the source libraries' are carried whole.
- D48 matches the accepted recommendation of old section 13 question 2 (floating engine at C4, fixed-point engine at C6, fft_plan<S>, fft_tables<S, N>, the four options and the spectrum format fixed at C4, block floating not carried), and it is consistent across D48, 7.2's table and bullets, the C4 and C6 rows, the graph's F and X nodes and section 12.
- Section 12's carried-file list and versions hold on origin/main: every DspTap header named exists there (including fft/srdif.h, fft/fft_arith.h, fft/fixed_point.h, detail/cx_math.h, which uses std::bit_cast); fft_fir.h is absent from main and present only on the spike branch; version.h says 0.7.0; SampleRateTap's project() says 0.9.0 and has async/.../spsc_ring.h, rational/.../stage.h, scripts/icount.py and scripts/ceilings.py. The QEMU plugin's page-boundary fix, which the tidy stopped mentioning, is on DspTap's main (begin_entry in tools/qemu_insn_plugin/insn_count.c), so nothing is lost by carrying the plugin from main.
- The other quoted sketch numbers reproduce with arm-none-eabi-g++ 13.2.1 via measure.sh: strip configuration 32 bytes of .data, strip state 548 of .bss, overdrive state 6,184 of .bss, mailboxes 76 and 16 bytes, overdrive callback 64 bytes of stack, the audio file calling only tanhf and the designer's file tan plus seven __aeabi_d* routines. The host demo prints 0 128 0 128 for the bare region, 64 64 64 64 with the impulse at frame 127 for steady, and 'need 26 in, which make 104; delivered 102 and holds 2' for the pull, as 9.5 and 9.6 say.
- Changes of wording that look like changes of meaning but are not: D27's audit level is now 'an explicit build option' where the old row said 'debug builds only' (the old 3.3 already made it an option independent of build type); D16's 'stable' tied to CF matches the old 10.1; D25 frozen at C4 matches the first audit's recommendation; the stage and analyzer tables were correctly brought into the state-passing form (run(state&, ...), outputs_for(const state&, n)) that the old 3.2 had not caught up with.
- The milestone order has no milestone needing something a later one delivers on the main line: C0b supplies sample_traits and fixed<> for C1's convert; C1 supplies delay for parallel and oversample, and handoff, fed_by and flush for CF's skeletons; C3 supplies the L/M stages for C7's rational; the FFT interface at C4 precedes C5's stft and C6's adapters. The gaps found are unscheduled gates and tools, not inversions.

### The sketch as evidence: C++17 correctness, and does it match the plan

- Warnings: demo.cpp, trace.cpp and mailbox_stress.cpp built with g++-13 and g++-14 at -std=c++17/20/23, -O0 and -O2, -fno-exceptions -fno-rtti and the full GCC list of section 7.3: sketch.h is clean; the only diagnostic is -Wstack-usage on demo's own main() at the 4,096-byte ceiling I chose. arm-none-eabi-g++ 13.2 with the same list at -Os and -O2: only -Wmissing-declarations on the free functions of embedded.cpp and embedded_design.cpp. clang++-18 -Weverything less exactly the exclusions section 7.3 names: only -Wctad-maybe-unsupported (15 instances over the nine builds), which section 4's 'explicit guides' already answers.
- Sanitizers: all three programs run clean under g++-13 -fsanitize=address,undefined at -std=c++17, and mailbox_stress under -fsanitize=thread (2,000,000 posts, 0 torn, 0 out of order). Clang's sanitizer runtime is not installed on this machine, so the Clang half was not run.
- The Cortex-M33 figures quoted in sections 9.2 to 9.4 reproduce with the repository's measure.sh: 6,184 bytes of .bss for the overdrive state, 548 for the strip state, 32 bytes of .data for the strip configuration, 76 and 16 for the two mailboxes, 64 bytes of stack in audio_callback, tanhf the only library call from the audio path, tan and seven __aeabi_d* routines from the designer's file. (The one exception, the strip callback's 20 bytes, is in a finding.)
- fed_by (first-audit findings 33 and 50) is sound: 3,000 calls of random length on seven chains (interpolating, decimating, one and two block stages, mixed) under ASan deliver exactly the frames asked for, bit-equal to the same input run straight through the chain, never pull zero frames or more than k_block, and never hold more than the holdover's capacity (largest held 199 of 200, 255 of 256).
- The stage contract holds on every sketch stage and on chains of them, including a nested chain and steady inside steady: over 2,000 calls of random length each, outputs_for was exact, frames_needed was the smallest n, no call exceeded max_outputs_for, and the output was bit-identical to one call over the whole input (chunking invariance, the gain ramp included).
- gain's ramp: a level changed in the middle of a ramp starts a new ramp from the current level (largest step between frames 0.010417 = 1/96, no jump), lands exactly on the goal (0.25), ramp_frames 0 applies at the next frame, and a re-zeroed state restarts at the level through the started flag.
- The fixed-extent process (first-audit finding 43) is deducible and a std::array passed directly takes the compile-time path: its three static_asserts fire with their own messages for an output that is too small, an input that is not whole frames and a chain that is not frame-synchronous; a 1:4 chain with a 4x output and an oversized output both compile; a wrong sample type is rejected.
- The mailbox's three-slot exchange is correct as written (each side only touches the slot it owns after the exchange; zero is the starting arrangement) and compiles to inline LDAEXB/STLEXB on Cortex-M4, M33 and M55 with no library call. On a core without exclusives (cortex-m0plus) it needs __atomic_exchange_1, which is not one of the plan's targets.

### Mailbox, handoff and the junction contract under the C++ memory model

- mailbox<T>, the algorithm: an exhaustive interleaving model (model_mailbox.py in the scratch directory; post = start write, finish write, exchange; take = load, exchange, start read, finish read) for up to 6 posts and 6 takes (3,681 states) found no violation. The poster's, middle and taker's slots are a permutation of {0, 1, 2} in every state (12 reachable arrangements: 6 permutations by the fresh bit, all-zero being the start). take never reads a slot being written, never delivers a never-written slot, a duplicate or an older value, never returns false after a completed post, and the last post is always collectable. Two mutants (no fresh check; fresh not cleared) are caught, so the model can see these faults.
- take() before any post returns false at the load and never touches a slot, so the union's inactive member is never read. A post landing between take's load and its exchange only replaces a fresh middle with a newer fresh one, because only the taker clears the bit; the model covers every such schedule.
- The memory orders are sufficient under the standard, not just on x86. Each exchange is acq_rel, so the poster's slot write happens-before the taker's read through the exchange that hands the slot over, and the taker's read happens-before the poster's next write of that slot through the exchange that hands it back. With every order weakened to relaxed, GCC's thread sanitizer reports data races in the same stress test, so the sanitizer does judge the orders and not only x86's behaviour.
- mailbox_stress.cpp reproduces: 54 thread-sanitizer runs (g++-13, g++-14, and clang++-18 objects linked against GCC's libtsan because clang's own runtime is not installed; payloads of 8, 64 and 4,096 bytes; 2,000,000 posts each, every word checked) gave 0 torn, 0 out of order, 0 duplicates and 0 sanitizer reports.
- An interrupt handler as one side on a single ARMv7-M or ARMv8-M core is sound for both mailbox and handoff. Pre-emption schedules are a subset of the interleavings the two models enumerate, the interrupt side's exchange completes without a retry, and the interrupted side's store-exclusive fails and retries once per interruption.
- handoff's push and pull are correct for a power-of-two capacity below 2^32. model_handoff.py, with a 3- or 4-bit index so the free-running index wraps, found no lost, repeated, reordered or concurrently accessed frame in 1,668 states per configuration. A push clamps to the room, so the producer cannot lap; the acquire and release orders on the two indices are the standard and sufficient ones. The model shows the same code failing for capacity 3, which the sketch's static_assert forbids.
- What the carried spsc_ring.h has that the sketch's handoff lacks, all of it portable work for C1 and none of it a correctness gap: cached cross-indices, two-segment memcpy transfer, side-owned exact occupancy queries and discard(), 64-byte separation of the two sides, size_t indices with an is_always_lock_free assertion. It is an element ring with a run-time, vector-backed, bit_ceil capacity, so the frame ring with a compile-time capacity is an adaptation the plan already records (first-audit finding 50).
- First-audit finding 22 is otherwise handled. report() is declared new design with individually atomic 32-bit fields (async's status() on origin/main already has exactly that shape, 32-bit atomics and "individually coherent, not mutually"). The thread-sanitizer and arm64 legs are in section 7.3 and C1, and the one-thread-per-side rule is in the assumptions-of-use register. push, pull, report, capacity_frames, tolerance_ppm and an at-rest reset() are each implementable for async as its source stands.

### Configuration and state on real targets

- Every memory figure quoted for the Cortex-M33 reproduces: measure.sh at HEAD (arm-none-eabi-g++ 13.2, -Os) gives s_strip 32 bytes .data, s_strip_state 548 .bss, s_overdrive 6,184 .bss, the two mailboxes 76 and 16 .bss, sizeof(overdrive) 8; embedded.o references only tanhf, embedded_design.o references tan and seven __aeabi_d* routines.
- A constexpr chain configuration never costs start-up code: across cortex-m33, m4 soft-float, m4f and m55 at -O0, -Os, -O2, -Os -flto and without -ffunction-sections there is no .init_array; at -O0 the configuration is .rodata (overdrive 8 bytes, the ROM design 24), from -Os it is folded into the code; `static auto s_strip = ...` is constant-initialised .data at every level.
- Constant-evaluated composition cannot trip -Wstack-usage: at -O0 neither operator| nor the chain constructor is emitted (0 of 149 functions in the .su file of embedded.cpp), so the first audit's findings 8, 10, 17 and 36 (a chain in .data or a 12 KB start-up frame) are handled by D34 for constexpr and constant-initialised chains.
- Section 9.7's pattern 2 (a constant viewed, not copied) works in a constexpr configuration: an FIR stage holding a pointer and length to a constexpr tap array composes with `|` at compile time; with a 164,160-byte table the variable chain is 24 bytes of .data, the table is in .rodata once, and run-time composition has an 8-byte frame.
- D36's link-stage gate is achievable with newlib: fourteen float libm functions (tanhf, powf, expf, logf, log10f, sinf, cosf, sqrtf, atan2f, exp2f, log2f, floorf, fmodf, expm1f), each linked into a bare-metal image, bring in no __aeabi_d* routine on cortex-m33 hard-float or cortex-m4 soft-float.
- The mailbox and handoff claims hold: mailbox_stress under g++-13 -fsanitize=thread reports 2,000,000 posted, torn 0, out of order 0; the sketch is warning-clean under g++-13 and clang++-18 at C++17, 20 and 23 with the README's flags; both are .bss at -O0 and -Os and use inline exclusives on M4, M33 and M55. One caveat outside the plan's named targets: on cortex-m0plus the mailbox needs __atomic_exchange_1, which a bare-metal toolchain does not supply.
- Call depth through process -> run -> step<I> is not a hazard: painted high-water under QEMU at -O0 is 544 bytes for the 5-stage chain and 1,032 for a 12-stage one (about 70 bytes a stage), and under 120 bytes at -Os on the M33.
- D37's two kinds of design fit the carried designers: DspTap main's design_prototype already writes into a caller's span<double>, estimate_taps is closed-form arithmetic that a C++17 constexpr size function can reproduce, and the table and scratch sizes the first audit gave for bridge and async are consistent with the sources on origin/main.

### DSP: the accepted decisions measured

- Section 9.5's numbers are right: a 512/128 Hann overlap-add region from an all-zero state, frames stamped by the first sample of the window, has latency N - hop = 384 frames bare and N - 1 = 511 inside a 127-frame prefill (derived, and measured by simulation at 64-frame calls in stft_lat.py). steady<>'s capacity is sufficient whenever its prefill is.
- Section 5.2's "log2 N for Q15, log2 N + 1 for Q31" matches DspTap origin/main: the fixed policy returned e = 9 and 12 for Q15 at N = 512 and 4096, and 9, 10, 11, 13 for Q31 at 256, 512, 1024, 4096; the round trip is x/2N (Q15) and x/8N (Q31). The first audit's N = 512 round-trip figures reproduce (Q31 113.7 / 93.7 dB sine, Q15 38.1 / 18.8 dB).
- D39: an oversampler of linear-phase L-th-band stages (N = 2mL - 1) can always be made a whole number of base frames with an integer delay smaller than Factor samples at the top inner rate. Simulated with DspTap's designs: x2 needs none; x4 needs 2 samples at x4 (one frame at x2); x8 needs 6 or 2 by the parity of the last stage's m; x16 needs 14, 10, 6 or 2; by 3 needs 2 at x3; x6 needs 2 at x6. "One frame" is true of x4 only. The plan's 18.5 frames reproduces with m = (8, 4).
- D36's premise is true: on a Cortex-M33 (arm-none-eabi-g++ 13.2, -O2) the carried double accumulation of a float dot calls __aeabi_f2d twice, __aeabi_dmul, __aeabi_dadd and __aeabi_d2f; float accumulation is one vfma.f32 per tap.
- With D36 no FIR stage of either source library falls below its profile's attenuation: strict-order float floors are -131 to -149 dBFS for full-scale input at 21 to 773 taps, 12 dB or more beyond every 120 dB stage of SampleRateTap (bridge transparent 132 to 134 dB SNR, rational transparent 129 to 139 dB) and 6.6 dB beyond 120 dB at DspTap's 773-tap decimator. The floor is relative to the signal. The one stage at its own number is async's 140 dB tier (see findings).
- D36 changes nothing for the recursive stages: DspTap origin/main iir_kernels.h already runs float states, coefficients and products for the float profile (iir_traits<float>, lines 250 and 283-285), and butterworth.h states its float-against-double difference and its 1e-4 float cutoff bound, which the sketch's lowpass_design::make uses.
- Flushing a chain that contains steady<> or a rate change is well defined in principle: output counts are additive across calls, so the total can be folded through stateful outputs_for; a steady 512/128 region's signal is fully out after 2N - 2 = 1022 zero frames, with the FIFO left holding only post-tail silence. What is missing is the state argument and the tail's size (see findings).
- The sketch builds without a warning (g++-13 -std=c++17 -Wall -Wextra) and demo.cpp reproduces the D35 numbers quoted in section 9: the bare region writes 0 128 0 128, the steady one 64 64 64 64 with the impulse at frame 127, and fed_by pulls 26 frames for 102, makes 104, delivers 102 and holds 2.

### Build, safety posture, documentation, milestones after the tidy

- First-audit findings 15, 28 and 41 are handled where it matters: with the level and FFT choice in an inline namespace, a consumer that calls the compiled core or contains a live boundary or audit check is refused at link against a library of another configuration in all nine modes tried (g++-13 -O2 and -flto, g++-14 -Os with --gc-sections, clang-18 -O2, lld with --gc-sections, -flto, and arm-none-eabi-g++ cortex-m33 at -Os with -ffunction-sections and --gc-sections, the same with -flto, and -O0).
- An integrator can define the namespaced handler as `void catenae::check_failed(catenae::check) noexcept` without naming the tag, clean under g++ -Wall -Wextra -Wpedantic -Werror and clang -Weverything -Werror; specialising `catenae::is_stage<T>` from outside the inline namespace (D10) also compiles.
- Section 7.3's full GCC list on the sketch: sketch.h is clean on g++ 13.3, g++ 14 and arm-none-eabi-g++ 13.2 at -O0, -Os, -O2 and -O3 and at C++17, 20 and 23. Only the .cpp files fire (-Wmissing-declarations on free functions, -Wstack-usage on test mains). Clang 18 -Weverything less the plan's named exclusions gives only -Wctad-maybe-unsupported (answered by the explicit-guides rule in section 4) and -Wmissing-prototypes in the .cpp files. Nothing fired that the helper rules do not answer, except -Wclass-memaccess on a memset reset (second finding).
- D2's claim that the ratchet is equal across standards holds on the sketch: embedded.cpp disassembles identically at -std=c++17, 20 and 23 on cortex-m33 at -Os and -O2 (text 1204 and 1408 bytes), on g++-13 -O2 and on clang-18 -O2 (same md5 of the disassembly in each trio).
- D36's link-stage gate is buildable for the case it is meant for: the sketch's strip with a ROM design, linked for cortex-m33 with --gc-sections, contains no __aeabi_d* routine and no double libm symbol.
- The plan's Cortex-M33 figures reproduce from symbol sizes: overdrive state 6,184 bytes, strip configuration 32, strip state 548, mailbox<lowpass_design<float>> 76.
- CF is achievable against C0b and C1 headers with toy kernels: on origin/main SampleRateTap's rational, bridge and async include only sample_traits, fir_kernels, kaiser, quantize, nyquist, iir_design, iir_kernels, subnormals, chain and expects from DspTap, and no FFT, so nothing from C4 is needed and the 'no quality bar' wording covers the rest.
- The mailbox's and handoff's atomics are lock-free on every core the plan targets (Cortex-M3, M4, M23, M33, M55 with arm-none-eabi-g++ 13.2; Hexagon via clang-18, which expands the byte exchange with memw_locked), and a ctypes caller can drive a chain through C functions over caller-supplied storage once the missing pieces of the third finding exist.

### Do SampleRateTap's three engines fit the contracts as they now are

- The stage contract holds rational's and bridge's L/M machine with an all-zero state: a skeleton (scratch/audit2/fit/lm_fit.cpp; outputs after i inputs = ceil(i L / M), which re-encodes stage.h's non-zero `m_pending = 1`) run through the sketch's chain, process() in random chunks and fed_by() for 160/147, 147/160, up2|4/3, 3/4|down2, down4|down4|down3, up3|up4|up4 and bridge|up2 gave outputs_for exact, frames_needed its exact inverse, chunking-invariant output, fed_by equal to process, and the holdover within its room, clean under ASan and UBSan. D35 does give SampleRateTap's pull(out, k) a home.
- D39 fits both synchronous engines: rational's (N - 1) / (2M) and bridge's (L T - 1) / (2L) are exact rationals, the recursive stages are nominal-plus-deviation as iir_stage.h states, and the plan's 18.5 and 23.5 frames reproduce from design.h's pins (super_economy and economy, relaxed inner pair).
- async's state can be valid when all zero: every non-zero start value in converter.h and pi_servo.h is either configuration (target, gains, thresholds) or is re-seeded when pull() leaves filling (m_servo.seed(occ) before the first update); `filling` inverts to a flag, the state enum's filling is 0, and all telemetry atomics are 32 bits and lock-free.
- A nominal 1:1 rate_ratio is enough for D16: SampleRateTap's own 44.1 to 48 across clocks (bridge/examples/bluetooth_bridge.cpp) runs bridge on the producer side and async at 48 kHz on both sides, which is feeding(bridge, state, async, in). No engine needs hertz once designs are in cycles per frame.
- D42 matches the engines for stages: all three take the channel count as a template argument on origin/main, and the rational and bridge C ABIs already serve other counts by fanning out to one-channel converters, bit for bit.
- The table sizes D37 rests on are right: from the sources, bridge stores ceil(L/2) x T coefficients, 8,960 to 54,464 bytes in float (4,480 to 27,232 in Q15), async (L + 1) x T, 16,512 to 164,160 bytes, with design scratch up to 216,384 and 327,680 bytes of double; 43,120 and 73,920 coefficients per format across the named profiles. Each format needs its own table (row-sum-preserving quantization), which D47's 'fixed point from the first version' implies: 32 bridge and 16 async tables over four formats.
- rational's C ABI is fully enumerable under D45: its header says what is constructed is 'named, never looked up' and 'never a custom design', and every divisor the 33 chains use at the four profiles has a pin (count.py found none unpinned), so no named chain needs a run-time design search.
- First-audit findings on fit that the plan now handles: 33 and 37 (exact pull and overshoot: D35, verified above), 27 (exact latency: D39), 35 and 11 for the named profiles (table designs as ROM or caller storage, profile as a type: D37), 38 (the C ABI has a design: D45), 22 (report() as new design, thread sanitizer and arm64 legs).

## Findings

<a id="f1"></a>
### 1. The tidy redefined is_frame_synchronous around an undefined "input group"; a chain that passes it still returns uneven counts through the fixed-block entry

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** Section 3.2 (is_frame_synchronous row), section 3.4 (Fixed blocks), section 4 ("is_frame_synchronous compose"), section 9.5, D35
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** Before the tidy (12849d4, 3.2) the member was "Whether outputs_for(n) is n L/M on every call. False for a hopped transform and for a stage whose phase makes its output count vary", and D35 as accepted says "outputs_for(n) is always n L/M" with "a by-3 down then up returns 66, 63, 63" as a case it must exclude. The new 3.2 says "a call of n frames, n a whole number of the stage's input groups, always writes n L/M". That is weaker, and "input group" is defined nowhere: no member publishes its size, section 4 does not say how it composes, and 3.4's fixed-block static_asserts (whole frames, room for max_outputs_for, is_frame_synchronous) do not test it. With the sketch's rule (chain = AND of the stages), a down-then-up chain is 1:1 and "frame-synchronous" and the fixed-extent entry compiles for it, yet it does not return n for n: exactly D35's own counter-example. The same definition also contradicts 9.5: by its own terms a block stage called with whole hops IS synchronous ("A host whose callback is a multiple of the hop and aligned to it needs no steady<>"), but the boolean is false, so that host cannot use the fixed-block entry and is told to wrap in steady<> and pay 127 frames.

**Recommendation.** Replace the boolean (or back it) with a published number: `input_group_frames` (1 for map and gain, M for a decimator, the hop for a block stage), composed by the chain across the cumulative ratio as a constexpr, and have the fixed-block overloads static_assert N % input_group_frames == 0. Then is_frame_synchronous means group == 1 (what an arbitrary n-for-n callback needs and what steady<> provides), a hop-aligned host gets the compile-time entry without steady<>, and D35's by-3 example is refused at 64 frames. Put the definition and the composition rule in 3.2 and section 4, and add a compile-fail test for down|up at a non-multiple block to C1.

**Second reviewer.** Reproduced against the unmodified sketch.h (g++-13): down2|up2 and a toy down3|up3 both pass static_assert(rate_ratio == 1:1) and static_assert(is_frame_synchronous); the fixed entry returns 2 4 2 4 for 3 frames in and 63 63 66 for 64 in. One nuance the finding omits: equal-size arrays (3/3, 64/64, 441/441) are still refused, by the room assert ('output block is too small'), so the uneven counts appear only when the caller supplies the larger output array. 'Input group' occurs once in PLAN.md (line 151), 0 times in draft 32, and no member publishes it; section 4 says only that the flag 'composes'. Same defect as findings 5 and 15.

<details><summary>Evidence as reported (confidence: high)</summary>

Old text: git show 12849d4:PLAN.md line 232; new text: /home/user/catenae/PLAN.md line 151 (grep -c 'input group': new 1, old 0, response 0). sketch.h line 375: down2 has `is_frame_synchronous = true; // for calls of a whole number of pairs`; line 509: chain ANDs; lines 562-571: the fixed entry asserts only is_frame_synchronous, N % width and M >= max_outputs_for. I compiled /tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/tidy/fs.cpp against an unmodified copy of sketch.h (g++-13 -std=c++17 -O2 -Wall -Wextra): `constexpr auto dn_up = down2<1>{} | up2<1>{};` satisfies static_assert(rate_ratio == 1:1) and static_assert(is_frame_synchronous); process(state, std::array<float,3>, std::array<float,4>) compiles and prints "2 4 2 4 2 4" for 3 frames in; down2 alone with a 3-frame array prints "1 2 1 2 1 2". In lat.cpp, blockwise<128> through the dynamic entry at 128 frames a call returns "128 128 128", while the same call with two std::array<float,128> fails: "static assertion failed: catenae: a fixed block needs a frame-synchronous chain; wrap it in steady<>".

</details>

<a id="f2"></a>
### 2. reset() was dropped from the stage contract; "zeroing its state" costs a state-sized stack frame and the sketch's state cannot be memset

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** Section 3.2 (state row), section 3.3 (the audio-path list), section 6.2, section 8 (assumptions of use), C0b
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** Until the tidy the stage and analyzer tables had `reset()` ("Back to the constructed state") and 3.3 listed it with run/process/push/pull. The tidy removed the member and wrote "Resetting a stage is zeroing its state" (the junction keeps reset()). No operation is named, so the caller writes it. The natural spelling, `s = {};`, builds a whole zero state on the stack and copies it: the first audit's major findings 8, 10 and 17 (a multi-KB frame beside a 64-byte callback) come back through the reset path. The other spelling, memset, is rejected by the plan's own warning set because the state as sketched (std::tuple members) is not trivially copyable. A reset is exactly what the plan prescribes after a non-finite input (3.4, the card's Limits row), so it will be called from near the audio path.

**Recommendation.** Restore a named operation in the contract: `reset(state&)` on the chain (and for a bare stage's state), specified to clear in place with no temporary and added to 3.3's list of calls that cannot fail. Require in stage_traits that a `state` is trivially copyable and trivially destructible (static_assert), which rules out std::tuple as the chain state's member and makes an in-place clear legal. Add "reset only between calls, on the chain's thread" to the assumptions-of-use register, and pin the reset's stack in C1 beside the callback's.

**Second reviewer.** Reproduced in /tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/verify2/reset.cpp on arm-none-eabi-g++ 13.2 (cortex-m33): `s_voice = {}` is a 6,192-byte frame at -Os and 6,200 at -O2, memset trips -Wclass-memaccess, is_trivially_copyable fails on all three compilers, placement new takes 24 bytes; old plan has 6 `reset()`, new has 1 (the junction's). One detail differs: `s_voice = od_state{}` was 0 bytes at -Os/-O2 for me (6,192 at -O0), so which spelling is costly depends on flags. The stack cost comes from std::tuple in the sketch, not from the plan's sentence: a plain trivially copyable aggregate of the same size resets with `s = {}` in 8 bytes (szs.cpp), so the fix is a named reset plus a trivially-copyable requirement in stage_traits. Same finding as 6 and 20.

<details><summary>Evidence as reported (confidence: high)</summary>

grep -c 'reset()': old plan 6, new plan 1 (the junction's, line 295). Experiment in /tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/tidy/rst.cpp and rst2.cpp with the sketch's overdrive chain of 9.4, arm-none-eabi-g++ 13.2.1 -std=c++17 -mcpu=cortex-m33 -mthumb -Os -fno-exceptions -fno-rtti -fstack-usage: `void reset_assign() { s_voice = {}; }` -> .su says 6200 bytes static (disassembly: sub sp #6176+8, memset of 6184 bytes on the stack, then four memcpy into s_voice); the same at -O2; `s_voice = state_of<...>{}` folds to the same function. `std::memset(&s_voice, 0, sizeof s_voice)` -> 0 bytes of stack but "warning: clearing an object of type ... chain<...>::state" (-Wclass-memaccess, in -Wall, an error under D31). static_assert(std::is_trivially_copyable_v<state>) fails on arm-none-eabi-g++ 13.2, g++-13 and clang++-18 (libstdc++). Placement new with value-initialisation took 24 bytes.

</details>

<a id="f3"></a>
### 3. D47 and the C6 row no longer cover Q4.27 or the stages outside D47's two lists, and D47 was restated differently from what was accepted

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** D47, D29, D24, section 10 (C1, C2, C6 rows), section 12, section 9.1 and 9.2
- **Severity:** major as reported, minor after verification
- **Verdict:** partly

**Problem.** Four things changed when section 13's recommendation became D47 and the C6 row was rewritten. (1) C6 was "Q15, Q31 and Q4.27 for the stages that take them"; it is now "The fixed-point profiles that are new design (D47)". Q4.27, one of the three formats of D29's first set and the one section 12 argues a fixed-point chain runs in, is now in no milestone (C1's convert is "among float, q15 and q31"), and D24 still has no name for a Q4.27 stage (first-audit finding 3, major, items c and d). (2) D47's two classes are not exhaustive: mix, TPDF dither, delay, interleave/deinterleave, handoff, steady, parallel, the meters, correlation and the FFT FIR are neither "carried with fixed point" nor in the C6 list (recursive, nonlinear, ramped, spectral), though section 12 itself says dither, a threshold and a meter depend on the binary point and the old 3.1 said mix needs a saturating sum. The old C6 wording caught them; the new one does not. (3) The accepted text put convert, unpack and pack with the first version because they are "format stages that exist only because of" fixed point; D47 now lists them under "where the carried code has it". No carried code has them, and the old 3.1 list "What has to be written" (the tag's traits lookup, convert's rounding and saturation, a saturating multiply for gain, a saturating sum for mix) was deleted, so that work is described as a port. (4) The accepted text said CI instantiates "each stage's contract (not its arithmetic)" for q31; the qualifier is gone, which now collides with D29 ("A stage accepts only the formats its battery pins"). And no listing shows a stage written against the traits: 9.1 and 9.2 hard-code port<float, Ch> and span<const float>, and the sentence that run takes "spans of the ports' value types" (the tag is not the element type) was cut from 3.2.

**Recommendation.** Give D47 a third clause and make the classes exhaustive: name which milestone gives each of mix, dither, delay, the layout converters, the meters and correlation their fixed-point form (most are copies or need one saturating helper, so C1/C2/C5 with the stage), say the FFT FIR is floating only if that is the decision, and put Q4.27 in a named milestone (convert to and from it at C1; pinned per stage with its profile). Restore "(not its arithmetic)" and say how it squares with D29's refusal (the traits instantiate; run does not). Move convert, unpack and pack out of "carried code has it" into "written new, with the stage", and restore the four-item list of new fixed-point work in section 12. Show one stage in section 9 written over S with the port's value type distinct from the tag.

**Second reviewer.** True: Q4.27 appears only at PLAN.md lines 77, 136-138 and 957-959 and in no milestone (old C6 row named it); mix, dither, delay, the layout converters and the meters are in neither of D47's lists; the old 3.1 'What has to be written' list and '(not its arithmetic)' are gone; no pack/unpack exists in either source library. Overstated: the accepted text (draft 32 lines 1297-1312) also put convert, unpack and pack in the 'with the first version' bullet, so the timing decision is unchanged and only the reason was compressed; the fixed<> tags are now in C0b and convert in C1, so the first audit's scheduling gap is mostly closed. What remains is a milestone and a D24 name for Q4.27 and a class for the unlisted stages: wording work, not a blocker.

<details><summary>Evidence as reported (confidence: high)</summary>

grep -n 'Q4\.27' PLAN.md: lines 77 (D29), 136-138 (3.1), 957-959 (section 12); none in section 10. Old C6 row: git show 12849d4:PLAN.md line 1012; accepted wording of D47: same file lines 1297-1312. No convert/pack/unpack in the sources: `git -C /home/user/DspTap grep -n -i -E '\bpack\b|unpack|s24|endian' origin/main -- include/` returns only comments in fir_kernels.h, pvoc.h and spectrum.h; `git -C /home/user/sampleratetap grep -n -i -E 'unpack|s24|int24|byte order|bswap' origin/main -- '*.h'` returns nothing outside the submodule. grep -c in the new plan: 'saturating multiply' 0, 'saturating sum' 0 (old: 1 each). New D47 text: PLAN.md line 78.

</details>

<a id="f4"></a>
### 4. Latency is a value of the configuration that a setter changes, but parallel<> must align latencies with storage sized at compile time; D43 also lost its ratio rule

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** D43, D39, D34, D22, section 3.2 (latency row), section 4, C1
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** Under D34 and D37 latency is "stored numbers from the design", read from the configuration, and set_design() replaces the design while the stream runs. D43 says parallel<ChainA, ChainB> runs two chains "with their latencies aligned", and C1 delivers it. Aligning needs a delay line in parallel's state, whose size D22 fixes by template arguments. The plan does not say whether latency is a compile-time property of the type (then a chain holding a designed filter cannot be in a parallel, which is the dry-beside-wet case D43 exists for) or a run-time value (then parallel needs a maximum as a template argument, a rule for what happens to the alignment when a design changes, and a rule for a non-integer or non-exact latency such as 2.176 frames). The same question decides whether a chain's latency()/exact_latency() are static constexpr, which stage_traits must fix at C0b. The tidy also dropped D43's accepted condition "Their ratios must match (asserted)" and that parallel is what a side chain is later built from; neither is in the response file.

**Recommendation.** Decide and write into 3.2 and D43: (a) which latency members are static constexpr (exact stages whose design is a template argument or a ROM constant) and which are values; (b) that parallel<A, B> requires compile-time exact latencies on both sides, or takes a MaxFrames argument and re-aligns (and how: a step) when a configuration changes; (c) how a fractional or inexact nominal latency is aligned (rounded, or refused). Restore "ratios must match (asserted)". Add parallel with a designed filter in one arm to the sketch before C0b freezes stage_traits.

**Second reviewer.** Plan text is as quoted: 3.2 marks max_outputs_for `static constexpr` and latency() as 'stored numbers from the design'; D43 says only 'with their latencies aligned'; no parallel, latency() or oversample in sketch.h. My host run gives the low-pass 2.176, 11.239 and 225.078 frames at 0.1, 0.02, 0.001, and two chains of one type with latencies 2.176 and 22.5. 'Their ratios must match (asserted)' and the side-chain sentence are in the old D43 (line 84) and in neither the new plan nor the response file. The natural answer (parallel takes only arms with a compile-time exact whole latency) is nowhere stated, and the trait's form is fixed at C0b, so it needs a decision; same finding as 8 and 42.

<details><summary>Evidence as reported (confidence: medium)</summary>

PLAN.md line 157 (latency row: no `static`, unlike max_outputs_for on line 156), line 69 (D43), lines 590 and 607 (set_design on a live strip). In the sketch, lowpass::nominal_latency() is a non-static member reading m_design (sketch.h line 308). I ran /tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/tidy/lat.cpp: latency at cutoff 0.1 is 2.176 frames; after set_design(make(0.02)) it is 11.239; after set_design(make(0.001)) it is 225.078. grep -c 'must match': old plan 1, new plan 0, response 0. parallel, latency(), exact_latency() and oversample are not in sketch.h (grep), so none of this has been compiled. The conclusion about parallel is by reasoning from D22, not from a build.

</details>

<a id="f5"></a>
### 5. is_frame_synchronous composed by && is unsound: a decimate-then-interpolate chain is labelled frame-synchronous, passes the fixed-block entry, and traps at run time on a host block that is not a multiple of its hidden input group

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md D35; section 3.2 (is_frame_synchronous row); section 3.4 'Fixed blocks'; section 4 ('rate_ratio and is_frame_synchronous compose'); sketch.h:375, 509, 562-571
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** The tidy changed the definition from draft 32's 'outputs_for(n) is n L/M on every call ... false for a stage whose phase makes its output count vary' to 'n a whole number of the stage's input groups', which is what the sketch's down2 does ('true; // for calls of a whole number of pairs'). But no contract member publishes the input group, the chain composes the flag with a plain && and reduces rate_ratio, so for down2 | x | up2 the ratio is 1/1, the group (2) is lost, and the chain claims n-for-n for every n. Neither definition composes by &&: under draft 32's wording down2 is false and the plan's own flagship 9.4 chain (up2|up2|map|down2|down2, truly n-for-n) would fail the fixed-block static_assert. The fixed-block overload asserts whole frames, room and the flag, but not that N is a whole number of groups. This is finding 37 of the first audit ('equal-size callbacks trap') returning for every chain that decimates before it interpolates, which is the ordinary multirate shape and the shape of D16's rational chains.

**Recommendation.** Add a published constant to the stage contract, e.g. static constexpr std::size_t input_group (frames; 1 for up2, M for an L/M stage, undefined or 0 for a block stage), define is_frame_synchronous as 'for calls of whole input groups', and have the chain compose the group (smallest n such that n times the product of the earlier ratios is a multiple of each later stage's group; constexpr-computable). The fixed-block process then static_asserts N % input_group == 0, and the text of D35 says an n-for-n callback with a block size the host chooses needs steady<> unless input_group == 1 (steady<down2|up2> already works in the sketch: burst 2, one frame of latency). Put the down2|up2 case in C1's compile-fail and run batteries.

**Second reviewer.** Same defect as finding 1, reproduced independently: chain ANDs the flag (sketch.h:509), down2 declares true 'for whole pairs' (line 375), and down2|up2 through the dynamic entry with 441 in / 441 out gives outputs_for 440 then 442 and 'CHECK 2 FAILED' (abort) on the second call. up2|up2|map|down2|down2 does return n for n (5 5 5 5), so neither definition composes by &&. The title merges two paths: the fixed entry gives uneven counts (and refuses equal-size odd blocks via the room assert); the trap is on the dynamic entry.

<details><summary>Evidence as reported (confidence: high)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/sketch/t/fsync.cpp against the unmodified sketch.h, g++-13 -std=c++17 with ASan/UBSan. static_assert(decltype(down2<1>{} | map | up2<1>{})::rate_ratio == ratio{1,1}) and static_assert(...::is_frame_synchronous) both pass. Fixed-extent process with std::array<float,3> in, std::array<float,4> out compiles and returns 2 4 2 4 2 4. chain(down2).process with array<5> in, array<3> out compiles and returns 2 3 2 3 2 3. Dynamic entry with a 441-frame host block and a 441-frame output: call 0 outputs_for=440, returned 440; call 1 outputs_for=442, 'CHECK 2 FAILED -> handler (abort)', i.e. the integrator's safe-state handler on the second audio callback.

</details>

<a id="f6"></a>
### 6. 'Resetting a stage is zeroing its state' has no clean mechanism: st = {} costs the whole state in stack on the M33, memset is a -Wall error, and a state not in static storage is not zero

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md section 3.2 (state row: 'Resetting a stage is zeroing its state'), D34, D22, section 9.7 ('a caller's arena'), D45; sketch.h:513-516 (chain::state holds std::tuple), 807-818 (fed_by_memory)
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** The tidy removed the stage's and the analyzer's reset() member (draft 32 line 237: 'reset() | Back to the constructed state') and replaced it with 'zeroing its state' without saying how. In the sketch every chain state and fed_by state contains std::tuple, whose copy assignment is user-provided in libstdc++, so the state is not trivially copyable, not trivially assignable and not standard-layout. Consequences: (1) 'st = {}' compiles but builds a full-size temporary on the stack and copies it, at every optimisation level: the very start-up-stack cost D34 was accepted to remove comes back at reset, and it scales with delay lines and FIFOs. (2) std::memset(&st, 0, sizeof st) trips -Wclass-memaccess, which is in -Wall and so an error under D31. (3) States are aggregates without default member initialisers, so 'valid when all zero' is true only for static storage: a local or arena-placed state (section 9.7 names the arena) is indeterminate, and nothing in the type says so. (4) mailbox and handoff cannot be assigned at all (deleted by std::atomic); handoff has no reset() in the sketch and the mailbox contract (6.3) has none, so a stale fresh value survives a stream restart. A C caller under D45 supplying zeroed storage has the same question of what makes those bytes a state.

**Recommendation.** Decide the mechanism in section 3.2 and put it in C0b: (a) stage_traits static_asserts that state is trivially copyable (and say whether standard-layout is required for D45); (b) the chain's state is a hand-written recursive aggregate, not std::tuple; (c) every state member has a default member initialiser so that 'state st;' is zero anywhere and a static one is still constant-initialised into .bss; (d) one named operation, catenae::reset(state&), is the only way the library and its documentation zero a state, with its stack pinned by the size probes; (e) mailbox and handoff get reset() members (handoff's is already in the 6.1 table), with the 'neither side running' assumption in the register.

**Second reviewer.** Same core as finding 2, reproduced: chain state not trivially copyable or standard-layout, `reset_ref(od_state& st){ st = {}; }` is 6,192 bytes of stack at -Os and 6,200 at -O2/-O0, memset warns under -Wall. Also reproduced: a non-static `state_of<lowpass<2>>` left uninitialised gives 'first sample out = inf' with clang++-18 -Weverything and no warning (uninit.cpp), and the remedy holds (an aggregate with default member initialisers stays in .bss and `s = {}` on it costs 8 bytes). The mailbox/handoff assignment point follows from std::atomic's deleted assignment; the junction keeps reset() in 6.1 but the mailbox contract has none. The 7,240-byte fed_by figure and the segfault I did not re-run.

<details><summary>Evidence as reported (confidence: high)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/sketch/t/reset.cpp. is_trivially_copyable, is_trivially_copy_assignable, is_standard_layout all false for state_of<overdrive> and for fed_by_state (g++-13 and clang++-18); true for gain::state and steady<blockwise>::state. arm-none-eabi-g++ 13.2 -mcpu=cortex-m33 -fstack-usage: 'void reset_assign(od_state&) { st = {}; }' uses 6,192 bytes of stack at -Os (6,200 at -O2 and -O0) for a 6,192-byte state; the fed_by state 7,240 bytes; the tuple-free steady state 8 bytes. memset under -Wall: 'clearing an object of type ...::state with no trivial copy-assignment [-Wclass-memaccess]'. 'm = {}' on mailbox<float> and handoff<2,1024>: 'use of deleted function operator='. t/local.cpp: 'fed_by_state<decltype(up4)> st;' as a local, then fed_by: segmentation fault at -O0 with no warning (held is garbage). Remedy tried in t/bss.cpp: a nested aggregate with default member initialisers in place of std::tuple is trivially copyable, a static one is still in .bss (nm: B, 3,084 bytes), and 's = {}' on it uses 8 bytes of stack on the M33.

</details>

<a id="f7"></a>
### 7. steady<> primes its FIFO from max_outputs_for(1), which is not the worst shortfall of a chain of two block stages: the FIFO underruns silently and returns wrong audio

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md D35; section 9.5 ('The FIFO starts with one frame fewer than the largest burst'); section 4 (steady<Config>); sketch.h:651-652, 681-690
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** steady<Inner> needs the most frames by which Inner's output can lag its input (the worst shortfall). The sketch takes k_burst = Inner::max_outputs_for(1) and primes k_burst - 1 frames. For one block stage of hop H that is H - 1 and right. For a chain of two block stages with hops A then B it is ceil(A/B)*B - 1, while the true shortfall is up to (A - 1) + (B - 1): the first stage withholds up to A - 1 frames and the second up to B - 1 of what the first released. When the shortfall exceeds the priming, 'held' goes negative, is cast to size_t, and the modular indexing reads stale FIFO contents: no sanitizer fires and no check fails. No member of the 3.2 contract table carries the number steady<> needs, so a stage author cannot supply it. Any 1:1 region built from two bursting stages with non-commensurate blocks is affected (for example the C4 overlap-save FFT FIR with its block beside a C5 stft region with a different hop). The sketch also relies, unstated, on max_outputs_for being monotone in n for its scratch, burst and holdover sizing.

**Recommendation.** Add the number to the stage contract as a published constant, e.g. static constexpr max_shortfall (output frames by which the stage can lag n L/M from any state; 0 for a frame-synchronous stage, H - 1 for a block stage), compose it in the chain (each stage's shortfall carried to the chain's output rate and summed), and have steady<> prime from that and state it as added_latency_frames. Say in 3.2 that max_outputs_for is non-decreasing in n. Add an audit-level check that the frames held never go negative, and put the 128|100 and 7|5 cases in C1's steady battery.

**Second reviewer.** Reproduced with my own ramp test on the unmodified sketch.h under ASan/UBSan (clean): steady<chain<blockwise<128>, blockwise<100>>> has max_outputs_for(1) = 200, added latency 199; with 33-frame calls the frames held reach -24 and 120 of 13,200 samples are wrong; 48-frame calls -9; 7|5 reaches -1 with wrong samples; commensurate pairs (128|64, 128|96, 64|48, 128|128) give 0 wrong. The plan relies on this rule itself (9.5: 'The FIFO starts with one frame fewer than the largest burst'), and 3.2 has no member carrying the worst shortfall. The case needs two block stages with non-commensurate hops in one steady region, which is uncommon, but the failure is silent wrong audio.

<details><summary>Evidence as reported (confidence: high)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/sketch/t/steady.cpp, unmodified sketch.h, g++-13 -std=c++17 ASan/UBSan (clean, which is the point). A ramp through chain(steady<chain<blockwise<A>, blockwise<B>>>) compared with the same ramp delayed by added_latency_frames. A=128,B=100: max_outputs_for(1)=200, added latency 199; with 33-frame calls the frames held fall to -24 and 120 of 13,200 output samples are wrong; with 48-frame calls -9 and 30 wrong. A=7,B=5: priming 9, held -1, 11 wrong samples with 1-frame and with 64-frame calls. Arithmetic: 127 + max(128k mod 100) = 127 + 96 = 223 > 199. Commensurate pairs are fine (128|64, 128|96, 64|48: 0 wrong). fed_by's holdover, sized from the same max_outputs_for(1), is sound because it bounds an overshoot, not a shortfall (t/fed.cpp: max held 199 of 200).

</details>

<a id="f8"></a>
### 8. Latency and tail are values of a configuration (D34, D37) but parallel<> and flush need them to size memory fixed by the type (D22); the sketch has neither latency() nor flush nor parallel, so the conflict is not visible

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md D43, D39, D9, D22; section 3.2 (latency(), tail_frames() rows); section 4 (flush, flush_output_frames, parallel); milestone C1
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** Under D34 and D37 a stage's latency and tail are 'stored numbers from the design', and under section 9.2 a configuration whose parameters change is a variable whose design is replaced at run time. So latency() and tail_frames() are run-time values of a configuration object, not properties of its type; the sketch's only instance, lowpass::nominal_latency(), is exactly that. But state_of<parallel<A, B>> must hold the delay that aligns the two chains, and by D22 its size is fixed by template arguments: the type cannot see the value. The plan does not say where the alignment length comes from (a template argument for the maximum, or a static constexpr latency that only type-determined stages can supply), what parallel does with a latency that is not a whole number or not exact (the lowpass's 2.176 frames), whether its construction can fail when the difference exceeds the maximum (a stage 'cannot fail'), or what set_design() on a stage inside one arm does to the alignment while the stream runs. The same gap sits under flush: flush_output_frames() is a run-time number when a tail comes from a run-time design, so the caller's output buffer for flush(state&, out) cannot be sized at compile time, and a recursive stage's tail can be thousands of frames delivered in one call (which also lands in the ratchet's largest-single-call number). parallel was added after the first audit (finding 62) and has never been reviewed as a design; it is in C1 and latency()'s form is frozen in C0b.

**Recommendation.** Settle before C0b freezes the headers: (1) split the report into what the type knows and what the value knows, e.g. a static constexpr max_latency_frames (and max_tail_frames) beside the value-level latency(); (2) give parallel its maximum alignment as a template argument or restrict it to arms whose exact latency is a compile-time whole number, and say which; (3) say that a setter that changes latency inside parallel either is not allowed or realigns with a stated artefact; (4) make flush resumable (it fills what it is given and reports whether more remains) or publish the compile-time bound a caller sizes from. Then add latency(), flush and a two-arm parallel to the sketch, since it is the plan's evidence.

**Second reviewer.** Same gap as finding 4, confirmed by my own run (one chain type, two stored latencies 2.176 and 22.5) and by reading: sketch.h has no latency(), exact_latency, tail_frames, flush or parallel, so the plan's evidence does not cover them. D22 fixes state size by template arguments, and the plan nowhere says where parallel's alignment length comes from, what it does with a non-whole latency, or what set_design does to it. The flush half is the same point as findings 14 and 44 (a run-time tail sizing one output buffer). The 'construction can fail' worry is speculative; the rest stands.

<details><summary>Evidence as reported (confidence: medium)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/sketch/t/lat.cpp, g++-13 -std=c++17: two configurations lowpass<1>{k_a} | gain<1> and lowpass<1>{k_b} | gain<1> have the same type and the same state type (both static_asserts with is_same_v pass; sizeof state 284 for both) while their stored latencies are 2.176 and 22.5 frames. With the section 9.2 form 'static auto s_strip = ...', using s_strip.stage<0>().nominal_latency() as an array bound fails: 'the value of s_strip is not usable in a constant expression'. grep of sketch.h: no latency() on chain or steady beyond the constant added_latency_frames, no exact_latency, tail_frames, flush or parallel. Draft 32 (git show 12849d4:PLAN.md line 84) says only that parallel 'aligns their latencies'; its old example sized a delay by hand (delay<2, 480>). Not run: there is no parallel to run.

</details>

<a id="f9"></a>
### 9. fed_by pulls from a junction in 64-frame pieces, and SampleRateTap's async converter then underruns on every callback of 256 frames or more

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** Section 6.1 (pull, fed_by, fed_by_state "the frames in transit"), D19, D35, D16; sketch.h line 836; milestones C0b (junction_traits), C1 (fed_by), CF, C8
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** The plan's fed_by fills a caller's buffer by calling junction.pull() once per chain block (64 frames), because fed_by_state's staging is one block. That is harmless for handoff. The async converter, which the plan names as the second junction, infers the consumer's period from the largest single pull it has seen. It raises its FIFO setpoint to that size plus a margin, because, in its own words, the setpoint must exceed the pull block "or the loop drains into a permanent underrun limit cycle". Driven in 64-frame pieces it sees 64 and sets 96, while the real demand per callback is 256 or more arriving back to back. The junction contract has no member that tells a junction the consumer's period and no rule that pull is called once per period. So the contract as written cannot host async behind fed_by. CF is unlikely to catch it: its skeletons have "no quality bar on the signal processing", and this only shows with the real servo.

**Recommendation.** Decide in section 6.1 how a junction learns the consumer's period, before junction_traits is frozen at C0b. The simplest form: the junction's design takes the largest frames-per-period as an argument (validated against capacity_frames in make()), so the setpoint never depends on observed pull sizes, and pull may then be called in pieces. The alternative is to make fed_by issue one pull per call, which needs staging of frames_needed(largest callback) frames in fed_by_state (finding 50's kilobyte again) and a compile-time maximum callback. Add to CF: the async skeleton is driven through fed_by with a callback larger than the chain block, with the real servo, and must lock.

**Second reviewer.** Reproduced with the real basic_converter<float,1> from SampleRateTap origin/main (7dbc79d) on its pinned DspTap (4b30a02), my own driver, +100 ppm, 20 s, fifo 16384: callback 256 pulled whole locks with 0 underruns and target 384; pulled as 4 x 64 gives 3,750 underruns, target 96, state filling, 81.25 % real; 512 as 8 x 64: 1,875 underruns and resyncs, 18.95 %; 1024: 937, 9.47 %; 128 as 2 x 64 never leaves acquiring. converter.h lines 334-358 set the setpoint from the largest single pull, and sketch.h:836 slices to k_block. The 6.1 junction table has no member for the consumer's period. Duplicate of finding 23; CF exists to find fit problems, but its 'no quality bar' skeletons would not exercise the servo.

<details><summary>Evidence as reported (confidence: high)</summary>

I extracted SampleRateTap origin/main (7dbc79d) async headers and its pinned DspTap (4b30a02) into the scratch directory and ran the real basic_converter<float, 1>: async_chunks2.cpp, g++-13 -std=c++20 -O2, fifo_frames 16384, clocks 100 ppm apart, 20 simulated seconds, push block equal to the callback. Results:
- Callback 256 pulled as 1 x 256: 0 underruns, setpoint 384, locked.
- Callback 256 pulled as 4 x 64: 3,750 underruns (one per callback), setpoint 96, state stuck at filling, 81 % of output frames real.
- Callback 512 as 8 x 64: 1,875 underruns, 1,875 resyncs, 19 % real.
- Callback 1024 as 16 x 64: 937 underruns, 9 % real.
- Callback 128 as 2 x 64: no underruns, but it stays in acquiring and never locks.
- Pulled whole, every size has 0 underruns.
Source: converter.h lines 334-358 on origin/main (m_observed_max_pull; needed = frames + max(frames / 2, 16)). sketch.h line 836 is `n = std::min(chain.frames_needed(st.chain, want - done), k_block)`, followed by junction.pull of n frames.

</details>

<a id="f10"></a>
### 10. handoff has no fill policy, so its latency has no nominal value: the delay is set by which thread ran first and grows by every underrun

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** Section 6.1 (latency() {nominal, range}; report() "state"; pull "always fills out"; reset(); "Latency across a junction composes as nominal only"), D19; sketch.h lines 709-744; C0b, C1
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** The junction contract promises latency() as {nominal, range} and a state field in report(). handoff, the instance that "proves the contract", is a bare ring: pull returns whatever is there and pads with silence. With both sides on one clock the fill is then start-up phase plus every frame of underrun so far, minus every frame dropped on overrun. Nothing brings it back. A consumer that happens to start with the ring near empty stays one late producer wake-up away from an underrun, and each underrun permanently adds its length to the delay. So handoff has no nominal; its range is 0 to the whole capacity. The async converter has what handoff lacks: a filling state until the fill reaches a threshold, a setpoint, and a hard resync at a high-water mark. The plan decides none of these for handoff. Separately, the contract's reset() "only while neither side is running" drops the consumer-side restart async has (reset_from_consumer(), safe while the producer runs), which is also the natural way to re-prime a handoff.

**Recommendation.** Give handoff a stated fill policy in section 6.1, as numbers:
- a target fill (template argument or configuration);
- pull delivers silence until the fill first reaches it (report().state = filling, then running);
- after an underrun it re-primes;
- what an overrun drops (newest, as async).
Then nominal is the target and range is the two block sizes. Add a consumer-side restart to the junction contract (a drain that only the pull side calls) beside the at-rest reset(). Pin "delay after start-up in either order" and "delay after one underrun" as C1 tests.

**Second reviewer.** Ran the unmodified sketch handoff<1,1024> single-threaded (host.cpp): producer-first gives delay 0 for 1,000 periods; consumer-first gives a permanent 64-frame delay after 64 frames of silence; one late producer period moves the delay from 0 to 64 for good (the reviewer reports 128 for their model of a late period; the mechanism is the same). The async converter on origin/main does have m_filling, m_fill_threshold_frames, m_high_water_frames and reset_from_consumer() (converter.h 186, 362-397, 419). 6.1 promises latency() {nominal, range} and a state in report() and states no fill policy for handoff, so its proving instance has no nominal. A caller can pre-push silence, and the ring settles after its first underrun, but neither is in the contract.

<details><summary>Evidence as reported (confidence: high)</summary>

handoff_latency.cpp in the scratch directory runs the sketch's handoff<1, 1024> unchanged, single-threaded with the two sides interleaved by hand, pushing a ramp and comparing each delivered frame's input index with its output position.
- Producer first in each period: delay 0 frames for 1,000 periods.
- Consumer first in each period: delay 64 frames, permanently, 64 frames of underrun.
- Producer first, then one period where the producer is late: delay jumps from 0 to 128 frames and stays there (128 frames of silence delivered mid-stream).
- A later 20-period consumer stall drops 384 frames and changes the delay again.
Read on origin/main: converter.h's m_filling, m_fill_threshold_frames, m_high_water_frames and the resync at lines 362-397; restart() and reset_from_consumer() at lines 186 and 419, both consumer-thread calls.

</details>

<a id="f11"></a>
### 11. A mailbox carries bytes, not ownership: a posted view races, and nothing tells the poster when the audio thread has let go of the old table

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** Section 6.3 (T "trivially copyable (asserted): a design, a level, a reading"), D44; section 6.2 (result_type "a value, or a view of storage in the state"; "leave it through a mailbox"); section 3.3 and D37 (table designs in the caller's storage); section 9.7 ("holds a view")
- **Severity:** major as reported, minor after verification
- **Verdict:** partly

**Problem.** The only requirement on T is trivial copyability. A span or any view type meets it, so the assert gives false assurance in two cases the plan itself sets up.
1. An analyzer's result may be "a view of storage in the state", and results "leave [the chain's thread] through a mailbox". Posting that view hands another thread a pointer into state the audio thread keeps writing.
2. A table design is built into the caller's storage and the stage holds a view. A new table reaching the audio thread through a mailbox is a posted view. The control thread cannot learn when the audio thread has collected it and stopped reading the previous table, because the mailbox has no return path. It can therefore never safely rebuild into, or reuse, the old storage. Under D22 there is no heap to hide this behind.
There is also a cost: post(const T&) makes the audio thread build a whole T (on its stack) and then copy it into the slot. For a reading the size of a spectrum (257 complex<float> is 2,056 bytes) that is a 2 KB stack temporary plus a second copy, on the thread section 9.7 says must not carry large objects on its stack.

**Recommendation.** State in 6.3 and in the assumptions-of-use register that a mailbox value owns everything it refers to: no pointer, span or view. Make it an opt-in trait where possible, since C++17 cannot detect it. Decide how a table design is replaced at run time: either a return mailbox carrying the generation the audio thread now runs (the control thread reuses a table only after seeing a later generation acknowledged), or rule it out and say a table design changes only at rest. Say that an analyzer whose result is a view is copied into an owning reading before it is posted. Add an in-place form, post_with(writer), where writer fills the poster's own slot, so that a large reading is written once and never sits on the audio stack.

**Second reviewer.** Facts hold: mailbox<span<const float>> compiles against sketch.h, and a control thread alternating two 63-tap tables and posting a view produces TSan data-race reports and 36,821 torn reads in an uninstrumented run; 6.2 does allow a result that is 'a view of storage in the state' and 9.7 has stages holding views. Overstated: the plan nowhere promises run-time replacement of a table design, and 'can never safely reuse the old storage' ignores that a second mailbox in the reverse direction (the audio thread posting the generation it runs) is the acknowledgement, with no new mechanism. What is missing is a stated rule (a posted value owns what it refers to, or an ack protocol) in 6.3 and the assumptions register.

<details><summary>Evidence as reported (confidence: medium)</summary>

view_race.cpp in the scratch directory: mailbox<span<const float>> compiles against sketch.h unchanged. A control thread alternates between two 63-tap tables ("redesign into the one not in use") and posts a view; an audio loop takes and reads. g++-13 -fsanitize=thread reports data races between the table write and the read. Without the sanitizer, 72,669 samples were read from a table in mid-rewrite in one run (13,319 in the instrumented run). The plan's text: section 6.2 result_type row and the sentence after the table; 3.3 "built by make(spec, table, workspace) into the caller's storage"; 9.7 "fir<63, 2>{k_taps} holds a view"; 6.3's T row.

</details>

<a id="f12"></a>
### 12. A chain's scratch is part of its state, so every voice and every channel-state pays for it again: 93 to 99 % of state_of<Chain> is scratch that carries nothing between calls

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** D34 ("state_of<Chain> is the memory", "One configuration serves many states"), D42 ("runs one mono configuration over as many states as there are channels"), section 4 ("Its state holds its stages' states, the scratch between them and a zero block for flushing"), section 9.4 (two voices), 9.7
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** D34 advertises one configuration serving many states (voices), and D42 makes "one mono configuration over N states" the answer to run-time channel counts. But the state bundles two different things: history (must persist, must be per voice) and inter-stage scratch (dead between calls, since run() consumes all of its input, and the plan fixes "one call, one thread"). In the sketch the history of the overdrive chain is 40 bytes and the scratch is 6,144. Sixteen voices cost 98.9 KB of .bss where 6.8 KB would do; twelve mono channel-states cost 37.2 KB where 3.4 KB would do. The plan's in-place sharing rule shrinks scratch for all-1:1 chains but not for any chain with a rate change or a port change, and the flush zero block adds another block x Ch x sizeof(S) per state. Reset also has to clear all of it. This is the RAM figure section 9.4 quotes as the result (6,184 bytes), and it is the per-instance price of the idiom the plan recommends. Changing it later changes process()'s signature, state_of, feeding_state/fed_by_state, and every size in the C ABI manifest (D45).

**Recommendation.** Decide it before C1: split `state_of<Chain>` (history only, per voice, zero-valid) from a `workspace_of<Chain>` (scratch and the flush zero block; one per thread, shareable by every state of that chain type, need not be zero). Offer `process(state&, workspace&, in, out)` as the primary form and, if wanted, a convenience type that holds both for the single-instance case. State the per-voice RAM in 9.4 as history only, and size the C ABI manifest's storage constants the same way.

**Second reviewer.** Reproduced on arm-none-eabi-g++ 13.2, cortex-m33 (szs.cpp): overdrive state 6184 = 40 stages + 6144 scratch; mono 3096 = 24 + 3072; the 9.2 strip 548 = 36 + 512. Section 4 does say the state holds the scratch and a zero block, and in the sketch chain::run consumes all input each step, so scratch carries nothing between calls. Caveat the finding half-admits: the all-1:1 figures (the strip, the 8-stage strip) are sketch artefacts, since the plan's in-place sharing would remove that scratch; the cost is real for chains with a rate or port change such as 9.4's. Not raised in the first audit, and changing it later changes process()'s signature and the C ABI sizes.

<details><summary>Evidence as reported (confidence: high)</summary>

In my scratch (audit2/embedded/st/szs.cpp, szs2.cpp) I printed sizeof(state), sizeof(state::stages), sizeof(state::scratch) from the unmodified sketch.h with arm-none-eabi-g++ 13.2 -mcpu=cortex-m33: overdrive (9.4) 6184 = 40 + 6144; the same chain mono 3096 = 24 + 3072; the strip of 9.2 548 = 36 + 512; an 8-stage all-1:1 stereo strip (6 lowpass, 2 gain) 3720 = 136 + 3584; a 12-stage chain with two 63-tap FIRs 11888 = 1136 + 10752. Arithmetic: 16 x 6184 = 98,944 vs 16 x 40 + 6144 = 6,784; 12 x 3096 = 37,152 vs 12 x 24 + 3072 = 3,360. sketch.h lines 513-516 put scratch inside chain::state; steady<>::state::burst and fed_by_memory::pulled/made are the same kind of per-call scratch. Nothing in sketch.h reads scratch written by a previous call.

</details>

<a id="f13"></a>
### 13. "Resetting a stage is zeroing its state" has no way to be done: the sketch's chain state is not trivial, `st = {}` needs a stack frame the size of the state at every optimisation level, and memset is a -Werror

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** Section 3.2 (`state` row: "Resetting a stage is zeroing its state"), D34 ("valid when all zero"), D45 (C functions over storage the caller supplies), D31, sketch.h chain::state (lines 513-516)
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** The stage and chain contracts have no reset operation (only the junction has `reset()`), and nothing requires a state to be a trivial type. The sketch builds chain::state from std::tuple, which is not trivially default-constructible, not trivially copyable and not standard-layout. Consequences: (1) the natural reset, `s_voice = {};`, materialises a temporary on the stack: 6.2 KB for the 9.4 chain, at -O0, -Os and -O2, on the thread that resets, which on an embedded target is often the audio thread or an ISR; (2) the other spelling, memset, raises -Wclass-memaccess (in -Wall), which D31 makes an error; (3) "valid when all zero" is only a language-level guarantee for a trivial type, and D45's C ABI hands the library raw zeroed storage from a C caller; (4) a state placed in "a caller's arena" (9.7) or declared as a local is only zero if the caller remembers `{}`, with no check. A trivial aggregate does not have the problem: GCC turns `= {}` into memset with a 0 to 8 byte frame.

**Recommendation.** In C0b: stage_traits static_asserts `std::is_trivial<state>` and `std::is_standard_layout<state>` for every stage, and the chain's state is a hand-written recursive aggregate, not std::tuple. Add `reset(state&) noexcept` to the stage and chain contracts (one function, written once from the traits, that zeroes in place) and say on which thread it may be called. Add a compile-fail test for a non-trivial state and a -fstack-usage pin on reset in C1's size probes.

**Second reviewer.** Reproduced on arm-none-eabi-g++ 13.2 cortex-m33: the overdrive chain's state is not trivially default-constructible, not trivially copyable, not standard-layout (lowpass<2>::state is trivial); 's_voice = {}' has a 6192/6192/6200-byte frame at -O0/-Os/-O2; memset gives 0-8 bytes but raises -Wclass-memaccess; a plain aggregate resets in 8 bytes. The tidy removed draft 32's reset() row and 3.2 now says only 'Resetting a stage is zeroing its state'. 'No way to be done' is overstated: placement value-initialisation (new (&s) T{}) measured 8-24 bytes, though that is itself a construct section 8 would have to log.

<details><summary>Evidence as reported (confidence: high)</summary>

audit2/embedded/st/s3.cpp against the unmodified sketch.h, arm-none-eabi-g++ 13.2 -std=c++17 -mcpu=cortex-m33 (g++ and clang++-18 hosted agree on the traits): is_trivially_default_constructible<state_of<overdrive>> = false, is_trivially_copyable = false, is_trivially_copy_assignable = false, is_standard_layout = false (a single stage's state, lowpass<2>::state, is trivial). -fstack-usage: `void reset_by_assignment() { s_voice = {}; }` 6192 bytes at -O0, 6192 at -Os, 6200 at -O2; `s_voice = od_state()` 6192/6200/6200; memset version 0 to 8 bytes but "warning: 'void* memset(void*, int, size_t)' clearing an object of type ... [-Wclass-memaccess]" under -Wall. Control: a plain aggregate `struct { std::array<float, 96000> line; std::size_t write; }` reset with `= {}` has a frame of 8 (-O0) and 0 (-Os, -O2). grep of sketch.h and demo.cpp finds no reset.

</details>

<a id="f14"></a>
### 14. flush(state&, out) is one call into one buffer sized by a run-time tail: 60 KB of output for one order-4 Butterworth at 480 Hz, and a trap in the boundary check if the buffer is a callback block

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** Section 4 (`flush(state&, out)`, `flush_output_frames()`, the zero block in the state), section 3.2 (`tail_frames()`), section 3.4 (boundary check on the caller's entry), D9, C1 (`flush` is a deliverable); first-audit findings 7 and 12 item 6
- **Severity:** major as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The flush contract was added in answer to finding 7 by carrying DspTap's shape: one call that feeds every stage its tail and writes all `flush_output_frames()` frames into `out`. For an FIR that is a few dozen frames. For a recursive stage the tail is a design-time number that grows as 1/cutoff, and the caller must own a contiguous output buffer of that size, known only at run time, so the fixed-block compile-time check cannot apply and the run-time boundary check (on in every build, D27) ends in the [[noreturn]] handler when the buffer is a normal audio block. An embedded caller therefore cannot use flush for any chain with a recursion in it; it has to feed zero blocks through process() itself, which makes the per-state zero block dead RAM. The plan also still has no saturation rule for composed tails on a 32-bit size_t (first audit, finding 12 item 6; the carried header quotes 232,710,082 frames for one stage). The sketch does not implement flush, tail_frames or the zero block at all, so this contract is not among those "as code that compiles and runs".

**Recommendation.** Make flush resumable: `flush(state&, out)` writes at most out's frames and returns the count, to be called until it returns 0, with its progress held in the (zero-valid) state; or drop it and publish `flush_input_frames()` with the rule "feed that many zero frames through process()". Either way take the zero block out of the per-instance state (one shared block, or the workspace of the first finding), state that tail_frames and its compositions saturate at a stated maximum, and write flush in the sketch before C1 freezes it.

**Second reviewer.** Built DspTap origin/main butterworth.h (bw.cpp): window_frames() float at fc 0.1 is 403/780/1554 for orders 2/4/8, at 0.01 4023/7493/14736, at 0.001 40855/75675/149085, at 1e-4 413900/766308/1508566, exactly as reported; chain.h's flush(sample* out) is one-shot and section 4 carries that shape; no flush, tail_frames or zero block in the sketch; no saturation rule in the plan (grep). I rate it minor, not major: flush is end-of-stream, a caller can feed zero blocks through process(), and making flush resumable is an additive change before C1. Same point as 44.

<details><summary>Evidence as reported (confidence: high)</summary>

DspTap origin/main include/tap/dsp/chain.h line 463 `std::size_t flush(sample* out) noexcept` is one-shot, as PLAN.md section 4 describes. I built DspTap origin/main's butterworth.h on the host (audit2/embedded/bw.cpp, g++-13 -std=c++20 -O2) and printed window_frames() for the float stage: fc 0.1: 403 / 780 / 1,554 frames at orders 2 / 4 / 8; fc 0.01: 4,023 / 7,493 / 14,736; fc 0.001: 40,855 / 75,675 / 149,085; fc 0.0001: 413,900 / 766,308 / 1,508,566. Stereo float output for order 4 at fc 0.01 is 7,493 x 2 x 4 = 59,944 bytes; at 0.001, 605,400 bytes. butterworth.h line 50 on origin/main: "the largest, order 16 at 1/2 - 1e-5: 232,710,082 frames". grep -n -i "flush\|tail_frames" on the sketch's sketch.h and demo.cpp: no implementation.

</details>

<a id="f15"></a>
### 15. is_frame_synchronous, as defined and as composed, passes a down-then-up chain (the audit's own 66/63/63 case) and cannot tell an stft from a decimator

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** D35; section 3.2 (is_frame_synchronous row); section 3.4 (Fixed blocks); section 4 ("rate_ratio and is_frame_synchronous compose"); section 9.5 last sentence; sketch.h lines 375, 422, 509, 565-568
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** The tidied definition is "a call of n frames, n a whole number of the stage's input groups, always writes n L/M. False for a stage that works in blocks". "Input group" is defined nowhere and is not a published member. By the first sentence an stft (ratio 1/hop) is frame-synchronous exactly as a decimator by 128 is; by the second it is not, and down2 also works in blocks of 2. The chain composes the flag by AND (sketch line 509). That is wrong in the direction that matters: a 1:1 chain that goes down and then up is declared frame-synchronous, so the fixed-extent entry compiles and nothing sends the caller to steady<>, yet the count varies per call. This is the very case D35 was accepted to fix (response file: "a by-3 down then up returns 66, 63, 63"). The pre-tidy definition ("false for a stage whose phase makes its output count vary") fails the other way: down2 is false, so the plan's own 9.4 overdrive chain could not use fixed blocks. Separately, 9.5 says a host whose callback is a multiple of the hop needs no steady<>, but the fixed-extent overload static_asserts is_frame_synchronous, which is false for the bare region, so that host cannot use the compile-time entry.

**Recommendation.** Publish one more constant in the stage contract at C0b: input_group, the smallest n for which every aligned call of k*n frames from the reset state writes k*n*L/M (1 for gain and up2, M for a decimator, hop for stft and for a block stage). Compose it in the chain: the smallest n such that the count arriving at each stage is a multiple of that stage's group. Then derive the flag instead of declaring it: a chain is frame-synchronous iff input_group == rate_ratio.down. That gives up|down true, down|up false and stft|istft false. The fixed-extent overload asserts N % input_group == 0 rather than the flag, which also makes 9.5's last sentence true (a 128- or 256-frame fixed block on the bare region compiles). Add compile-fail tests for down3|up3 with a 64-frame array.

**Second reviewer.** Same defect as findings 1 and 5. My own toy down3/up3 in the sketch's style passes both static_asserts and the fixed entry returns 63 63 66 63 63 66 for 64 frames in with a 66-frame output array; with a 64-frame output array it is refused, but by the room assert, not by the flag. The definition's two sentences do conflict for an stft versus a decimator, and 9.5's 'needs no steady<>' host cannot use the fixed-extent overload because the bare region's flag is false.

<details><summary>Evidence as reported (confidence: high)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/dsp/steady_test.cpp, built with g++-13 -std=c++17 against the unmodified /home/user/catenae/docs/sketches/config-state/sketch.h, adding toy down3/up3 stages in the sketch's own style (is_frame_synchronous = true "for whole groups", as the sketch's down2 at line 375). Both static_asserts pass (rate_ratio == 1:1, is_frame_synchronous true) and the fixed-block process compiles. Output: "down3 | up3, is_frame_synchronous = 1, fixed-block process of 64 frames returns: 63 63 66 63 63 66" and "down2 | up2, is_frame_synchronous = 1, fixed-block process of 5 frames returns: 4 6 4 6 4 6". No power-of-two host block is a multiple of 3, so every down3 | ... | up3 chain (process at 16 kHz inside 48 kHz) is affected. grep -n -i "input group" PLAN.md finds only line 151.

</details>

<a id="f16"></a>
### 16. D38 rests on "block floating needs an exponent to travel"; a block-floating inverse does not, and it is 32 to 45 dB better than the floor the plan is about to state

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** D38; section 5.2 (fixed-point bullet, "a Q31 one states its loss", "overall gain (1)"); section 7.2 (own fixed-point engine: "fixed scaling only"; back ends "within a stated tolerance"); C6
- **Severity:** major as reported, minor after verification
- **Verdict:** confirmed

**Problem.** D38 offers Q31 resynthesis with fixed scaling in both directions, and 7.2 ports DspTap's fixed-point engine as "fixed scaling only". The reason on record for deferring block floating is that an exponent must travel with each frame. That is true only of the forward transform. With the forward fixed (the spectrum's format stays a compile-time type, as D25/D29 need) and the inverse block-floating, the istft consumes the inverse's exponent itself, as a shift before overlap-add: nothing travels and no spectral stage sees it. The carried engine already has that inverse. The fixed/fixed floor is absolute and low-grade at long lengths: about 16-bit grade at N = 4096 for a 32-bit path, with a +0.33 LSB DC term and a 2 to 3 LSB systematic error at index 0 or 1 of every frame that "bits lost" does not describe. Also, "overall gain (1)" needs a saturating left shift of log2 N + 3 bits that the plan does not mention.

**Recommendation.** Restate D38: the forward transform is fixed scaling (format in the type); the own engine's Q31 inverse is block floating with its exponent consumed inside istft; the exponent that would travel (a block-floating forward) is what stays deferred. Change 7.2's "fixed scaling only" to carry the block-floating inverse. State the contract number as an absolute floor in dBFS per length plus the DC term, not as bits lost: about -155 / -149 / -143 dBFS for the own engine. Since CMSIS-DSP has no block floating, state its Q31 istft floor separately (expect about -122 / -110 / -98 dBFS) or do not offer Q31 resynthesis on that back end. If fixed/fixed is kept regardless, it is defensible at 256 and 1024 (20- and 18-bit grade) and marginal at 4096 (16-bit grade), and the card must say the floor is absolute. Say that restoring unity gain is a saturating shift.

**Second reviewer.** Reproduced against DspTap origin/main (q31rt.cpp, white noise, 64 blocks): fixed forward + fixed inverse loses log2 N + 3 bits with floors -122.3 / -116.7 / -110.2 / -98.2 dBFS at N = 256/512/1024/4096, level-independent (SNR 33.4 dB at -60 dBFS, N = 4096); fixed forward + block-floating inverse gives -154.5 / -152.0 / -148.7 / -142.8 dBFS with e_i 0 or 1. The exponent is consumed where the inverse runs, so the response file's reason for deferring block floating does not apply to the inverse. I did not check the DC term or per-index means. Severity lowered: the plan states its loss honestly and nothing breaks; it is a better option than D38 chose, to be decided before the fixed-point FFT interface is fixed at C4 and the engine ported at C6, and it would make the Q31 istft contract differ between the own engine and CMSIS-DSP.

<details><summary>Evidence as reported (confidence: high)</summary>

Built against DspTap origin/main headers (git archive origin/main include, g++-13 -std=c++20 -O2): q31rt.cpp, q31mix.cpp, q31bias.cpp in /tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/dsp/. Fixed forward + fixed inverse, Q31, 64 blocks each: bits down = log2 N + 3 = 11 / 12 / 13 / 15 at N = 256 / 512 / 1024 / 4096; error floor -122.3 / -116.7 / -110.2 / -98.2 dBFS rms (0.80 LSB of the returned format), the same at 0, -20, -40 and -60 dBFS for sine and white noise. SNR for a sine at 0 / -20 / -40 / -60 dBFS: N=256 119 / 99 / 79 / 59 dB; N=1024 107 / 87 / 67 / 47 dB; N=4096 95 / 75 / 55 / 35 dB. Mean error +0.33 to +0.34 LSB (-105.9 dBFS DC at 4096); largest per-index mean 2.2 to 3.2 LSB at index 0 or 1. Fixed forward + block-floating inverse, result brought straight back to Q31 (white noise): floor -154.5 / -148.7 / -142.7 dBFS at N = 256 / 1024 / 4096, level-independent (e_i = 0 or 1); SNR at -60 dBFS 90 / 84 / 78 dB against 57 / 45 / 33 dB. The first audit's N = 512 figures reproduce (113.7 / 93.7 dB sine, 111.9 / 91.9 dB noise). Not measured: the instruction cost of the inverse's per-stage headroom scans, a windowed overlap-add region, CMSIS-DSP's Q31 round trip.

</details>

<a id="f17"></a>
### 17. parallel<A, B> (D43, due at C1) has no rule for the latencies it cannot align: inexact, fractional, changed by a setter, or bursting

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** D43; section 4 (combinators); section 3.2 (latency(), has_exact_latency); D37 (a design stores its latency); D34 (a parameter is configuration); D22; C1; section 11.3 ("a dry path that must line up with a wet one")
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** "With their latencies aligned" is the whole contract, and four cases are undecided. (1) A branch with a recursive stage has only {nominal, deviation}: the sketch's low-pass reports 2.176 frames, so alignment is a rounding choice nobody has made. (2) An exact branch can be fractional (bare up2|up2|...|down2|down2 is k + 1/2, by 3 is k + 1/3), and no integer delay in the other branch aligns it. (3) Latency is a stored number in a design (D37) and a design is replaced at run time by set_design (9.2), reached through stage<I>() behind parallel's back. So the compensating delay's length is a run-time value: it cannot be a static_assert or size a state by itself (D22 needs a compile-time capacity), and nothing says when it is recomputed or what happens to the audio when it changes mid-stream. (4) A bursting branch beside a frame-synchronous one writes different counts per call. The tidy also dropped the pre-tidy D43 sentence "Their ratios must match (asserted)". The sketch contains no parallel, no latency() on a chain and no oversample, so this is not among the contracts "that compile and run".

**Recommendation.** Decide before C1 and write into D43: both branches must be frame-synchronous with equal rate_ratio (asserted); the delay capacity is a template argument (parallel<A, B, MaxDelay>) or a published static max_latency; alignment is by exact_latency() when both have it, with a check when the difference is not whole, and otherwise by a stated rounding of nominal with the residual misalignment reported by parallel's own latency() deviation; and the delay length is read from the two configurations at a defined moment (each call, or an explicit realign()) with a stated behaviour on change. Restore the ratio assertion in the text.

**Second reviewer.** D43 at HEAD is one sentence (PLAN.md line 69) plus a mention at line 234; draft 32 line 84 had 'Their ratios must match (asserted)', which is gone. sketch.h has no parallel, oversample, flush or chain latency (grep: 0 hits), nominal_latency() is a non-static member reading m_design (line 308) and set_design replaces it (line 307), so the compensating delay's length is a run-time value while D22 needs a compile-time capacity. The four undecided cases are real and parallel is due at C1. I did not re-run the reviewer's oversample.py; the fractional latencies are the plan's own (D39, 9.4).

<details><summary>Evidence as reported (confidence: high)</summary>

PLAN.md lines 69 and 234 are the only definition. git show 12849d4:PLAN.md line 84 has "Their ratios must match (asserted)", absent at HEAD. sketch.h line 308: nominal_latency() is a non-static member reading m_design; line 307: set_design replaces it; demo output "strip settled at 1.000; latency 2.176 frames (stored in the design)". grep for parallel, oversample, tail_frames, flush in sketch.h: no hits. Fractional latencies measured in oversample.py (same scratch directory) with DspTap's nyquist.h designs: x4 24.5 and 18.5 base frames, x8 26.25 / 26.75, x16 27.125 / 27.375, x3 15.333, x6 26.667 (= 80/3).

</details>

<a id="f18"></a>
### 18. steady<>'s prefill of (largest burst - 1) is not the worst backlog: two block stages whose sizes do not nest corrupt the output silently

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** D35; section 9.5 ("The FIFO starts with one frame fewer than the largest burst: that is the added latency"); sketch.h lines 651-652, 659, 681-690
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** k_burst = Inner::max_outputs_for(1) is the largest burst, but the prefill steady<> needs is the largest backlog, max over the stream of (frames in - frames out). They are equal for one block size or for nested sizes (powers of two), which is why the plan's 512/128 case works. For blockwise<a> | blockwise<b> with a < b and neither dividing the other, the backlog is a + b - gcd(a, b) - 1, larger than b - 1. The FIFO then underflows: surplus goes below -(k_burst - 1), held wraps as a size_t, the write index is wrong, and the output is wrong with every index still in bounds, so no check at any level fires. Such sizes are ordinary once a 10 ms block (480 or 441 frames, or log_mel's hop of 160) meets a power-of-two FFT hop, or an overlap-save FFT FIR sits beside an stft. No existing trait gives the right number, so the reported latency ("its true latency") is wrong for these chains too.

**Recommendation.** Size the prefill from the composed input_group of the first finding: prefill = input_group - 1 is always sufficient (the output has caught up exactly at every multiple of the group); it equals the burst for single and nested sizes and is 383 for 96|128 where 191 would do. If the tight value is wanted, publish a max_backlog constant per stage and compose it. Either way add a static_assert or audit check that surplus never goes below -prefill, and a test with non-nested block sizes at C1.

**Second reviewer.** Reproduced with the unmodified sketch.h (host.cpp): steady<blockwise<96> | blockwise<128>> has added_latency_frames 127 and 22 of 4096 frames wrong, first at 127; 480|512 has 9519 of 25600 wrong; 128-only, 64|128 and 128|96 are clean. Brute-force backlog: 96|128 = 191, 480|512 = 959, 441|512 = 951, 128|96 = 191, 64|128 = 127. No check fires because every index stays in bounds. D35 and 9.5 state the rule 'one frame fewer than the largest burst' as the plan's own, so a C1 implementation written from the plan would repeat it and report a wrong latency.

<details><summary>Evidence as reported (confidence: high)</summary>

steady_test.cpp (same scratch directory) against the unmodified sketch.h, identity block stages, a ramp in, compared with the ramp delayed by added_latency_frames: steady<blockwise<128>> at 64- and 37-frame calls: 0 frames wrong; steady<blockwise<64> | blockwise<128>>: 0 wrong; steady<blockwise<96> | blockwise<128>> (added_latency_frames = 127): 22 of 4096 frames wrong, first at frame 127; steady<blockwise<480> | blockwise<512>> (511): 9519 of 25600 wrong. Brute-force backlog over 200000 frames: 96|128 -> 191 (sketch prefill 127); 480|512 -> 959 (511); 441|512 -> 951 (511); 128|96 -> 191 (191, right by luck); 64|128 and 128|64 -> 127 (127). Capacity is sufficient whenever the prefill is. Checked separately (stft_lat.py): a 512/128 Hann region from a zero state has latency 384 bare and 511 inside a 127-frame prefill, so the plan's two numbers are right.

</details>

<a id="f19"></a>
### 19. The mailbox's post is undefined behaviour for exactly the types the plan posts through it (a design, a stage's configuration); no sanitizer sees it and section 8's deviation list has no entry for the fix

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** D44, D37, section 6.3 (`T`: trivially copyable (asserted): a design, a level, a reading), section 9.3, section 8 (no undefined behaviour relied on; the deviation list); sketch.h lines 763 and 793-796
- **Severity:** major as reported, minor after verification
- **Verdict:** confirmed

**Problem.** mailbox::post does `m_slots[m_poster].value = value;` on a `union slot { unsigned char unused; T value; }` whose active member is `unused`. Under [class.union]/5 an assignment begins the lifetime of a union member only when its type has a trivial, non-deleted default constructor. D37 makes a design 'a class, not an open aggregate' with no default constructor, and a stage's configuration has none either, so for `mailbox<lowpass_design<float>>` (the listing of 9.3) the assignment is a member call on an object that is not alive. `float` and plain structs are fine. It works today because compilers emit a copy, which is why the 2-million-post stress run, the thread sanitizer, ASan and UBSan are all clean: the verification section 6.3 and 7.3 name cannot find this class of bug. The only asserted requirement on T (trivially copyable) is the wrong one. The repair (construct into the slot with placement new) is, as I recall MISRA C++:2023, itself a deviation ('advanced memory management'), as is the `union` keyword that both `mailbox` and `expected` use (the first audit cited it as Rule 12.3.1); section 8's opening deviation list names neither.

**Recommendation.** In 6.3 state how a value enters a slot: constructed in place (placement new), never assigned; or store bytes and require T to be trivially default constructible, which D37 would then have to allow for designs. Add to C1 a compile-time test that runs post/take inside a constant expression at C++20 for a design type, since that is the only tool in the plan's CI that sees lifetime errors. Add `union` and placement new to section 8's deviation list as the two constructs `expected` and `mailbox` cannot be written without, and add `static_assert(std::atomic<std::uint8_t>::is_always_lock_free)` (it fails on Cortex-M0/M0+, passes on M3, M4, M23, M33, M55).

**Second reviewer.** Reproduced: the slot union copied verbatim and assigned in a constant expression at C++20 is accepted for float and a plain struct and rejected for lowpass_design<float> and gain<2> by g++-14 ('does not implicitly begin its lifetime because ... does not have a non-deleted trivial default constructor, use std::construct_at') and clang++-18; post/take of a design under ASan+UBSan is silent; construct_at is accepted. It is a real defect in the sketch the plan cites for its mailbox claims, but the fix is one line and the plan text does not prescribe assignment, so major is inflated. The MISRA rule content is from the reviewer's memory and unverifiable here.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch audit2/build/union/u.cpp copies the slot union verbatim and evaluates `slot<T> s{}; s.value = v;` in a constant expression at -std=c++20 (the compiler's own UB detector). T=float and T=struct{unsigned,unsigned}: accepted by g++-14 and clang++-18. T=lowpass_design<float> and T=gain<2>: g++-14 'accessing slot<catenae::lowpass_design<float>>::value member instead of initialized ...::unused member in constant expression ... does not implicitly begin its lifetime because catenae::lowpass_design<float> does not have a non-deleted trivial default constructor, use std::construct_at instead'; clang++-18 'member call on member value of union with active member unused is not allowed in a constant expression'. san.cpp (post then take of a design) under g++-13 -fsanitize=undefined,address: silent, prints the value. fix.cpp with std::construct_at(&s.value, v) is accepted as a constant expression. Rule names and numbers are from memory, not the text.

</details>

<a id="f20"></a>
### 20. A state is 'valid when all zero' but nothing says how it gets or returns to zero: reset() was dropped in the tidy, the natural spellings cost a state-sized stack frame or a -Wall error, and a state outside static storage is silently garbage

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** D34, section 3.2 (`state` row: 'Resetting a stage is zeroing its state'), section 4, section 6.3/D44 ('a new configuration reaches the audio thread this way'), section 8 (assumptions of use), section 9.7; sketch.h chain::state and std::tuple
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** Draft 32's stage and analyzer tables had `reset()`; the tidied 3.2 replaces it with a sentence and names no operation. Three consequences, all measured on the sketch. (1) The sketch's chain keeps configurations and states in std::tuple, which is not trivially copyable, so `state_of<chain>` and the chain's configuration are not either. `memset` over a chain state is rejected by -Wclass-memaccess (in -Wall, so an error under D31); `g_voice = od_state{}` builds a 6,200-byte temporary on the Cortex-M33 stack at -Os and -O2; `g_voice = {}` is free at -Os but 6,192 bytes at -O0. A reset is something an audio thread does (voice stealing, a seek), against the plan's own -Wstack-usage ceilings. (2) `mailbox<chain configuration>` does not compile (the trivially-copyable assert), so D44's 'a new configuration reaches the audio thread this way' holds for one stage's configuration or a design and not for a chain's. (3) A bare stage's state is trivially default constructible while a chain's is not, so `state_of<lowpass<2>> st;` as a local, a class member or arena memory is uninitialised: the contract's validity silently depends on storage duration. Section 8's assumptions-of-use list (threads, aliasing, flush-to-zero, handler, setter) does not include 'the integrator zeroes every state', and no check at either level can see it.

**Recommendation.** Decide before C0b and write into 3.2 and 4: (a) a state is a trivial, standard-layout aggregate and a configuration is trivially copyable, asserted by `stage_traits`, which rules std::tuple out of `chain` (a recursive aggregate instead); (b) one library function `reset(state&)` that zeroes in place, with its stack cost pinned on the embedded legs; (c) either every state member has a zero default member initialiser (still `.bss`, measured below as unchanged in kind) so that `state st;` is valid anywhere, or 'a state not in static storage must be value-initialised' becomes an identified assumption of use with a compile-fail or sanitizer test. State in 6.3 whether a chain's whole configuration may be posted.

**Second reviewer.** Same core as findings 2 and 6. Reproduced: chain configuration and state not trivially copyable; mailbox<chain configuration> fails the sketch's static_assert (mb.cpp); a non-static state_of<lowpass<2>> prints 'first sample out = inf' under clang++-18 -Weverything with no warning, g++-13 gives no uninitialised warning at -O0 or -O2 in my build; draft 32 lines 237 and 436 are the dropped reset() rows; section 8's register does not list zeroing a state. One number is the other way round for me: `= od_state{}` cost 0 bytes at -Os/-O2 and `= {}` cost 6,192/6,200, which only shows how fragile the spelling is. D44's 'a new configuration' is shown in 9.3 as a design and a level, so the chain-configuration case is a wording gap more than a broken promise.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch audit2/build/capi/triv.cpp (g++-13): lowpass<2>, gain<2>, lowpass_design<float> trivially_copyable=1; the `lowpass|gain` chain configuration, its state_of and fed_by_state: trivially_copyable=0, standard_layout=0; state_of<lowpass<2>> trivially_default_constructible=1. mb.cpp: `mailbox<strip_t>` fails with 'catenae: a mailbox carries a trivially copyable value'. reset.cpp on arm-none-eabi-g++ 13.2 -mcpu=cortex-m33 with -fstack-usage: reset by `= od_state{}` 6200 bytes (-Os, -O2); by `= {}` 0 (-Os), 6192 (-O0); memset 0 bytes but 'warning: clearing an object of type ... with no trivial copy-assignment [-Wclass-memaccess]'; placement new 24 bytes. misc/uninit.cpp: a non-static `state_of<lowpass<2>>` and `state_of<gain<2>>` fed silence prints 'first sample out = inf' when built with clang++-18 -Weverything (0 warnings); g++-13 warns only at -O2 after inlining. `git show 12849d4:PLAN.md` lines 237 and 436 are the dropped `reset()` rows.

</details>

<a id="f21"></a>
### 21. D45's C ABI needs things no build system or milestone lists: per-target sizes without running target code, an init for the configuration, a shared library, a size check the inline namespace cannot give, and a manifest that can name designs

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** D45, D46, section 3.4 (one level per image), section 7.4, section 10 (CF, C6 and the graph), section 11.1 (notebooks)
- **Severity:** major as reported, minor after verification
- **Verdict:** partly

**Problem.** I wrote one manifest entry (the `lowpass | gain` strip of 9.2) as a generated C header, a C++ definition file and a Python ctypes notebook cell. It works, and it needed the following, none of which is in sections 7 or 10. (1) 'Sizes as constants' are per target: the overdrive state is 6,192 bytes aligned 8 on x86-64 and 6,184 aligned 4 on the M33 (size_t members), and a back-end choice changes workspace sizes too. A cross build cannot run a program to print sizeof, so the header is either generated by compiling for the target and reading symbol sizes with nm (a script in CMake, in waf, and by hand for the amalgamation), or the manifest declares capacities that a static_assert checks. (2) A configuration is not valid when zero, so every entry needs `*_config_init`, plus one exported function per setter and per `make()`; C++17 cannot enumerate these, so the manifest must name each parameter. (3) extern "C" names carry no inline namespace: a C caller built against a header generated for another target, level, FFT back end or manifest links and then overruns its own storage. This is the hazard 3.4 closes for C++ and D45 reopens. (4) The dynamic `process` calls the [[noreturn]] handler on a wrong size, which from ctypes kills the Jupyter kernel; the wrapper needs its own pre-check returning a status. (5) ctypes needs a shared library, hidden visibility, an export macro and a pinned symbol list (both source libraries have exactly this under capi/); D4 and 7.4 say one static library, never prebuilt. Python cannot read a #define, so sizes must also be exported as functions or data. (6) D45's manifest lists chains and junctions, but the notebooks of 11.1 are responses and group delay: `group_delay(f)` is a design-time free function (3.3) and sweeping a cutoff needs `make()`, so the manifest must name designs as well. The layout comparison comes from benchmark output, not the C ABI. (7) C6's 'C ABI from its manifest, and the executed notebooks' has no node in the graph of 10.1 and C0a's CI has no Python or notebook-execution leg.

**Recommendation.** Add a short section 7.5 before CF writes the first manifest: the manifest's form (I would use an X-macro header that both the C header and the definitions expand, with declared capacities checked by static_assert, so no generator and no nm step is needed in three build systems); every entry exports `init(config, config_size, state, state_size)` returning a status so a stale header is caught at run time; a typed storage macro that carries alignment, not a bare size; wrappers that validate sizes and return a status instead of reaching the handler; entries for designs and design-time functions; a `catenae_c` shared target in CMake only, for tests and notebooks, with waf and the amalgamation building the same file statically. Put the shared target, the exported-symbol pin and a notebook-execution leg in C0a or C6 explicitly, and add the C ABI and notebooks to the graph.

**Second reviewer.** Checked facts hold: overdrive state is 6192 bytes align 8 on x86-64 and 6184 align 4 on the M33; strip config 32, state 548, mailboxes 76 and 16 on both; the sketch's chain configuration is not trivially copyable; both source libraries build their capi as SHARED with hidden visibility; 'manifest' appears only in D45, CF and C6 and the 10.1 graph has no C ABI or notebook node. But D45 fixes only the principle and CF's row is 'a first C ABI manifest written against them', which is where the manifest's form is meant to be found, so 'none of which is in sections 7 or 10' describes deferred detail more than a defect. The points worth a sentence now are the extern "C" names escaping the inline namespace and per-target size and alignment constants.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch audit2/build/capi: sizes.cpp compiled with g++-13 and arm-none-eabi-g++ -mcpu=cortex-m33 -Os, `nm -S` on probe arrays: overdrive state 6192/align 8 vs 6184/align 4; steady state 2072 vs 2056; fed_by state 3648 vs 3640; strip config 32, strip state 548, mailbox<design> 76 on both (matching the plan's M33 figures). catenae_c.h / catenae_c.cpp / nb.py: built with -fPIC -shared; my first attempt failed on `static_assert(std::is_trivially_copyable_v<strip_t>)` (see the previous finding), so the configuration cannot be handed to C as plain bytes either. DspTap origin/main tools/capi/CMakeLists.txt (`add_library(dsptap_capi SHARED ...)`, CXX_VISIBILITY_PRESET hidden) and sampleratetap origin/main rational/capi/CMakeLists.txt (27 pinned exported symbols, /bigobj under MSVC) show what the notebooks' path costs in the source libraries. `grep -n manifest PLAN.md` finds only D45 and the CF and C6 rows.

</details>

<a id="f22"></a>
### 22. Three gates settled in sections 1 and 7 land in no milestone, one of them rejects the plan's own example as worded, and C1's stages cannot exercise half of what C1 delivers

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** Section 10 (C0a, C1, C2, C6 rows; graph and workstream table), D36 and section 7.3 (double promotion), D47, D29, section 2 (charter), section 3.4 (`sanitize`)
- **Severity:** major as reported, minor after verification
- **Verdict:** confirmed

**Problem.** (1) D36's link-stage gate appears in D36 and 7.3 and in no milestone row; C0a's CI list has warnings, tidy, scripts, ratchet, probes and coverage but not it. As worded ('a float-profile image ... may not reference a software-double routine or a double libm entry') it rejects the image of 9.2 and 9.3, where a control path calls `lowpass_design<float>::make()`: that is the plan's supported way to retune at run time. The gate has to be scoped to images with table designs only, or to the audio path, and the plan does not say how an image is split. (2) D47's 'CI instantiates its contract for q31' is in no milestone row. The tidy dropped draft 32's qualifier '(not its arithmetic)', without which it is unachievable for the stages D47 itself defers to C6; and it collides with D29 ('a stage accepts only the formats its battery pins') with no mechanism named for instantiating the contract of a format the stage must refuse. The sketch's `gain`, `lowpass`, `map` are hard-coded `port<float, Ch>`, the opposite of 'written against the traits', so nothing has tried this. (3) D47's tidied list says fixed point arrives early 'where the carried code has it (... convert, unpack, pack)'. Neither library has those; draft 32 said they are 'the format stages that exist only because of it'. So C1's `convert` among float, q15 and q31 is new numeric design (scale, rounding, saturation at +1.0) with no decision behind it. (4) C1's stages (map, gain, delay, interleave, deinterleave, convert) are all 1:1 and frame-synchronous, and only delay has a tail. `steady` needs a bursting inner stage, `fed_by`'s holdover needs `max_outputs_for(1) > 1`, state-dependent `outputs_for`/`frames_needed`, the 'fixed block needs a frame-synchronous chain' compile-fail test and `flush` across a rate change all need stages that arrive at C3 or C5. The sketch uses toys (`blockwise`, `up2`, `down2`) for exactly this; C1 does not list test fixtures. (5) Smaller gaps between tables: `sanitize` (3.4) and the charter's 'dynamics without a side chain' are in no milestone; the graph's C4 FFT FIR node lacks the Kaiser design it needs to be tested; C6's fixed-point spectra have no edge from the stft node or the fixed-point engine.

**Recommendation.** C0a: add the link-stage gate to the harness, defined as 'the linked, garbage-collected ELF of each float probe that uses table designs only contains none of a listed set of symbols', and reword D36 and 7.3 to that scope; state the flash cost of a run-time designer (about 8.3 KB here) where 9.2 quotes its measurements. C1: add 'every stage is a template on its sample format; a CI target instantiates `stage_traits` (declarations only) for q31', restore the lost qualifier in D47, and say how that coexists with D29 (for example `is_stage` false until the battery pins the format). Fix D47's parenthesis and add a decision for `convert`'s numeric contract. C1: name the test-only fixture stages (a block stage, a toy 1:2 and 2:1) as deliverables. Place `sanitize` and a dynamics stage in a milestone or take them out of sections 3.4 and 2.

**Second reviewer.** Checked each part. 'link-stage' appears only at D36 and 7.3 and in no milestone row; linking the sketch for cortex-m33 with --gc-sections gives 404 bytes of text and no double symbol from the ROM design alone, 8,708 bytes and 17 soft-double/libm symbols once set_cutoff is reachable, so the gate as worded rejects 9.2's own image. Draft 32 does say 'instantiates each stage's contract (not its arithmetic)' and 'the format stages that exist only because of it (convert, unpack, pack)'; DspTap origin/main has no unpack/pack/convert stage. C1 lists no bursting or rate-changing fixture although steady, fed_by's holdover and flush need one; `sanitize` and dynamics are in no milestone. All true, but each is a scoping or placement fix of a sentence or a row, so I rate the bundle minor.

<details><summary>Evidence as reported (confidence: high)</summary>

`grep -n 'link-stage' PLAN.md`: lines 76 (D36) and 419-421 (7.3) only. Scratch audit2/build/gate: embedded.cpp + embedded_design.cpp from the sketch linked for cortex-m33 at -Os with --gc-sections. With the ROM design only: no `__aeabi_d*` or libm symbol in the ELF, text 1,552 bytes. With one `set_cutoff(0.02)` call reachable: 24 such symbols (__aeabi_dmul, __aeabi_ddiv, __aeabi_dadd, __aeabi_f2d, __aeabi_d2f, tan, __kernel_tan, __ieee754_rem_pio2, ...), text 9,888 bytes. Draft 32 (`git show 12849d4:PLAN.md` lines 1296-1309) has 'CI instantiates each stage's contract (not its arithmetic) for q31' and 'the format stages that exist only because of it (convert, unpack, pack)'. `git grep -i 'unpack\|s24\|byte order' origin/main -- include` in DspTap finds only pvoc.h (phase unwrapping) and nothing in sampleratetap's engine headers; sample_traits.h has coefficient and accumulator conversions, no sample-format converter.

</details>

<a id="f23"></a>
### 23. fed_by pulls a junction in sub-blocks; the real async converter never locks that way, and underruns on every callback above the block size

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** Section 6.1 (fed_by, fed_by_state 'frames in transit', pull(out)); D19, D35, D16; sketch.h fed_by (line 836); CF
- **Severity:** major as reported, major after verification
- **Verdict:** confirmed

**Problem.** fed_by takes the frames a chain needs from the junction a block at a time (staging is one block: sketch.h `n = min(frames_needed(want - done), k_block)`, k_block = 64). The junction contract says nothing about how many pull() calls one caller request may become. For handoff it does not matter. For the asynchronous converter it is decisive: its servo is clocked by pull() (dt = frames / rate per call), it samples occupancy once per pull, and it raises its setpoint from the largest pull it has seen. Split one callback into several pulls and the occupancy it observes is a sawtooth of the sub-block size, far above its 1-frame lock threshold, and the setpoint adapts to 64, not to the callback. So D16's async junction does not work behind fed_by for any request needing more than one block of chain input (a 128-frame callback, or 128 frames through bridge-down = 139/140 input frames).

**Recommendation.** Add to the junction contract what a caller's request is: for clocks::independent, fed_by makes exactly one junction pull per call. That makes fed_by's staging a compile-time maximum request (fed_by_state<Chain, MaxFrames> or the chain's with_block), with a boundary check on the request. The alternative is a junction member that marks the request once (servo update, setpoint observation) apart from taking frames. Either is a change to junction_traits and to fed_by as C1 builds them. Make CF's async skeleton carry the real servo and assert 'locked, zero underruns' through fed_by with a request larger than the block.

**Second reviewer.** Duplicate of finding 9, reproduced with the real converter at default config, +200 ppm, 120 s: callback 128 whole locks with target 192, in pulls of <= 64 it stays in acquiring with target 96; callback 240 whole locks with target 341, in pulls of <= 64 gives 24,000 underruns and 79.99 % real. Callback 96 as 64 + 32 also stays in acquiring. converter.h updates the servo once per pull with dt = frames / rate. I did not re-run the hand-raised-setpoint variant.

<details><summary>Evidence as reported (confidence: high)</summary>

Built SampleRateTap origin/main's async converter (git archive to scratch, g++-13 -std=c++20 -O2) in scratch/audit2/fit/subpull.cpp: mono float, default config, +200 ppm, 120 s, callback of CB frames taken either as one pull(CB) or as pulls of at most 64 (what fed_by does). Output: 'callback 128, pulls of <= 128: state 2 (locked), underruns 0, effective target 192' against 'callback 128, pulls of <= 64: state 1 (acquiring), effective target 96'; 'callback 240, pulls of <= 240: locked, underruns 0, effective target 341' against 'callback 240, pulls of <= 64: state 0, underruns 24000, real 80.02 %' (one underrun per callback). With the setpoint raised by hand (target 341 or 192) the sub-pulled runs have no underruns but still never leave acquiring in 120 s; even callback 96 = 64 + 32 stays in acquiring. My lm_fit.cpp skeleton confirms the sketch's fed_by asks its source for 'at most 64 frames' per pull when asked for up to 700. Source: async/include/tap/sr/async/converter.h lines 344 (observed max pull), 399 to 400 (dt and servo update per pull).

</details>

<a id="f24"></a>
### 24. The junction contract has no configuration/state split, so a configured junction (async) lands in .data, or the C1 signatures change

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** D34, D19, D37, D44; section 6.1 table (push(in), pull(out), report(), reset()); feeding / fed_by signatures; 9.6; sketch.h handoff
- **Severity:** major as reported, minor after verification
- **Verdict:** partly

**Problem.** D34 is stated for stages. A junction in 6.1 and in the sketch is one object with push(in) and pull(out): its ring, its atomics and any configuration are members of the same object. handoff has no configuration, so it is all zero and sits in .bss. The asynchronous converter has one (a view of its bank, a target fill, servo constants: run-time values in SampleRateTap today, a design under D37), so as one object it is initialised data: ring and histories copied into flash, which is the problem D34 was accepted to remove. Splitting it changes push/pull/report to take a state, and changes handoff, feeding and fed_by, all built at C1 before CF looks. Three things follow that the plan does not say: (1) a junction's configuration is read by two threads, so D44's 'the audio thread applies it at the top of a call' has no single audio thread to rely on; (2) 'resetting is zeroing its state' cannot be an assignment for a state holding atomics; (3) async's all-zero state needs its non-zero start values re-encoded (see checked_ok: feasible).

**Recommendation.** Decide now, in section 1, one of: (a) a junction is a configuration plus a state like a stage (`push(state&, in) const`, `pull(state&, out) const`, `report(const state&)`), handoff's configuration being empty; or (b) a junction is one all-zero object configured only by its type (profile, capacity and target fill as template arguments, servo constants as a tag type), with no run-time design. State that a junction's configuration does not change while either side runs, and define reset for a state with atomics as member-wise stores. Write handoff, feeding and fed_by at C1 in the chosen form.

**Second reviewer.** Facts hold: on cortex-m33 the sketch's handoff<2,1024> is 8,200 bytes of .bss and the same ring with a pointer and a target set by a constexpr constructor is 9,104 bytes of .data (szs.cpp); async's converter.h holds m_cfg, m_bank, m_ring, m_servo and non-zero start values in one object. But the title's either/or is overstated: 6.1 already makes capacity a template argument and 3.3 makes a storage-sizing profile one, so a type-configured all-zero junction fits the contract with no signature change (the finding's own option b), the junction keeps a reset() member so 'zeroing by assignment' does not arise, and CF is the plan's stated place to settle async's shape. What is missing is one sentence saying D34 applies to junctions and which form.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch/audit2/fit/junction_m33.cpp, arm-none-eabi-g++ 13.2 -std=c++17 -Os -mcpu=cortex-m33: the sketch's `static handoff<2, 1024>` is `.bss._ZL6s_link 8200`; the same ring plus a 224-float history inside a class with two configuration members (a `const float*` to the bank and a target) built by a constexpr constructor is `.data._ZL6s_link 9104` (9,104 bytes of flash and of RAM). atom.cpp: `s_state = {}` on a struct with std::atomic members fails on g++-13 ('use of deleted function ... operator='); member-wise stores compile. Source: async converter.h lines 198 to 244 hold m_cfg, m_bank, m_ring, m_servo and the atomics in one object; nothing in its API changes the config after construction.

</details>

<a id="f25"></a>
### 25. D37 on rational: the design divisor sizes storage too and belongs to the chain, so up2<2> | up2<2> cannot be SampleRateTap's chain; custom and searched designs have no form

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** D37 and section 3.3 ('Where a profile fixes a tap count that sizes storage, the profile is a template argument'); 9.4 (`up2<2>{} | up2<2>{} | ...`, 'latency 18.5 and 23.5'); C3 (up2/down2, the L/M machine, oversample); C7
- **Severity:** major as reported, minor after verification
- **Verdict:** confirmed

**Problem.** In rational a stage's tap count is fixed by three things: its ratio, the profile, and its design divisor (its lower rate over the chain's lowest rate), which basic_chain computes and passes as profile.relaxed(divisor). Under D34 the history length is in the stage's type, and `a | b | c` composes independent configurations, so a stage cannot learn its divisor from the chain. The type must carry profile and divisor. The plan shows `up2<2>{}` with neither, and C3 ships up2/down2/oversample 'with one pinned design' while two identical types necessarily have the same length. The latencies 9.4 quotes (18.5, 23.5) are those of the relaxed chains; with the inner pair unrelaxed the same chain is 28.5 frames and about 47 % more multiplies. Separately, D37's two table forms do not cover a tap count known only after make(): rational's custom profile and any divisor without a pin are searched at run time (also reachable through the C ABI's create_stage), and bridge's custom profile carries its own tap count; nothing says how the state is sized (a maximum, or a count the caller supplies as a template argument after a host-side search).

**Recommendation.** Settle the rate-changing stage's signature before C3: `stage<S, Ch, Ratio, Design>` where Design is a type that fixes the tap count and names the table (generated ones per profile and divisor; `custom<Taps>` filled by make(spec, table, workspace)). Named chains become alias templates that compute each stage's divisor (plain constexpr in C++17). Show the relaxed inner pair in 9.4 and have oversample use it. Add a sentence to D37 for a searched design: the search is a host tool, its result a template argument; and list in D16 what is dropped (run-time profile objects, run-time search, create_stage at an unpinned divisor).

**Second reviewer.** Source read on origin/main: chain.h:169 builds each stage from p.relaxed(k_divisors[I]); design.h pins economy at 1/1 as m = 10 and at 2/1 as 5 or 4, and a custom profile or unpinned divisor is searched at run time. The arithmetic checks: N = 39 outer and 19 inner gives 9.5 + 2.25 + 2.25 + 9.5 = 23.5 (super_economy 18.5), unrelaxed 28.5, and 86 against 126 multiplies, so the plan's own quoted latencies are those of chains whose two up2 stages differ in length. I did not re-run count.py (124 stage types). Major is inflated: CF includes one rational chain and runs beside C3 precisely to force such signature changes before the freeze at C9.

<details><summary>Evidence as reported (confidence: high)</summary>

Read origin/main: rational/chain.h line 169 `basic_stage<S, Ch, Rs>(p.relaxed(k_divisors[I]))...`; design.h line 54 economy at divisor 2 pins m = {5,4,4,5,5} against 10 at divisor 1, lines 121 and 167 'a custom profile ... is searched', 'all 0, a search, when there is none'; stage.h line 375 history = row length + 64. Arithmetic from the pins: up2|up2|down2|down2 at economy, N = 39 outer and 19 inner, latency 9.5 + 2.25 + 2.25 + 9.5 = 23.5 (the plan's number; 18.5 at super_economy likewise); unrelaxed 9.5 + 4.75 + 4.75 + 9.5 = 28.5; multiplies per base frame 2 x (21 + 2 x 11) = 86 against 2 x (21 + 2 x 21) = 126. scratch/audit2/fit/count.py over the 33 chains of the C ABI and the four pin tables: 124 distinct (ratio, profile, divisor) stage types and 132 chain types per sample format and channel count (16 and 33 today, the profile being a run-time object), 73 distinct coefficient tables per format holding 4,529 coefficients (18 KB in float), no unpinned divisor.

</details>

<a id="f26"></a>
### 26. CF runs beside the work it protects and has no quality bar, so it cannot find these; the three contract changes it would force are known now

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** Section 10 (CF row), 10.1 ('CF follows C1 at once and runs beside the stage work'; workstream table), C1, C3; first-audit finding 34
- **Severity:** major as reported, minor after verification
- **Verdict:** partly

**Problem.** The first audit asked for a fit milestone 'after C1 and before C2 to C6 harden the contract'. Draft 32's CF row ended 'changed here, before C2 to C6 build on them'; the tidy dropped that clause and kept 10.1's 'runs beside the stage work'. So C3 builds up2/down2, the L/M machine and oversample while CF is still deciding what a rate-changing stage's template arguments are, and C1 has already built handoff, feeding and fed_by on the junction contract CF will change. CF's skeletons have 'no quality bar on the signal processing': a toy servo passes through a sub-blocking fed_by, so the first finding above would surface at C8, one milestone before the freeze.

**Recommendation.** Make the three changes now, before C0b: (1) junction_traits: configuration apart from state or type-only configuration, immutable while either side runs, plus a source-side reset; (2) fed_by: one junction pull per request for an independent-clock junction, with a compile-time maximum request, and accept any source with pull() so SampleRateTap's pull(out, k, pop) has a home; (3) rate-changing stages take their design (profile and divisor, or custom<Taps>) as a type. Then let CF gate C3's rate-changing stages and everything built on a junction, and require its async skeleton to carry the real servo with a lock test.

**Second reviewer.** The clause 'before C2 to C6 build on them' is in draft 32's CF row (line 1007) and gone from the new one; 'runs beside the stage work' was already in draft 32 (line 1022), so the two disagreed before the tidy and the tidy kept the weaker one; the graph gates only C7/C8 on CF. That part is real. The rest rests on other findings ('the three contract changes known now', the sub-blocking fed_by) that are not established here, and CF still says 'what it forces on the contracts is changed here', so the consequence is some rework risk during a short overlap, not a failure to find the problems.

<details><summary>Evidence as reported (confidence: high)</summary>

git show 12849d4:PLAN.md line 1007 against PLAN.md line 757 (clause removed); PLAN.md lines 769 to 770 and 803 to 807; C3 row line 759. The first three findings each change a C0b/C1 header or a C3 type and were found by reading the engines and running their code for a few minutes each, which is the work CF describes.

</details>

<a id="f27"></a>
### 27. The new plan claims more for the sketch than it contains, and one quoted measurement no longer reproduces

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** Preamble table ("The contracts of sections 3 to 6 as code that compiles and runs"), section 9 opening sentence, section 9.2 (the 20-byte stack frame), section 9.3
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The old plan said D34, D35 and D37 were compiled, that 9.1 to 9.3 and the pull side of 9.5 were excerpts, and made no claim for the memory section. The tidy widened this twice: the preamble says the sketch is "the contracts of sections 3 to 6 as code", and section 9 says "All of this is in docs/sketches/config-state/ ... except 9.5 and the edge side of 9.6". Most of sections 3 to 6 is not in the sketch, and neither is 9.7 or the feeding side of 9.6. A reader planning C0b will take latency, tail, flush, in-place sharing, planar ports, the check levels, parallel and oversample as tried in the configuration-and-state form; none has been. The sketch's own README still says "nothing here is a contract" and cites "PLAN.md 9.3" for what is now 9.4. Separately, 9.2 quotes "its callback's stack frame 20 bytes" as a Cortex-M33 measurement; the committed sketch gives 32, and the mailbox form of 9.3 gives 48.

**Recommendation.** Say in the preamble what the sketch covers (configuration and state, designs, steady, the exact pull, the mailbox, on one sample type and one layout) and list in section 9 what is uncompiled: 9.5, both chains and `feeding` of 9.6, and 9.7. Correct 20 to 32 and add the 48 for the mailbox form, or regenerate the numbers from measure.sh in the same commit that changes the sketch. If the sketch is to stand for sections 3 to 6, extend it first with latency/tail/flush, one planar stage and parallel (see the previous finding).

**Second reviewer.** Ran measure.sh on a scratch copy of HEAD's sketch: s_strip 32, s_strip_state 548, s_overdrive 6184, mailboxes 76 and 16, audio_callback 64 as quoted, but strip_callback is 32 bytes (48 with the mailbox), not 20; the sketch at 38c0c82 gives 20 and at 2f92932 gives 32. The sketch has no steadied, feeding, delay<, fir<, fft_plan, flush, parallel, oversample, planar or stage_traits, while the preamble says it is 'the contracts of sections 3 to 6 as code' and section 9 excepts only 9.5 and the edge side of 9.6. The sketch README still says 'nothing here is a contract' and calls the overdrive chain 'PLAN.md 9.3'.

<details><summary>Evidence as reported (confidence: high)</summary>

grep over /home/user/catenae/docs/sketches/config-state/*.{h,cpp} for steadied, feeding, delay<, fir<, fft_plan, flush, parallel, oversample, planar, spectrum, wire, tail_frames, is_in_place, has_exact, with_block, stage_traits, report( : no hits (only nominal_latency, added_latency_frames and starved appear). sketch.h has one port template (interleaved), std::array as the only fixed-block overload, a handoff with push and pull only, fed_by but no feeding, no joint<>, no check level. Measurement: copied the directory to scratch and ran `bash measure.sh <scratch>/sk/out` (arm-none-eabi-g++ 13.2.1): s_strip 32 .data, s_strip_state 548 .bss, s_overdrive 6184 .bss, mailboxes 76 and 16, audio_callback 64 bytes: all as quoted; but strip_callback 32 bytes and strip_callback_with_mail 48 bytes. Building the sketch as of commit 38c0c82 gives 20; from 2f92932 (the mailbox commit) on it is 32.

</details>

<a id="f28"></a>
### 28. Contract details and porting facts that were in draft 32 and are now in neither PLAN.md nor the response file

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** Sections 3.2, 5.1, 5.2, 6.1, 7.2, 7.3, 9.6, 12; D36, D37, D39; the preamble's "settled" sentence
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** Each is small; together they are what an implementer would have to rediscover. (1) fed_by's underrun report: old 9.5 said `s_from_link.starved` counts the input frames the junction supplied as silence. The new 6.1 and 9.6 name no way for fed_by to report an underrun, and `got` in 9.6 is always the frames asked for, although 3.4 says the count is the only sign. (2) planar_section: "de-interleaves once per block, runs mono stages over contiguous channels, re-interleaves, and presents itself as an interleaved stage" is gone; D7 and section 4 no longer say whether it takes mono stages or planar-port stages. (3) run's arguments: "spans of the ports' value types" and, for planar, "an array of Ch spans" were cut. (4) D39: "bare up2/down2 beyond 2x are documented as fractionally delayed" and that oversample's fix is one frame at the inner rate. (5) The own FFT engine does not produce D25's frame either: DspTap's is exp(+i) with the Nyquist real part at index 1, so the port conjugates and unpacks two bins and DspTap's fingerprint pins do not carry. 5.2 and 7.2 now list that work only for CMSIS-DSP and vDSP, and section 12's "not mechanical in five places" omits it. (6) D36: which profiles stay bit for bit (double, Q15, Q31). (7) D37: "so only make() and the table generator can produce one", while 9.2 keeps a public from_table. (8) 7.3: the Tap clang-tidy rule requiring k_ on constants is switched off (first-audit finding 59). (9) Statuses: the preamble says every decision is "the maintainer's, settled"; the old table distinguished settled from accepted ("stays open to change") and proposed, and marked D46's "no print edition" and the notebooks' timing as "this draft's reading". Section 0 (why a new library), the headroom rationale for Q4.27 and the reasons for D47 and D48 are likewise only in git history.

**Recommendation.** Put (1) to (5) and (8) back as one line each where the contract is stated (6.1 and 9.6; D7 or 5.1; the run row of 3.2; D39; a sixth bullet in section 12 for the FFT's sign and packing with "new bit pins"; 7.3). For (7) either make from_table reachable only by the generator or say that a table is trusted and pinned by its bit pin. For (9) add a short status table to the response file (settled, accepted, the draft's reading) and move section 0 and the D47/D48 reasoning there, so the preamble's "one home per fact" holds without git archaeology.

**Second reviewer.** My greps match for most items: 'starved', 'mono stages', 'conjugat', 'exp(+i)', 'switched off', 'spans of the ports' and 'Why a new' are in draft 32 and in neither the new plan nor the response; fed_by in sketch.h returns `want` and counts st.starved; DspTap fft.h line 13 says exp(+i) and line 358 puts Nyquist at data[1] while 5.2 specifies exp(-i) and section 12's five places omit it; the old status column and D46's 'this draft's reading' are as described. Overstated: items 4, 6 and 7 (fractional delay beyond 2x, the bit-for-bit profiles, 'only make() and the table generator') are in the response file's D39, D36 and D37 rows, as the finding's own counts show, so 'in neither' is wrong for those.

<details><summary>Evidence as reported (confidence: high)</summary>

grep -c -F in new plan / response / old plan (git show 12849d4:PLAN.md): 'starved' 0/0/1; 'mono stages' 0/0/1; 'fractionally' 0/1/1; 'conjugat' 0/0/1; 'exp(+i)' 0/0/1; 'switched off' 0/0/1; 'Why a new' 0/0/1. sketch.h line 850: fed_by returns `want`; line 838: `st.starved += n - real`. DspTap convention verified: `git -C /home/user/DspTap grep -n -E 'exp\(\+i|Nyquist' origin/main -- include/tap/dsp/fft.h` shows line 13 "packing, exp(+i), unnormalized inverse" and line 358 "bin[N/2].real = data[1]". Old statuses: 12849d4 lines 38-44 and the Status column (D46: line 87). The response file's D34 to D39 rows are byte-identical to the old plan's (cmp), so what it does hold is faithful.

</details>

<a id="f29"></a>
### 29. The layout numbers the tidy added to section 5.1 misquote the spike's tables

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** Section 5.1 (second paragraph), D5
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The old plan carried no layout numbers in 5.1; the tidy added a summary as D5's evidence. Two of its three statements are not what the source says. "the FFT FIR, 1.04 to 1.16, interleaved cheaper on every core": on the soft-float M4, a core the family's CI runs, the ratio is 1.002 to 1.006 ("no difference, within 1 %" in the source). "a stateless gain, 0.28 to 0.88 on x86-64 and the M55, planar cheaper where the vector unit is wide": the M55 at 2 channels is 1.248, interleaved cheaper, the exception the source calls out; and the scalar FPU cores, where planar costs 1.30 to 1.40 times at 6 and 12 channels, are left out. Stereo on the M55 is the commonest embedded case, so the summary points the wrong way for it. The pointer "docs/audits/ has the provenance" leads to a sentence naming a branch and three CI runs in another repository; the numbers themselves have no home in catenae, against rule 1 of 11.2.

**Recommendation.** Rewrite the sentence from the tables: FFT FIR 1.04 to 1.16 on the cores with an FPU and Hexagon, no difference on the soft-float M4; gain 0.28 to 0.52 at 6 and 12 channels on the M55 and x86-64, 1.25 for stereo on the M55, 1.30 to 1.40 on the M4F and M33. Commit the three runs' tables (or their layout JSON) under docs/audits/ with their commits, since D3 says the spike branch is a source and not a dependency and it may not outlive the port.

**Second reviewer.** Read the spike branch's bench/README.md: the fir row for m4-softfp is 1.002 / 1.006 / 1.004 ('no difference, within 1 %'), outside the plan's '1.04 to 1.16 ... on every core'; the gain row for m55 at 2 channels is 1.248 (interleaved cheaper), outside '0.28 to 0.88 on x86-64 and the M55', and m4f/m33 at 6 and 12 channels are 1.301 / 1.403, unmentioned. The '12 times' and 'under 2 %' statements match the source. The numbers themselves are not committed in catenae; docs/audits/ only names the branch and runs.

<details><summary>Evidence as reported (confidence: high)</summary>

`git -C /home/user/DspTap show ccr-ef01ff18-nxz8n4:bench/README.md` lines 513-520: `fir` row m4-softfp 1.002 / 1.006 / 1.004; m4f 1.052-1.078; m33 1.051-1.074; m55 1.056-1.097; m55-ooura 1.040-1.064; hexagon 1.137-1.162. Lines 550-558: `gain` host 0.878 / 0.471 / 0.515; m4f and m33 1.012 / 1.301 / 1.403; m55 1.248 / 0.276 / 0.291; hexagon 1.008-1.014. Lines 563-570 state the stereo exception and "up to 12 times on the M55 at 2 channels", "within 2 %" (those two are quoted correctly). `git log -S'0.28 to 0.88' -- PLAN.md` shows the sentence first appears in 9f7eabb.

</details>

<a id="f30"></a>
### 30. Milestones, the parallel-work graph and the documentation plan disagree in small ways after the rewrite

- **Lens:** tidy (Nothing lost, nothing contradicted by the tidy)
- **Where:** Section 10 (C0a, C0b, C1, C6), section 10.1 (graph and table), section 11.4, sections 6.2, 7.1, 7.2, 9.5 to 9.7
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** (1) 11.4 now gives C0b "the stage, junction, analyzer and mailbox contract pages", but the mailbox is C1 code, C0b's contents do not list it, and 11.4 ends "A page is written with its code, in the same change". (2) Four of the nine guide chapters of 11.3 (what arithmetic costs, rate and latency, bursts and buffering, measuring) have no milestone in 11.4; steady, the subject of "bursts and buffering", is C1 code but not among C1's chapters. (3) Gates the plan relies on are in no milestone: the link-stage gate of D36 (it needs a float image, so C1 or C3), the raw-shift script and the suppression count of 7.3, the @req trace script and length budgets of 11.2, and the table generator that section 12 calls "a host tool in the build" (first needed by C3's designs and C4's fft_tables). C0a names only "the naming and guard scripts". (4) The graph: node Q (C6 fixed-point profiles) hangs only from S, though D47's C6 set includes gain and map (C1) and the spectral stages (T); fixed-point spectra need both T and X and have no node; the C ABI and notebooks (C6) and their dependence on CF's manifest are not drawn. (5) 7.1 says a compiled FFT "needs no ABI tag on every class that embeds it", while 3.4 and 7.2 spell the FFT choices into the inline namespace that tags every symbol. (6) Names used two ways: `tap<Analyzer>` (D11, 6.2) and `tap<1, rms_meter>` (9.6); `handoff<S, Ch, Capacity>` (6.1) and `handoff<2, 1024>` (9.6); `steady<Config>` (section 4) and an undefined `steadied(gate)` (9.5); `delay<Ch, MaxFrames>` whose state is `std::array<S, ...>` (9.7). The `basic_<name><S>` pattern behind D24's suffixes is no longer stated anywhere. (7) 3.4's boundary row applies "the output holds outputs_for(n) frames" to pull, which always fills out. None blocks a milestone; each will be asked about at C0a or C0b.

**Recommendation.** Move the mailbox page to C1 (or its header to C0b); give each guide chapter a milestone; add a line to C0a for every script and gate the plan relies on, with the link-stage gate at the first milestone that has a float image and the generator at C3; add the T and C1 edges into Q, a node for fixed-point spectra, and one for the C ABI fed by CF; reword 7.1 to "no per-class tag beyond the library-wide inline namespace"; settle each name once (the alias form of D24 with its basic_ template shown once in section 9).

**Second reviewer.** Read each pair of places: 11.4 gives C0b the mailbox contract page while C0b's contents list no mailbox; tap<Analyzer> (6.2) against tap<1, rms_meter> (9.6); handoff<S, Ch, Capacity> (6.1) against handoff<2, 1024> (9.6); steady<Config> (section 4) against steadied(gate) (9.5); the graph's Q hangs only from S; 3.4's boundary row applies outputs_for(n) to pull; the link-stage gate, raw-shift script and generator are in no milestone row. Two items are a little overstated: the four unplaced guide chapters fall under 11.4's 'C2 to C5: a chapter or page as its code lands', and 7.1's 'no ABI tag on every class' does not contradict a library-wide inline namespace, it only reads badly beside it.

<details><summary>Evidence as reported (confidence: high)</summary>

Read against each other: PLAN.md lines 754-756 (C0a, C0b, C1), 762 (C6), 772-807 (graph and table), 904-929 (11.3, 11.4), 353-354 against 211-213 and 386-387, 321 against 726, 297 against 720, 234 against 692, 739. All 48 decision numbers are present once and every "section N" reference resolves (python check over PLAN.md), so the cross-references themselves are sound; these are disagreements of content.

</details>

<a id="f31"></a>
### 31. The sketch's joint proof is never instantiated: a 2-channel stage joined to a 1-channel stage compiles on all three compilers and overruns the caller's buffer

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md D27, section 3.4 (first row), section 9.5 ('What does not compile'); sketch.h:481-491
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The joint static_assert lives in joint<I>(), reached only through the initialiser of the private static member k_joints_ok, which nothing uses. A class template's static data member initialiser is instantiated only when the member is needed, so the assert never runs. The sketch therefore does not show that joints are proven at compile time, and nothing else catches a channel-count mismatch because both sides are span<float>. The same trap waits for stage_traits<T> ('one static_assert per missing or ill-typed member') if its asserts sit in members that are not instantiated. C1's compile-fail battery would find it, so the cost is small; but the plan cites this file as the contracts 'as code that compiles and runs'.

**Recommendation.** Fix the sketch (assert in the class body, or instantiate joint<I, Out, In> as a base class or in a member typedef that the class definition requires). In section 3.2 say that joint<> and stage_traits<> are instantiated by the chain's class definition itself, not by a member that may go unused, and keep one compile-fail test per joint message in C1 as planned.

**Second reviewer.** Reproduced: map<2> | map<1> compiles with zero diagnostics on g++-13, g++-14 and clang++-18 at C++17 and C++23 with -Wall -Wextra, and the fixed entry with array<8> in / array<4> out gives an ASan stack-buffer-overflow WRITE in map<1>::run via chain::step<1>. Adding static_assert(k_joints_ok) after line 491 makes it fail with the joint message. The plan cites this sketch as its compiled contracts, so the joint proof is not actually demonstrated there; C1's compile-fail battery would catch it.

<details><summary>Evidence as reported (confidence: high)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/sketch/t/joint.cpp: 'constexpr auto bad = map<2, ident>{} | map<1, ident>{}' compiles with no diagnostic under g++-13, g++-14 and clang++-18 at C++17 and C++23 with -Wall -Wextra, and process() returns 8 'frames' for 4 stereo frames. t/joint2.cpp: with std::array<float,8> in and std::array<float,4> out the fixed-extent static_asserts pass (4 >= max_outputs_for(4) * 1) and ASan reports 'stack-buffer-overflow ... WRITE of size 4' in map<1>::run called from chain::step<1> (sketch.h:113, 580). Adding 'static_assert(k_joints_ok, ...)' after line 491 makes the same program fail with 'catenae: one stage's output port is not the next stage's input port'.

</details>

<a id="f32"></a>
### 32. 'The contracts of sections 3 to 6 as code that compiles and runs' overstates the sketch: about half the named contract members are absent, and three of the absences hide costs

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md opening table (line 16); section 9 opening sentence; sections 3.2, 3.3, 3.4, 4, 6.1, 6.2; sketch README
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** Present in sketch.h: port<float, Ch> only, rate_ratio, state, is_frame_synchronous, run, outputs_for, frames_needed, max_outputs_for, setters, is_stage, chain with dynamic and std::array process, stage<I>(), steady, handoff push and pull, fed_by, mailbox, a four-member expected. Absent: stage_traits and junction_traits; joint<Index, Out, In> with one message per difference; is_in_place and buffer sharing; has_exact_latency, latency(), exact_latency(); tail_frames(), flush_residue(), flush(), flush_output_frames(); with_block (k_block is one global that chain, steady and fed_by all read, so a per-chain block length changes all three); process for T(&)[N] and a fixed-extent span (the sketch's span has no extent); planar, spectrum, wire and fixed<> ports; parallel, oversample, planar_section; feeding and feeding_state; clocking, report(), latency(), reset() of a junction; every analyzer member and tap; expected's operator->, value(), value_or(); the audit level and the inline namespace. Section 9 excepts only 9.5 and 'the edge side of 9.6', but 9.6's feeding, mix, convert and 9.7's delay, fir and fft_plan are not there either. Hidden costs: (1) the flush zero block 'in the state' is RAM per state instance, not shared: 64 frames x 2 channels x 4 bytes = 512 bytes, which takes the section 9.2 strip's state from 548 to 1,060 bytes per voice and is not in the 9.4 table; a single shared zero block would cost it once per image. (2) is_in_place 'declarable when the ports match and the ratio is 1:1' must also require is_frame_synchronous: a 1:1 block stage writes 128 frames over input it has not read yet. (3) A chain's own is_in_place is not the conjunction of its stages' (a chain of two or more frame-synchronous 1:1 stages is in place whatever they declare, because scratch lies between). Also one number does not reproduce: the strip callback's stack frame is quoted as 20 bytes in 9.2 and the README, and measure.sh at HEAD prints 32.

**Recommendation.** Change the two sentences to say what the sketch shows (D34's split and where memory lands, D35's steady and exact pull, a design, the mailbox) and list what it does not. Before C0b, either extend the sketch with latency(), flush, is_in_place sharing and with_block, or mark those rows of 3.2 and 4 as unproven. Decide where the flush zero block lives and add it to the 9.4 numbers; add 'and is_frame_synchronous' to the is_in_place rule; correct or re-measure the 20-byte figure.

**Second reviewer.** Read sketch.h whole: no stage_traits, junction_traits, joint<> template, is_in_place, latency(), exact_latency, tail_frames, flush, with_block (k_block is one global), fixed<>, planar or spectrum ports, parallel, oversample, feeding, analyzers, or a junction report/reset; the plan's header table and section 9 do claim sections 3 to 6 as running code. My run of measure.sh prints 32 bytes for strip_callback where 9.2 and the README say 20 (20 is now post_design's frame). The in-place point is right: a 1:1 block stage with a part-filled block writes a whole block over input it has not read. The 512-byte zero block is arithmetic from the plan's own text, not measured.

<details><summary>Evidence as reported (confidence: high)</summary>

Read sketch.h whole and grepped it for each member named in sections 3 to 6. measure.sh copied to scratch and run with arm-none-eabi-g++ 13.2.1: 6,184 / 548 / 32 / 76 / 16 bytes reproduce; 'void strip_callback(...)' is 32 bytes of stack (48 with the mailboxes), audio_callback 64. t/prop.cpp and t/fixed.cpp exercise what is present; t/fixed.cpp case 6 (C arrays) fails with 'no matching function'. The 512-byte figure is arithmetic from k_block = 64.

</details>

<a id="f33"></a>
### 33. operator| flattens only its left operand, and four smaller defects in the sketch's vocabulary types

- **Lens:** sketch (The sketch as evidence: C++17 correctness, and does it match the plan)
- **Where:** PLAN.md section 4 ('it flattens, so the type lists the stages once'; stage<I>() by position); section 3.3 (expected; 'A design is a class'); sketch.h:26-30, 215-241, 272, 630-637
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** (1) a | (b | c), stage | named_chain and chain | chain all produce a nested chain<..., chain<...>>, a different type with a different state and different stage<I>() positions from a | b | c. A named sub-chain on the right (constexpr auto eq = b1 | b2; in | eq | out) is the natural way to write section 9, and stage<I>() by position then means something else. Nesting is not worse in memory, which is worth knowing before deciding: the nested form is smaller here because the inner chain's scratch is sized from its own 64-frame block. (2) expects() is not constexpr, so *e and e.error() on a constexpr expected are not constant expressions; the same will be true of every checked accessor (span at the audit level) unless the check function is constexpr with the handler call in the failing branch. (3) expected's anonymous union deletes the destructor for a T that is not trivially destructible; fine for designs, but section 3.3 promises std::expected's interface without saying T and E must be trivial. (4) lowpass_design::from_table is public and unchecked, so 'only make() and the table generator can produce one' (the response to first-audit finding 5) is not enforced: an unstable table builds an infallible stage. A constexpr stability check in from_table would refuse a bad constant table at compile time in C++17. (5) A non-stage operand of | gives the compiler's 'no match for operator|', not a catenae message; chain<> fails inside <tuple>.

**Recommendation.** Add the two missing overloads (stage | chain and chain | chain) so that | always flattens, or say in section 4 that only left-nested composition flattens and that a parenthesised sub-chain is a nested stage with its own block and positions. Make the check function constexpr. State the triviality requirement on expected's T and E. Make from_table constexpr-checked (or reachable only by the generator's output) and add a compile-fail test for an unstable table. Give | a static_assert overload for a non-stage operand.

**Second reviewer.** Reproduced all five: a | (g | d) is chain<up2, chain<map, down2>> (state 1,560 bytes against 2,072 flat) and (a | g) | (g | d) nests its right operand; *e on a constexpr expected fails with 'call to non-constexpr function expects'; expected<T> with a non-trivial destructor has a deleted destructor; from_table({1,0,0,5,7}, -3) builds a lowpass silently; a | 3 gives the compiler's 'no match for operator|'. Section 4 says both 'it flattens' and 'chains nest' without saying which applies to a right-hand chain.

<details><summary>Evidence as reported (confidence: high)</summary>

/tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/sketch/t/pipe.cpp: a | (g | d) is chain<up2<2>, chain<map<2>, down2<2>>> (state 1,560 bytes) against chain<up2<2>, map<2>, down2<2>> (2,072 bytes), is_same_v false; (a | g) | (g | d) is chain<up2, map, chain<map, down2>>; 'a | 3' gives 'no match for operator|'; chain<> gives 'tuple index must be in range'. t/misc.cpp: a non-const lvalue chain on the left does flatten. t/exp.cpp: -DCX 'call to non-constexpr function void catenae::expects(bool, int)' on g++-13 and 'not an integral constant expression' on clang++-18; -DNONTRIV 'use of deleted function ~expected()'; -DFROMTABLE: from_table({1, 0, 0, 5, 7}, -3) and a lowpass built from it compile silently; -DDISCARD: the nodiscard warning does fire on both compilers.

</details>

<a id="f34"></a>
### 34. mailbox::post is formally undefined behaviour in C++17 for every design type: assignment cannot start the lifetime of a union member that has no trivial default constructor

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** Section 6.3, D44, D37 ("a design is a class, not an open aggregate"), section 8 ("no undefined behaviour relied on"); sketch.h lines 763 and 793-796
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** A slot is `union { unsigned char unused; T value; }`, zero-initialised so that `unused` is the active member. post() does `m_slots[i].value = value`. C++17 [class.union]/5 lets an assignment begin the lifetime of the nominated member only when it is a non-class type or a class with a non-deleted trivial default constructor. The sketch's lowpass_design, and by D37 every design, has a private constructor and no default constructor at all. So for the plan's own example, mailbox<lowpass_design<float>>, the assignment calls a member function on an object whose lifetime has not begun, and take() then reads it. GCC and Clang generate the expected byte copy today, and UBSan does not report it. But section 8 claims no undefined behaviour is relied on, and the first constexpr use or a stricter compiler turns it into an error. The static_assert on trivial copyability also admits a T whose copy assignment is deleted (a class with a const member), which then fails inside post() with an unrelated message.

**Recommendation.** In the C1 mailbox, start the object explicitly: `::new (static_cast<void*>(&slot)) T(value)` in post(), or store `alignas(T) unsigned char[sizeof(T)]` and std::memcpy in and out (defined for trivially copyable types). Assert is_copy_constructible as well as is_trivially_copyable. Add a compile-time test that posts and takes a design type in a constant expression on the C++20 leg, which is the only tool that sees this.

**Second reviewer.** Reproduced (union_life.cpp, -std=c++20 constant evaluation): assignment through the inactive union member is accepted for float and a trivially default-constructible aggregate on g++-13, g++-14 and clang++-18; for a class with only a private constructor g++-14 rejects with 'does not implicitly begin its lifetime because design does not have a non-deleted trivial default constructor' and clang++-18 with 'member call on member value of union with active member unused'; g++-13 accepts. embedded.cpp does instantiate mailbox<lowpass_design<float>>. It is a sketch defect the C1 mailbox must not copy; the plan's 6.3 does not prescribe the union, but section 8 does claim no undefined behaviour is relied on.

<details><summary>Evidence as reported (confidence: high)</summary>

union_life.cpp in the scratch directory, same union shape, evaluated at compile time with -std=c++20.
- T = float and T = an aggregate with a trivial default constructor: accepted by g++-13, g++-14 and clang++-18.
- T = a class with only a private constructor (lowpass_design's shape; static_assert that it is trivially copyable and not default constructible): g++-14 rejects with "'slot<design>::value' does not implicitly begin its lifetime because 'design' does not have a non-deleted trivial default constructor, use 'std::construct_at' instead"; clang++-18 rejects with "member call on member 'value' of union with active member 'unused' is not allowed in a constant expression". g++-13 accepts it (it does not enforce the rule).
embedded.cpp line 47 and demo.cpp line 111 instantiate mailbox<lowpass_design<float>>.

</details>

<a id="f35"></a>
### 35. The mailbox's atomic byte does not link on Cortex-M0/M0+, and on Hexagon it is a locked update of the whole word that also holds each side's private index

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** Section 6.3 ("three slots and one atomic byte"; "an interrupt handler counts as a thread"), section 6.1 report() counters, section 7.3 (sanitizers), C0a ("the Cortex-M and Hexagon QEMU legs"); sketch.h lines 764, 777, 798-801
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** The plan names "Cortex-M" without saying which architecture levels, and its single-core, interrupt-as-a-thread use is exactly where an M0-class part appears.
1. ARMv6-M has no exclusive-access instructions. There the mailbox's exchange and any fetch_add counter (async counts underruns and overruns that way) become calls to __atomic_exchange_1 and __atomic_fetch_add_4, which the bare-metal toolchain does not supply, so the image does not link. handoff's plain loads and stores are fine.
2. The sketch has no is_always_lock_free assertion (the carried ring has one). On a platform that does supply those routines with a lock, the mailbox would silently stop being wait-free.
3. On Hexagon, a named CI target, there is no byte-wide locked access. The compiler rewrites the byte exchange as a load-locked and store-conditional of the aligned 32-bit word, and that word also contains m_poster and m_taker, which each side writes with ordinary byte stores. This is correct only if an ordinary store by the other hardware thread defeats the reservation. I could not confirm that for Hexagon, and qemu-hexagon cannot show it either way.
4. Neither verification leg in 7.3 (thread sanitizer on x86, stress on arm64) exercises an interrupt pre-empting the other side on one core.
Small cost: the offset encoding computes `% 3` two to four times per post or take, which is a udiv on M4/M33/M55 and a library divide on M0.

**Recommendation.** Make the mailbox's atomic a 32-bit word that shares its word with nothing (the report() fields are already specified as 32 bits), and replace the modulo arithmetic with a 3-entry table or a compare. static_assert is_always_lock_free in mailbox and handoff. State the supported cores in section 6: ARMv7-M and ARMv8-M as is; ARMv6-M either out of scope or served by one port hook (an exchange under PRIMASK) that the integrator supplies. Give single-writer counters a load and a store, not fetch_add. Add one bare-metal QEMU test in which a timer interrupt is the poster (and the producer) against the main loop, since that is the deployment the assumption of use describes.

**Second reviewer.** Reproduced: for cortex-m0 and m0plus the object references __atomic_exchange_1, __atomic_fetch_add_4 and __aeabi_uidivmod and the link fails; m3, m4, m23, m33, m55 inline exclusive-byte instructions; the sketch has no is_always_lock_free assert; clang-18 for hexagonv68 lowers the byte exchange to memw_locked on the aligned word. But the plan names no ARMv6-M target (the family's legs are M4, M33, M55), and the Hexagon hazard is unverified by the reviewer's own account and I could not verify it either; word-wide LL/SC for sub-word atomics is the compiler's standard lowering. What stands is a missing statement of supported cores and the lock-free assert.

<details><summary>Evidence as reported (confidence: medium)</summary>

arm_probe.cpp in the scratch directory (mailbox post and take, handoff push and pull, one fetch_add), arm-none-eabi-g++ 13.2, -Os.
- cortex-m4: ldrexb/strexb inline, links with --specs=nosys.specs.
- cortex-m33, cortex-m55, cortex-m23: ldaexb/stlexb inline, link.
- cortex-m0 and cortex-m0plus: undefined __atomic_exchange_1, __atomic_fetch_add_4 and __aeabi_uidivmod; the link fails with "undefined reference to `__atomic_exchange_1'". __GCC_ATOMIC_CHAR_LOCK_FREE is 1 on M0 and 2 on M4.
- hex.c, clang-18 --target=hexagon -mcpu=hexagonv68 -O2: the byte exchange compiles to `r0 = and(r0,#-4)`, `r1 = memw_locked(r0)`, `memw_locked(r0,p0) = r5`.
One web search did not find Hexagon's rule on ordinary stores and reservations, so point 3 is an unverified risk, not a demonstrated fault.

</details>

<a id="f36"></a>
### 36. The tidy dropped report()'s read order: a clamp alone reports a nearly empty ring as full

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** Section 6.1, report() row ("the fill is clamped to the capacity"); first-audit finding 22
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** Finding 22 asked for the fill to be "computed with a stated read order and clamped", and the plan before the tidy said so (commit 12849d4, line 401: "computed in a stated read order and clamped to the capacity"). The tidied plan keeps only the clamp. The order is the part that matters. A third thread that reads head and then tail can see a tail that has passed the head it read. The unsigned difference wraps to nearly 2^32 and the clamp turns it into "full". Reading tail first and head second can only over-estimate, and the clamp then bounds it. For async the question does not arise: its fill is a smoothed float that the consumer publishes. That is a second valid design the contract row should allow by name.

**Recommendation.** Restore the sentence, and make it concrete: report() reads the consumer's index first and the producer's second, then clamps; or the junction's consumer publishes the fill as one atomic word (async's form). Say which one handoff uses.

**Second reviewer.** Draft 32 line 401 reads 'the fill is computed in a stated read order and clamped to the capacity'; the new 6.1 row keeps only 'the fill is clamped to the capacity'. The arithmetic is right: head read as 100, tail later read as 164, 100 - 164 mod 2^32 = 4,294,967,232, clamped to 1024, reported full; in the other order the result over-estimates and the clamp bounds it. The first audit's finding 22 asked for the read order and the tidy lost it.

<details><summary>Evidence as reported (confidence: high)</summary>

Arithmetic on the sketch's uint32 indices with Capacity 1024: read head = 100; both sides then advance by a few blocks with the true fill never above 64; read tail = 164. 100 - 164 mod 2^32 = 4,294,967,232, and min(that, 1024) = 1024: reported full while the ring never held more than 64 frames. In the other order (tail = 100, then head = 228) the result is 128, an over-estimate bounded by the capacity. `git show 12849d4:PLAN.md` line 401 against PLAN.md line 292. model_handoff.py in the scratch directory confirms the two sides' own fill computations stay in [0, capacity] in every interleaving.

</details>

<a id="f37"></a>
### 37. The async junction's pull calls exp and about fifty software-double routines on a Cortex-M33, which D36's link-stage gate rejects; section 12 does not list the servo as new numeric work

- **Lens:** concurrency (Mailbox, handoff and the junction contract under the C++ memory model)
- **Where:** D36 and section 7.3 (the link-stage gate), section 7.1 ("the asynchronous converter's servo" as compiled per storage type), section 12 ("not mechanical in five places"), C8, CF
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The gate says a float-profile image for a core with a single-precision FPU may not reference a software-double routine or a double libm entry. The carried async converter runs its servo, its occupancy arithmetic and the conversion of the rate estimate to the resampler's fixed-point phase in double on the consumer thread on every pull, including exp. That is once per call, not per sample, so the cost is small. But the gate is a link-time yes or no, and it will fail the float async image on the M33 and M4F. Either the servo becomes a new single-precision numeric design (a 0.05 Hz loop and a rate estimate that must resolve well under 1 ppm, in a format whose resolution near 1.0 is 0.06 ppm), or the gate needs a stated exception. Neither is in the plan, and section 12's list of places where the port is not mechanical names the FIR accumulator but not this.

**Recommendation.** Decide before CF, since the CF skeleton is where the gate first meets async. Either add "the asynchronous converter's servo and phase arithmetic" to section 12's non-mechanical list as a new float or fixed-point design with its own measured floor, or write the gate's exception into D36 (per-call control arithmetic of a junction, named symbol by symbol) so that the gate stays a yes or no.

**Second reviewer.** Reproduced: basic_converter<float, 2>::pull from origin/main compiled for cortex-m33 hard-float -Os has relocations to __aeabi_dmul x18, __aeabi_dsub x9, __aeabi_dadd x8, __aeabi_dcmpgt x4, ddiv, d2f, f2d, d2uiz and exp x2 in its own section, with more in pi_servo::advance_hold and fractional_resampler::process. D36's gate as worded would reject that image, and section 12's list of non-mechanical work does not name the servo. Minor is right: async is re-implemented at C8 and the cost is once per call. Duplicate of finding 51.

<details><summary>Evidence as reported (confidence: high)</summary>

async_m33.cpp in the scratch directory instantiates basic_converter<float, 2>::pull and push from origin/main (7dbc79d): arm-none-eabi-g++ -std=c++20 -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -Os. The relocations in pull's own section: __aeabi_dmul x18, __aeabi_dsub x9, __aeabi_dadd x8, __aeabi_dcmpgt x4, __aeabi_ddiv, __aeabi_d2f, __aeabi_f2d, __aeabi_d2uiz, and exp x2. pi_servo::advance_hold adds exp, __aeabi_ddiv and more; fractional_resampler::process has __aeabi_d2lz and __aeabi_dmul; publish_status has __aeabi_dmul. Source: converter.h lines 362-400 (backlog_frames() returns double; dt and m_servo.update(occ, mu, dt) are double) and pi_servo.h (all double).

</details>

<a id="f38"></a>
### 38. What make() costs is gone from the plan: the carried tail computation needs a 1.4 to 20.6 KB stack frame and cannot be "compiled, parameterized on nothing" without a workspace or the worst-case frame

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** D37 (a design "stores its latency and tail as numbers"), section 7.1 table row 3 ("the tail computation" compiled, parameterized on nothing), section 9.2 and 9.3 (make() called on a control path), section 7.3 (`-Wstack-usage=` at each target's stack ceiling); first-audit finding 12 item 5, not carried through the tidy
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** D37 moves the tail into the design, so every make() of a recursive design runs the carried flush-window computation. On DspTap main that is a template on the section count with its N x N matrices on the stack. Section 7.1 lists it as compiled once with no template parameter; without a heap that means either the order-16 frame (20.6 KB) for every design, order 2 included, or a caller's workspace that no make() signature in the plan shows. The sketch's make() is a toy with no tail (88-byte frame; tan and seven double routines), and those are the only designer numbers the plan quotes. The first audit asked for make()'s stack and instruction count as contract numbers per order; the tidied plan states neither, and a per-function -Wstack-usage limit set low enough to mean anything will fire on this function.

**Recommendation.** Say which it is in 3.3 and 7.1: the tail computation is a header template on the section count (stack as carried, stated per order), or it is compiled and takes a caller's workspace with a constexpr size function like the table designs. Put make()'s stack and instruction count per order on the design's card as contract numbers, and say in 9.2 on which stack make() is expected to run. Decide what `-Wstack-usage=` is set to (a per-function frame limit, not the whole-stack ceiling) and list make() among the functions it must admit.

**Second reviewer.** Reproduced on arm-none-eabi-g++ 13.2 cortex-m33 -Os (bw_arm.cpp against DspTap origin/main): cascade_flush_window<float, K> frames are 480, 1,432, 5,272 and 20,640 bytes for K = 1, 2, 4, 8, and the object references tan, sin, cos, log, __divdc3, __muldc3 and the __aeabi_d* set. 7.1 row 3 does list 'the tail computation' as compiled and parameterized on nothing while D37 puts the tail in every make(). This is item 5 of the first audit's finding 12 (rated minor there), which D37 did not carry; still open, still minor. The 2.3 M instruction figure is the header's own, not re-measured.

<details><summary>Evidence as reported (confidence: high)</summary>

audit2/embedded/bw_arm.cpp: DspTap origin/main butterworth.h compiled with arm-none-eabi-g++ 13.2 -std=c++20 -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -Os -fstack-usage: detail::cascade_flush_window<float, K> frames 480 (K=1, order 2), 1,432 (K=2), 5,272 (K=4), 20,640 (K=8, order 16); the object references tan, sin, cos, log, __divdc3, __muldc3 and eleven __aeabi_d*/f2d routines. butterworth.h lines 88-93 on origin/main state the time: 2.3 M instructions a design at order 8 on a Cortex-M33 or M4F (not re-measured by me). The sketch's set_cutoff measured here: frame 88 bytes, painted high-water 176 bytes on QEMU mps2-an505 at -Os. PLAN.md 7.1 row 3 and D37 read as quoted; grep for "workspace" in PLAN.md finds only the FFT and table designs.

</details>

<a id="f39"></a>
### 39. Nothing bounds a configuration's size or keeps it constant-initialised, and the by-value chain makes both mistakes expensive and silent

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** D34 ("a small constant value"), D37 (small designs by value, table designs viewed), section 4 (`a | b | c`), section 9.2 ("a configuration whose parameters will change is a variable"), section 9.7 pattern 2, section 6.3 (mailbox T)
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** D34 works because configurations are a few bytes, but "small" is not a rule anything checks, and four ordinary spellings go wrong without a diagnostic. (1) A stage that holds a table by value: a variable configuration puts the whole chain in .data (flash and RAM), once per voice that has its own parameters, because per-voice parameters mean a configuration per voice. (2) A function-local `constexpr auto k = a | b | c;` without `static` is copied to the stack on every call. (3) `a | b | c` at run time (any stage built from a make() result) copies every stage so far at each `|`: the frame is quadratic in chain length. (4) A namespace-scope chain built from `*make(...)` silently becomes dynamic initialisation: start-up code, the configuration in .bss, and at -O0 the designer and soft-double linked in; C++17 has no constinit to stop it. Holding tables by view (9.7) fixes the sizes but leaves a gap in D44: a mailbox then carries the pointer, not the table, and post() gives the control thread no way to learn that the audio thread has stopped reading the previous table, so it cannot safely reuse that storage.

**Recommendation.** State the rule and enforce it: stage_traits asserts `is_trivially_copyable` and `sizeof(Stage) <= ` a small stated ceiling, so anything larger must be held by view; delete the rvalue overload of a view-taking constructor. Build a chain with one variadic construction instead of pairwise copies, or take stages by const reference in operator|. Provide a `CATENAE_CONSTINIT` macro (`__constinit` on GCC 10 and later, `[[clang::require_constant_initialization]]`) and use it in every example of a static configuration; write `static constexpr` in function-scope examples. For table designs, add to D44 either an acknowledgement the poster can read or the statement that a table handed over is never reused.

**Second reviewer.** Reproduced on the M33 at -Os with a 2 KB by-value stage: a function-local constexpr chain without static has a 2,104-byte frame (32 with static); a | b | c | d | e built at run time has a 10,376-byte frame; a namespace-scope chain built from *make(...) lands in .bss with an .init_array entry. Nothing in D34 or stage_traits bounds a configuration's size or forces constant initialisation; 9.7's 'a constant viewed, not copied' is a convention, not a check. The mailbox-and-view sub-point is finding 11 again.

<details><summary>Evidence as reported (confidence: high)</summary>

audit2/embedded/st (stress.h adds fir<Taps,Ch> holding a view, shaper<N,Ch> holding N floats by value, dither<Ch> with a seed, to a copy of sketch.h), arm-none-eabi-g++ 13.2 cortex-m33. s1.cpp: constexpr fir<63,2> | shaper<512,2> | dither<2> is 2,064 bytes of .rodata at -O0/-Os/-O2, no .data; the same chain as `static auto` is 2,064 bytes of .data. s7.cpp: function-local non-static constexpr of that chain: frame 2,080 (-O0), 2,200 (-Os), 2,200 (-O2); with `static`: 16 to 32. s2.cpp, operator| at run time: 5 small stages 248 (-Os) / 384 (-O2) bytes; 12 small stages 1,392 / 1,976; 5 stages with the 2 KB shaper 8,392 to 10,432; 12 stages with it 25,584 (-O0), 25,760 (-Os), 25,728 (-O2). s6.cpp, a 41,040-float bank (async's transparent table, 164,160 B): by view the variable chain is 24 bytes of .data and run-time composition an 8-byte frame; by value it is 164,176 bytes of .data and an 820,832-byte frame for three stages. s5.cpp: mailbox of a 2,052-byte value is 6,160 bytes of .bss. s4.cpp: `static auto s = lowpass<2>{*lowpass_design<float>::make(0.1)} | gain<2>{1.0f, 96};` gives a 4-byte .init_array entry, a 60-byte `_GLOBAL__sub_I` function and the object in .bss at -Os; at -O0 also tan and seven __aeabi_d* routines. sketch.h mailbox::post returns void.

</details>

<a id="f40"></a>
### 40. The stack figures quoted for the audio callbacks are leaf frames, one is stale, and the real depth is 1.5 to 5 times larger

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** Section 9.2 ("its callback's stack frame 20 bytes"), section 9.4 table ("Stack of the callback 64 bytes"), docs/sketches/config-state/README.md (20 and 64), measure.sh
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** measure.sh prints -fstack-usage of the callback function alone. That is one frame, not the stack the callback needs: chain<lowpass, gain>::run is not inlined into the strip's callbacks (72-byte frame), and up2::run and down2::run are not inlined into the overdrive's (32 and 24). The strip's figure is also out of date: the script at HEAD prints 32 for strip_callback, and 20 is now post_design's frame. On a core without an FPU the frames grow again. None of this is large, but the plan presents these as the measurements behind D34, and its own rule is that a number has one machine-checked home.

**Recommendation.** Replace the three figures with painted high-water numbers (or the static worst path from the carried ceilings script) and label them as such; correct 20 to the measured value in 9.2 and the README; have measure.sh fail if a quoted number and the measured one differ. State in section 4 that call depth in an unoptimised build grows by about one frame per stage.

**Second reviewer.** My run of measure.sh from a scratch copy: audio_callback 64, strip_callback 32 (the plan and README say 20), strip_callback_with_mail 48, set_design 12, post_design 20, set_cutoff 88; the .su also lists chain<lowpass<2>, gain<2>>::run at 72 and up2::run / down2::run at 32 / 24 as separate, non-inlined functions, so the call path is about 96 to 104 bytes, in line with the reported painted figures. I did not repeat the QEMU high-water runs or the -O0 and soft-float cases. The quoted numbers are leaf frames and one is stale; none is large.

<details><summary>Evidence as reported (confidence: high)</summary>

Copied the sketch to audit2/embedded/sk and ran ./measure.sh (arm-none-eabi-g++ 13.2, cortex-m33, -Os): audio_callback 64, strip_callback 32, strip_callback_with_mail 48, set_design 12, set_cutoff 88; the .su also lists chain<lowpass<2>, gain<2>>::run 72, up2<2>::run 32, down2<2>::run 24 as separate functions. Then linked the same objects with DspTap origin/main's platform/cortexm_startup.c and linker scripts (copied to scratch), painted 64 KB below the stack pointer, ran each callback under qemu-system-arm and scanned (audit2/embedded/bm/main_sketch.cpp, hw.h). High-water in bytes, overdrive callback / strip callback / strip with mail: M33 -Os 96 / 100 / 116; M33 -O2 68 / 100 / 116; M33 -O0 560 / 328 / 336; M4 soft-float -Os 128 / 168 / 184; M4F and M55 -Os 96 / 100 / 116. A 12-stage chain at -O0 (bm/deep.cpp): 1,032 bytes, about 70 bytes per stage through step<I>.

</details>

<a id="f41"></a>
### 41. "The profile is a template argument" understates what sizes a rate-changing stage's state, and two carried reset values are not zero

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** Section 3.3 ("Where a profile fixes a tap count that sizes storage, the profile is a template argument"), D22, D34, section 9.4 (`up2<2>{}`), C3 ("`up2` / `down2` and the L/M stage machine with one pinned design"), C7, CF, section 12
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** Because `state` is a nested type of the configuration, every size in it must be in the configuration's type. For rational that is not the profile alone: on main the taps per branch are pinned per profile, per band and per design divisor, where the divisor is the stage's position in its chain, and a custom profile's tap count is found by a run-time search. So the inner and outer `up2` of the 9.4 oversampler are different types with different state sizes, `up2<2>` as written cannot name either, a custom searched profile cannot exist, and the template signature of `up2` / `down2` that C3 ships with "one pinned design" is not the one C7 needs. Sizing the state for the largest pin instead would cost up to about 11 times the history of a relaxed stage. Separately, D34's all-zero reset does not match the carried stage machine, whose reset sets two fields to non-zero values; each needs a bias or a started flag, as does any generator with a seed (a zero xorshift state never leaves zero), and with a shared configuration every voice then draws the identical dither sequence. The tidy also dropped the pre-tidy sentence that said the sketch leaves the profile of up2 and down2 open.

**Recommendation.** Make the template argument the design, not the profile: a tag type per generated ROM table that publishes `taps` (and its latency) as static constants, e.g. `up2<Ch, Design>`, with profile and divisor choosing the tag through a metafunction that the named chains and `oversample<>` use. Fix that signature at C3, not C7, and have CF's table say that searched custom profiles and run-time `fifo_frames` are dropped. Add to 3.2 that a state field whose reset value is not zero is stored as an offset from it or behind one flag, and that a seed shared through one configuration gives identical sequences unless the state is given a per-instance stream.

**Second reviewer.** The first half is confirmed and is finding 25: taps per branch are pinned per profile, band and design divisor (design.h tables, 3 to 44), a custom profile is searched, and draft 32's sentence 'What the sketch leaves open: up2 and down2 take a profile' was dropped. The second half is already handled: stage.h reset() does set m_end = m_row_len and m_pending = 1, but 3.2 says 'anything that starts elsewhere belongs in the configuration or behind a flag', which is exactly the remedy proposed. The shared-seed observation (every voice draws the same dither sequence) is true and unaddressed.

<details><summary>Evidence as reported (confidence: high)</summary>

sampleratetap origin/main rational/include/tap/sr/rational/design.h lines 41-80: four tables of 8 divisors x 5 bands (160 pins), taps per branch from 3 (super_economy at 12/1) to 44 (transparent at 147/160); economy at 1/1 is 10, at 2/1 is 5 or 4; lines 121-131 "A custom profile leaves its pins at 0 and design_stage searches". stage.h line 518 `explicit basic_stage(const profile& p = profile::economy())`, lines 372-377 size m_rows, m_hist and m_scratch as std::vector from the design; reset() at lines 314-324 sets `m_end[e] = m_row_len` and `m_pending = 1`. async/converter.h line 56 `fifo_frames` and the filter_spec phases and taps are run-time fields of config. Pre-tidy PLAN.md (12849d4) line 873: "What the sketch leaves open: `up2` and `down2` take a profile"; no such sentence at HEAD. My dither stage (audit2/embedded/st/stress.h) needed a `started` flag to load the seed, exactly as the sketch's gain does.

</details>

<a id="f42"></a>
### 42. parallel<A, B> must size its alignment delay at compile time, but a stage's latency is a stored run-time number that set_design can change and that need not be whole

- **Lens:** embedded (Configuration and state on real targets)
- **Where:** D43 ("with their latencies aligned"), D39 and section 3.2 (`latency()` "stored numbers from the design"; `exact_latency()`), D22 and section 9.7 pattern 1, section 9.2 (set_design), C1 (`parallel` is a deliverable)
- **Severity:** minor as reported, major after verification
- **Verdict:** confirmed

**Problem.** parallel's alignment delay is memory, so under D22 and D34 its length must come from template arguments. Section 3.2 makes `max_outputs_for` explicitly `static constexpr` but describes latency as numbers stored in the design, read from the configuration object, and 9.2 shows a design being replaced on a running chain. So for a chain containing a designed filter the latency difference is not known to the type, can change after a set_design or a mailbox take, and is fractional (the sketch's low-pass reports 2.176 frames), which an integer delay cannot align. The plan does not say which chains parallel accepts, what bounds its delay, or what happens to the alignment when a design changes. It surfaces at C1, where parallel is to be built on `delay` and `gain`, and `delay<Ch, MaxFrames>` itself has a run-time length.

**Recommendation.** Decide in D43: parallel accepts only chains whose latency is exact, whole and a constant of the type (so `exact_latency` becomes a static constant for stages that have it, like `max_outputs_for`), with a static_assert naming the rule; or parallel takes a `MaxAlign` template argument and a run-time alignment that is re-read when a design changes, with the click stated. Say which, and add the rejected case to C1's compile-fail battery.

**Second reviewer.** The same finding as 4 and 8, and I rate the three alike: sketch.h line 308 is a non-static nominal_latency() reading the design, line 307 replaces the design, embedded.cpp's ROM design stores 2.176250899482823f, and neither parallel nor exact_latency exists in the sketch. The plan does not say which chains parallel accepts or what bounds its delay. It is cheap to settle (most likely: exact, whole, compile-time latency only), but it is a decision about a trait whose form C0b freezes and a C1 deliverable, which is why I put it above the reviewer's minor.

<details><summary>Evidence as reported (confidence: medium)</summary>

Read only; there is no parallel, oversample or chain latency in the sketch (grep -n -i "parallel\|oversample\|exact_latency" sketch.h demo.cpp: no matches) so nothing could be compiled. PLAN.md section 3.2 rows `max_outputs_for(n)` ("static constexpr") and `latency()` ("stored numbers from the design"); sketch.h line 308 `constexpr float nominal_latency() const noexcept { return m_design.nominal_latency(); }` is a non-static member reading the design, and line 307 set_design replaces it; embedded.cpp's ROM design carries 2.176250899482823f as its latency.

</details>

<a id="f43"></a>
### 43. D36 measured: float accumulation costs 8 to 26 dB, no source stage falls below its profile except async's 140 dB tier, but summation order (up to 20 dB) and contraction are undecided and the tolerance cannot be in ulps

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** D36; section 12 ("The float accumulator: a new numeric design with new floors"; fir_kernels.h carried); section 4 (bit-identical at contraction off); section 7.1 (FIR dot kernels are header templates); C7 ("to a tolerance")
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The decision holds, and "measured and stated per stage" is nearly enough. Three things are missing and are cheap now and awkward after floors are recorded. (a) The carried kernels' strict ascending tap order is the noisiest reasonable order in float: it finishes by adding the small far-edge products to a full-size partial sum. Accumulating from both ends toward the largest tap (two contiguous loops, no gather) recovers 9 to 20 dB, to within 4 to 6 dB of double accumulation. Four interleaved partial sums, which is what a Helium or Neon float reduction computes, recover 4 to 12 dB. Fixing strict order first forecloses tap-axis vectorisation on the M55 or forces every floor to be re-recorded. (b) With double accumulation the product of two floats is exact, so the carried float FIR gives the same bits fused or not. With float accumulation it does not, and GCC fuses by default on every FPU with VFMA and ignores #pragma STDC FP_CONTRACT. Float FIR output therefore differs between x86-64 and the Arm legs, and no cross-leg bit pin is possible for these header-template kernels. (c) The float-against-SampleRateTap difference is unbounded in ulps of the output.

**Recommendation.** In D36 fix the float accumulation order as part of the numeric design, before any floor is recorded: ends toward the largest tap, or a fixed four-lane order chosen so that Helium and Neon can meet it. State that float FIR stages carry floors and tolerances, never cross-target bit pins, unless the kernel is in the compiled core with contraction off. Write C7's tolerance as absolute against the block's peak (measured need: 1.2e-6; 4e-6, about 2^-18, leaves margin up to 800 taps) or as an rms floor of -125 dBFS, not in ulps or per-sample relative error. State on async's transparent card that the float profile's floor, about -140 dBFS, is at the tier's own number and that _f64 is the profile that meets it with margin.

**Second reviewer.** Reproduced with my own C++ test (Kaiser low-pass, sine, 60,000 outputs, contraction off): at 773 taps float strict order -131.4 dBFS, four lanes -142.7, ends-to-centre -151.6, double accumulation -156.3; at 184 taps -137.2 / -148.0 / -149.1 / -156.7. On the M33 GCC emits vfma.f32 for the float loop by default and with #pragma STDC FP_CONTRACT OFF, and vmul + vadd only with -ffp-contract=off; the double loop calls __aeabi_dmul/dadd. Section 4 already says a header-template stage compiles under the consumer's flags and its card says so, which covers part of (b); the accumulation order and the form of C7's tolerance are undecided. I did not re-run the per-stage SampleRateTap designs or the ulp figure.

<details><summary>Evidence as reported (confidence: high)</summary>

facc.py and facc2.py (numpy 2.5.3) in /tmp/claude-0/-home-user/dd231a64-4f4f-561b-96b1-85c53a22809c/scratchpad/audit2/dsp/: Kaiser designs per DspTap origin/main nyquist.h and decimate.h at the source tap counts (rational design.h pins m = 7 to 25 per branch, N = 2mL - 1; decimate.h 81/121/239 and 259/389/773; bridge design.h 28 to 184 per phase; async polyphase_filter.h 32 to 80), float samples and coefficients, 60000 outputs, error against double coefficients with double accumulation, in dBFS re 1.0 (sine / white noise at 0 dBFS). Carried double accumulation: -150 to -164. Float, strict order: rational down2 economy (21 taps) -145.3 / -149.2; down2 transparent (51) -141.9 / -145.5; down3 transparent (101) -139.0 / -144.3; down8 transparent (351) -133.7 / -143.3; decimate by 6, 100 dB (773) -131.4 / -139.4 (SNR 128.4 / 126.8 dB); bridge transparent (184) -136.7 / -137.6 (SNR 133.7 / 132.2); async transparent (80) -140.2 / -140.9 (SNR 137.2 / 135.7, against a "140 dB-class" tier, before its two-row blend). The floor is relative: at -60 dBFS, 773 taps, -189.6 dBFS. Order, 773 taps sine: strict -131.4, reversed -131.1, fused -131.4, four lanes -143.0, ends-to-centre -151.6, double -157.2; bridge 184: -136.7 / -142.1 / -149.5 / -155.8. Float versus carried per sample: 65 to 94 % of outputs differ, largest difference 1.2e-6 of full scale, which is up to 592,454 ulp of the output near a zero crossing. mac.cpp, arm-none-eabi-g++ 13.2 -O2 -mcpu=cortex-m33 -std=c++17: the float loop is vfma.f32 by default and still vfma.f32 with #pragma STDC FP_CONTRACT OFF; only -ffp-contract=off gives vmul + vadd. The double loop calls __aeabi_f2d (twice), __aeabi_dmul, __aeabi_dadd, __aeabi_d2f, which confirms D36's premise. Fused versus unfused float accumulation: 45.7 % of outputs differ; the double product was checked exact with Fractions.

</details>

<a id="f44"></a>
### 44. flush() is one call for the whole tail, and the carried recursive tail is 10^4 to 10^6 frames; flush_output_frames() needs the state

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** Section 4 (flush); section 3.2 (tail_frames, flush_residue); D9; D22; section 11.2 card ("Tail ... fc >= 0.002"); C1, CF, C3
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** flush(state&, out) writes the whole tail into one caller buffer, sized beforehand from flush_output_frames(). The carried definition of a recursive tail is decay from the largest finite value to below the smallest normal one, which is what the plan's residue language ("256 x min normal") keeps. That makes the tail tens of thousands to millions of frames. With no heap, on the targets the library is for, the caller cannot own that buffer, and the call's work is unbounded in practice. FIR and STFT tails are small, so C1 and CF will not see this; it arrives with the Butterworth at C3, after the flush contract is fixed. Two smaller gaps: flush_output_frames() is written without the state, but for a rate change or a bare bursting region the count depends on the phase and the block fill (DspTap's version reads its own stateful outputs_for); and steady<> must declare a tail. Flush, tail and the zero block exist nowhere in the sketch.

**Recommendation.** Make flush incremental: flush(state&, out) writes at most out.size() frames and returns the count, with a cursor (stage index, zeros remaining) in the chain's state and flush_remaining(const state&) for the total. It can then run from a fixed block. Give flush_output_frames the state. Offer a practical tail beside the exact one (zero frames to -120 dB re full scale, with the residue at that point as the stated number), or say on the card that a caller on a small target feeds zeros through process() instead. State steady<>'s tail as the inner tail plus the prefill. The zero block is needed only for stage 0: a later stage's input scratch can be cleared instead.

**Second reviewer.** Reproduced the Butterworth figures on DspTap origin/main (bw.cpp): float order 8 window_frames 954 at fc 0.2, 2,972 at 0.05, 14,736 at 0.01, 29,568 at 0.005, 74,314 at 0.002 (the card's own lower limit), 149,085 at 0.001, 1,508,566 at 1e-4; double order 8 from 7,570 to 11,643,333; float order 4 at 0.002 is 37,748. Section 4 writes flush_output_frames() with no state although the count depends on a rate changer's phase, and the sketch has no flush. The stft tail figures (791, 1022) I did not re-run. Same point as 14; minor is the right level.

<details><summary>Evidence as reported (confidence: high)</summary>

bwtail.cpp (same scratch directory) against DspTap origin/main butterworth.h, window_frames() in frames, float order 8: fc 0.2 -> 954; 0.05 -> 2,972; 0.01 -> 14,736; 0.005 -> 29,568; 0.002 -> 74,314 (581 KB of stereo float; this is the card's own lower limit); 0.001 -> 149,085; 1e-4 -> 1,508,566. Double order 8: 7,570 to 11,643,333. Float order 4 at 0.002: 37,748. stft_lat.py: a 512/128 region that zeroes bins answers an impulse for 791 frames after it (bound N - hop + N - 1 = 895, plus up to 127 of alignment), so a steady region's tail is 2N - 2 = 1022 input frames: finite and definable. grep -n "flush\|tail_frames" /home/user/catenae/docs/sketches/config-state/sketch.h: no hits.

</details>

<a id="f45"></a>
### 45. D36's link-stage gate, as worded, rejects the plan's own run-time design example

- **Lens:** dsp (DSP: the accepted decisions measured)
- **Where:** D36; section 7.3 (Double promotion bullet); section 9.2 and 9.3; section 7.1 (designers are compiled double arithmetic)
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** 7.3 says a float-profile image for a core with a single-precision FPU "may not reference a software-double routine or a double libm entry". 9.2 and 9.3 show, as the intended use on a Cortex-M33, a control path calling lowpass_design<float>::make() at run time and posting the result through a mailbox, and 9.2 reports that this file "calls tan and seven software-double routines". An image-level gate fails that image. A per-object gate cannot replace it: make() in the sketch is a header template, so it lands in the consumer's object beside whatever else that file holds, and section sweeping or LTO blurs the line further. The gate's scope is therefore an open decision, and until it is made the gate either blocks run-time redesign on exactly the cores D36 is for or checks nothing.

**Recommendation.** Define the gate by what it protects: it runs on named CI probe images that contain audio paths built from ROM designs only (one per float stage family), where any __aeabi_d* or double libm reference fails. An image that links a designer is outside it by definition. Say so in D36 and 7.3, and keep the designers in the compiled core (7.1 already puts them there) so that "links a designer" is visible as a symbol.

**Second reviewer.** The texts do conflict: 7.3 (lines 418-421) forbids a float-profile image from referencing a software-double routine or a double libm entry, 9.2 and 9.3 show make() at run time on the control path, and measure.sh on the sketch lists tan and seven __aeabi_d* routines for the designer's file, as 9.2 itself reports. make() is a header template in the sketch (lines 251-269). The gate's scope (which images) is stated nowhere; one sentence in D36 or 7.3 settles it.

<details><summary>Evidence as reported (confidence: high)</summary>

PLAN.md lines 418-421 (the gate, "image"), 604-609 and 614 (make() at run time; "the designer's file calls tan and seven software-double routines"), 351 (designers are double). git show 12849d4:PLAN.md lines 591-594 has the same image-level wording with "the gate sees every path". /home/user/catenae/docs/sketches/config-state/measure.sh lists undefined symbols per object file (embedded.o against embedded_design.o), which is a per-file inspection, not a link-stage gate. sketch.h lines 251-269: make() is defined in the header.

</details>

<a id="f46"></a>
### 46. The sketch's joint proof never fires: a mono output feeding a stereo input compiles on three compilers and runs

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** D6, D27, section 3.2 (`joint<Index, OutputPort, InputPort>`), section 9 ('all of this is in the sketch'), the header table ('the contracts of sections 3 to 6 as code that compiles and runs'), C0b and C1; sketch.h lines 480-491
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The sketch puts the port check in `joint<I>()`, reached only from the initialiser of `static constexpr bool k_joints_ok`. A static data member of a class template is instantiated only when used, and nothing reads `k_joints_ok`, so the static_assert is never evaluated. Two stages whose ports differ in channel count but share a sample type join, compile without a warning and run with wrong frame arithmetic. The plan's central compile-time promise is therefore not demonstrated by the code it cites. It also shows the trap the real `joint<>` template has to avoid: a check placed in a template entity that nothing forces to instantiate is no check. C0b delivers `joint`, but its compile-fail tests are listed at C1.

**Recommendation.** State in 3.2 that the joint assertions are evaluated at class scope of `chain` (a `static_assert` over a fold in the class body, or `joint<...>` as a base class), not inside a member that must be used. Move 'one compile-fail test per way two ports can differ' to C0b with the `joint` header it tests. Either fix the sketch or drop the sentence that it carries the contracts of section 3.

**Second reviewer.** Reproduced (host.cpp): a to_mono<2> stage (port<float,1> out) piped into the sketch's gain<2> (port<float,2> in) compiles with no diagnostic on g++-13, g++-14 and clang++-18 at -std=c++17 -O2 -Wall -Wextra -Wpedantic and prints '32 frames'; k_joints_ok is declared once at sketch.h:491 and never read, so the static_assert in joint<I>() is never instantiated. The plan leans on the sketch only in general terms for this (9.5's compile error is in the excepted section), and C1's compile-fail battery would catch the same mistake in the real joint<>, so it is a sketch defect and a useful warning, not a hole in the plan.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch audit2/build/misc/joint2.cpp: a `to_mono<2>` stage (input port<float, 2>, output port<float, 1>) piped into the sketch's `gain<2>` (input port<float, 2>), then `process` on two std::array<float, 128>. g++-13, g++-14 and clang++-18 at -std=c++17 -O2 -Wall -Wextra -Wpedantic: no diagnostic; the program prints 'mono feeding a stereo gain compiled and ran: 32 frames'. `grep -n k_joints_ok` over the sketch directory: one hit, the declaration at sketch.h:491.

</details>

<a id="f47"></a>
### 47. 'An image cannot mix levels' holds only if the handler is inside the inline namespace, and not at all for a header-only file with no live check; the amalgamation cannot carry the contraction policy

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** D27, section 3.4 (one level per image; a failed check), section 7.1 (the numeric contract is the library's), section 7.4 (amalgamation), C0b's link-fail test; sketch.h line 22
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The inline namespace does what the first audit asked wherever a file calls the compiled core or reaches a live check: a mismatch refused to link in all nine toolchain modes I tried. Three things remain. (1) It depends on the handler being declared inside the namespace. The sketch declares `catenae_check_failed(int)` at global scope; in that form a header-only file at `audit` and one at `boundary` link together everywhere, including the M33 at -Os with --gc-sections and with LTO. The plan's wording ('every symbol crossing into the library') does not cover the handler, which crosses out of it. (2) A header-only file whose checks are all compile-time (fixed blocks at `boundary`) references nothing, so it links into an image of another level or FFT choice. Nothing is shared, so it is not an ODR violation, but D27's 'an image cannot mix levels' and 3.4's 'two files that disagree do not link' are false as written, and 'what is tested is what ships' leans on them. The first audit's finding 28 asked for the narrower wording. (3) A consumer's forward declaration `namespace catenae { template <std::size_t> struct gain; }` becomes ambiguous; the `joint<...>` note that 3.2 says is 'where the two ports are read' will print the tag on every type; Doxygen needs the tag macro expanded. Separately, for the amalgamated `catenae.cpp` built by 'anything else', 7.1's 'its bit pins hold whatever the consumer passes' has no carrier: there is no build system to add the flag.

**Recommendation.** Say in 3.4 that the handler is declared inside the inline namespace and defined by the integrator as `void catenae::check_failed(catenae::check) noexcept` (this compiles without naming the tag), and that the tag is computed by the preprocessor in one non-generated header from config.h's macros so that three build systems and a hand-edited config.h cannot spell it differently. Reword D27 to what is delivered: no definition is shared across configurations, and a file that calls the compiled core or contains a run-time check does not link against another configuration. Give C0b's link-fail test the cases above, including the one that is expected to link. Ship `catenae/fwd.h`. For the amalgamation, state the required contraction flag as an assumption of use and have its CI leg run the bit pins; a per-compiler pragma is a deviation and does not hold under clang's fast mode.

**Second reviewer.** Reproduced with a minimal inline-namespace model on g++-13 and on the M33 at -Os with --gc-sections: with the handler declared inside the namespace, an audit file with a live check does not link against a boundary handler ('undefined reference to catenae::audit::check_failed(int)'); an audit file that uses only the fixed-block path links into a boundary image; with the handler at global scope, as sketch.h:22 has it, audit and boundary files link together. So D27's 'an image cannot mix levels' is true only for files with a live check or a core call and only if the handler is in the namespace. GCC ignoring the STDC contraction pragma is also confirmed, so the amalgamation has no carrier for the flag. I did not re-run the forward-declaration or LTO cases.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch audit2/build/ns (catenae.h with `inline namespace CATENAE_ABI` built by token pasting from config.h; core.cpp, handler.cpp, three consumers; run.sh). In g++-13 -O2, g++-14 -Os gc-sections, g++-13 -flto, clang++-18 -O2, clang-18 + lld gc-sections, clang-18 -flto, arm-none-eabi-g++ cortex-m33 -Os gc-sections, the same with -flto, and -O0: cases 1-4 (a consumer calling the core, or with a live boundary or audit check, against a library of another level or FFT choice) are refused, e.g. 'undefined reference to catenae::cfg_audit_own::check_failed(...)'; cases 5-7 (fixed-block header-only consumers of another configuration; an audit file plus a boundary file with the integrator's handler built at audit) LINK in every mode. ns/g2 (handler at global scope): audit header-only file + boundary file + boundary core LINKS on g++-13, clang-18 LTO, M33 gc-sections and M33 LTO. spec.cpp: specialising `catenae::is_stage` from outside compiles; the forward declaration gives 'reference to gain is ambiguous'. misc/fma.cpp on the M33: default g++ -std=c++17 emits vfma.f32 (fused); `#pragma STDC FP_CONTRACT OFF` is ignored by GCC with a -Wall warning; `#pragma GCC optimize("fp-contract=off")` gives vmla.f32; clang-18 honours the STDC pragma by default but still emits vfmadd under -ffp-contract=fast.

</details>

<a id="f48"></a>
### 48. A public from_table() lets unvalidated numbers become a design, so 'a stage is built from a design and cannot fail' still has the hole the first audit's finding 5 reported

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** D37, section 3.3 (a design is where failure lives; two kinds), section 9.2 (`from_table`), the response file's D37 row ('only make() and the table generator can produce one')
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** The response to findings 5 and 13 made a design a class so that only `make()` and the table generator can produce one. But the generator is a host tool that writes source, so the door it uses, `from_table(coefficients, latency)`, is a public constexpr function that anyone can call with any numbers. Nothing validates them: not the format-specific stability check that 3.3 says `make()` performs, not the fixed-point guard-bit and Q2.30 limits 3.3 lists. The stored latency and tail are whatever the caller typed. The 'bit pin' protects the tables catenae ships, not an integrator's. For a safety reader this is calibration data entering without a plausibility check, at the one place the plan says failure lives.

**Recommendation.** Make `from_table` validate what can be validated without libm, as constexpr arithmetic: for a biquad the stability triangle (|a2| < 1, |a1| < 1 + a2) in the design's own format, and the fixed-point range limits of 3.3. A failed check calls the handler, which is not constexpr, so a bad table used to initialise a `constexpr` design is a compile error and a run-time one traps. State in 3.3 that this is the rule for every table entry point, and that latency and tail in a table are trusted numbers covered by the generator's pin.

**Second reviewer.** Reproduced (host.cpp): lowpass_design<float>::from_table({0.1, 0.2, 0.1, -2.5, 1.7}, 0) compiles as a constexpr, builds a chain, and the impulse response ends in -nan; from_table is public in sketch.h:272 and in the 9.2 listing, while the response's D37 row says only make() and the table generator can produce a design. It repeats the table half of the first audit's finding 5, where the second reviewer accepted 'generated, bit-pinned' as the vouching for catenae's own tables; what is new is that the door is public to an integrator. A constexpr stability check in from_table is cheap; the gap is real and small.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch audit2/build/misc/unstable.cpp: `constexpr auto k_bad = lowpass_design<float>::from_table({0.1f, 0.2f, 0.1f, -2.5f, 1.7f}, 0.0f);` (a2 = 1.7, poles outside the unit circle) compiles under g++-13 -std=c++17 -Wall -Wextra -Werror, builds `chain(lowpass<1>{k_bad})`, and after an impulse and 768 frames prints 'the impulse response is -nan'. PLAN.md lines 575-576 show `from_table` in the public section of the 9.2 listing; sketch.h line 272.

</details>

<a id="f49"></a>
### 49. Section 8's lists and section 11's card were not updated for D34, D37, D44 and D45 (rule names recalled from memory)

- **Lens:** build (Build, safety posture, documentation, milestones after the tidy)
- **Where:** Section 8 (deviation log, assumptions-of-use register), section 11.2 (the card), section 11.3 and 11.4 (chapters against milestones), sections 9.2 to 9.4 (static storage idiom)
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** What the accepted decisions add that the two lists in section 8 do not name. I am recalling MISRA C++:2023 and ISO 26262-6 from memory and the numbers may be wrong; the constructs are in the sketch. Deviations: (a) the central idiom of 9.2 to 9.4, `static state_of<...> s_x;`, `static auto s_strip`, `static mailbox<...>`, is mutable data with static storage duration, which I recall the 2023 edition restricts both for globals and for local statics (its storage-duration rules), and ISO 26262-6's unit-design table asks to avoid or justify; it is the integrator's code, but the library's examples, tests, guide and C ABI instances all teach it, and the only alternative offered is 'a caller's arena'. (b) `span` and `expected` 'with the standard's interface' need implicit single-argument constructors: five NOLINTs in sketch.h (lines 61, 65, 69, 217, 220), each a standing deviation under 7.3's own rule, and I recall a required rule that such constructors be explicit. (c) `take(Apply&& apply)` takes a forwarding reference and never forwards it. (d) As I recall, the 2023 edition gives no guidance on `<atomic>` or threads, so 'written with MISRA in mind' covers none of D19 and D44; the plan has tests for them but no written rule (which memory orders, and why) that a reviewer could inspect. Assumptions of use not in the register: a state outside static storage must be zeroed; one configuration serving many states means a setter retunes every voice; the amalgamation's contraction flag; lock-free byte atomics on the target. The card of 11.2 predates D34: it has no row for parameters (which setters exist, whether a change is smoothed, which D44 makes 'the stage's matter') and none for the configuration's size, although 3.2 now ends with 'accessors and setters'. Of the guide's nine chapters, 11.4 assigns five to C1; 'the audio callback' leads to designs and `expected`, but no design exists in the library until the kernels or C3; C0b ships a mailbox contract page with no mailbox header in C0b. Doxygen and mdBook are still untried.

**Recommendation.** Add to the deviation log's opening entries: static storage for states, configurations and mailboxes (with the compliant alternative the guide will show); implicit conversions in `span` and `expected`; `union` and placement new (first finding). Make `take` forward or take its callable by value. Add a rule that each junction and mailbox carries a requirement stating its memory orders and the argument for them. Add the four assumptions of use. Add 'Parameters' and 'Configuration' rows to the card. Move 'the audio callback' chapter's design half to the milestone that ships the first design, or put a toy design in C1.

**Second reviewer.** The checkable parts hold: five NOLINTs on converting constructors in sketch.h (61, 65, 69, 217, 220); take(Apply&&) calls apply as an lvalue; section 8's deviation and assumptions lists name none of static storage, implicit conversions, union, or zeroing a non-static state; the 11.2 card has no parameters row; C0b ships a mailbox contract page while the mailbox arrives at C1; M0 byte atomics are not lock-free. The MISRA and ISO 26262 content is from memory by the reviewer's own statement and cannot be checked here, and section 8 calls its list 'opening entries' and says no rule is quoted. Low-confidence list hygiene.

<details><summary>Evidence as reported (confidence: low)</summary>

`grep -n NOLINT sketch.h`: 5 hits, all converting constructors. sketch.h line 772 `bool take(Apply&& apply) noexcept` calls `apply(...)` as an lvalue. PLAN.md lines 478-486 are the two lists; lines 862-872 the card; line 159 the 'accessors and setters' row; lines 923-925 the C0b and C1 documentation rows against line 755-756. scratch audit2/build/misc/atom.cpp: `std::atomic<std::uint8_t>::is_always_lock_free` is false for -mcpu=cortex-m0 and cortex-m0plus with arm-none-eabi-g++ 13.2, true for m3, m4, m23, m33, m55. `which doxygen mdbook waf` returns nothing on this machine, so nothing in 11.2's 'untried' paragraph or the waf build could be run. No MISRA or ISO text was to hand.

</details>

<a id="f50"></a>
### 50. D42's fallback (one mono configuration over as many states as channels) is unsound for every junction

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** D42; section 2 ('Run-time channel counts' out); section 6.1; D45
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** D42 offers two ways to serve a run-time channel count; the second, running one mono configuration per channel, holds for stages (channels share only the schedule) and fails for junctions. N mono handoffs are N rings with N head/tail pairs: a pull that lands between the producer's pushes leaves the channels offset for good. N mono asynchronous converters are N clock servos. SampleRateTap's own async C ABI refuses the fan-out for that reason while rational's and bridge's use it.

**Recommendation.** Reword D42: the mono fan-out is for stages and chains; a junction is instantiated at the full channel count, and a caller with a run-time count instantiates the largest it needs. Say the same in the junction contract page and in the D45 manifest rules (async is exported at each count it serves, never fanned out).

**Second reviewer.** The facts hold: async's C ABI header (lines 84-87) refuses the fan-out because each channel would get its own clock servo, rational's and bridge's fan out bit-identically, and two mono rings pulled between two pushes stay offset by construction. But D42 speaks of 'one mono configuration over as many states', which is stage and chain vocabulary; a junction in the plan has neither a configuration nor a state, so 'unsound for every junction' attacks a reading the plan does not make. What is missing is one clarifying sentence in D42 and in the D45 manifest rules.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch/audit2/fit/fanout.cpp with the sketch's handoff<1, 1024> twice, the same signal on both, one consumer pull placed between the two pushes of block 0: 'block 0: left real 64, right real 0', then 'block 1: left[0] = 65, right[0] = 1 (offset 64 frames)' and the same offset on blocks 2 and 3. async/capi/tap_sr_async_capi.h lines 84 to 87: 'here that would give each channel a clock servo of its own, so another count is refused instead'; rational and bridge capi headers: any other count 'runs that many one-channel converters behind the handle ... bit-identical'.

</details>

<a id="f51"></a>
### 51. The async servo is double arithmetic with exp() on every pull: D36's link gate rejects the float async junction, and 7.1 misdescribes the servo

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** D36; section 7.3 (link-stage gate); section 7.1 table row 2 ('the asynchronous converter's servo ... explicitly instantiated for the four'); C8
- **Severity:** minor as reported, minor after verification
- **Verdict:** confirmed

**Problem.** D36's gate forbids a float-profile image from referencing a software-double routine or a double libm entry. The carried servo is one non-template class in double, independent of the sample type, and pull() runs it once per call with two or three std::exp calls, a double-to-int64 conversion and double status arithmetic. So the float async junction fails the gate as carried, in every sample format, and there are not four instantiations of a servo to compile. The plan lists the float accumulator as the only new numeric design in the port; the servo is a second one, and once it changes, async's Q15 and Q31 outputs can no longer be compared with SampleRateTap bit for bit either.

**Recommendation.** Record a decision: the servo is written for float (and stated as new numeric design in section 12, with its own measured numbers), or the gate has a named exemption for control-rate code. Correct 7.1: the servo does not depend on the storage type. State that async's comparison with SampleRateTap at C8 is to a tolerance in every format.

**Second reviewer.** Duplicate of finding 37, reproduced: pull and pi_servo::advance_hold reference exp and the __aeabi_d* family on the M33; pi_servo.h line 99 is 'class pi_servo' with no template and std::exp at lines 215 and 232, so 7.1's 'the asynchronous converter's servo ... explicitly instantiated for the four' misdescribes it. I did not re-run the float-servo comparison (the SNR figures), so that part is unverified.

<details><summary>Evidence as reported (confidence: high)</summary>

scratch/audit2/fit/servo_m33.cpp (pi_servo::update plus the eps conversion, as pull() calls them), arm-none-eabi-g++ -std=c++20 -Os -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard, nm -u: `exp`, `__aeabi_dadd`, `__aeabi_dmul`, `__aeabi_ddiv`, `__aeabi_dsub`, `__aeabi_d2lz`, `__aeabi_ui2d` and four double compares. pi_servo.h line 99 `class pi_servo` (no template), lines 215 and 232 std::exp. I also re-typed a scratch copy of pi_servo.h to float (variables, literals, pi) and ran the real converter (servo_cmp.cpp, 60 s, 997 Hz): it locks at the same times with zero underruns, and the fitted SNR is 108.6 / 99.1 / 79.9 / 91.0 / 80.5 dB against the double servo's 88.9 / 87.7 / 80.0 / 90.7 / 77.0 dB at +200, -200, +37, +950 ppm (32-frame pulls) and +200 ppm (240-frame pulls). So a float servo looks feasible but its output differs.

</details>

<a id="f52"></a>
### 52. The junction contract still has no place for async's consumer-side reset, its resync count, or a latency that changes at run time

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** Section 6.1 (reset(), report(), latency()); first-audit finding 34 (second reviewer); CF
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** The first audit's second reviewer listed reset_from_consumer() and effective_target_latency_frames as gaps; the response deferred them to CF and the tidy contract is unchanged: reset() 'only while neither side is running'. The engine's public API (C++ and C ABI) has a consumer-thread reset that runs while the producer keeps pushing, and its own underrun recovery is the same operation. report()'s listed fields omit resyncs and the effective setpoint. latency() is {nominal, range}, but async's nominal moves when pull() raises the setpoint, so it is a reading of consumer-written state, not a stored number, and the contract does not say which thread may call it.

**Recommendation.** Add to junction_traits: `reset_from_source()` (the pull side discards what is buffered and restarts; legal while the push side runs), a per-junction report type whose fields each meet the 32-bit atomic rule, and say that latency() is callable from any thread and may change between calls for an independent-clock junction.

**Second reviewer.** The source facts are true: converter.h has reset_from_consumer() at line 419, resyncs and effective_target_latency_frames in converter_status (lines 105-110), the setpoint raise in pull(), and the C ABI exports the consumer-side reset. But this is the first audit's finding 34 (second reviewer, items 3 and 4) again, and the plan's answer is the CF milestone, whose table of every public member of the three engines and 'what it forces on the contracts is changed here' exist for exactly this. Nothing new is shown beyond the contract row being unchanged; the 48-to-192 and 341 setpoint runs I did not repeat.

<details><summary>Evidence as reported (confidence: high)</summary>

async/include/tap/sr/async/converter.h line 419 `void reset_from_consumer() noexcept` (discards the ring from the read side, resets the servo), lines 105 and 110 (resyncs, effective_target_latency_frames), lines 344 to 358 (the raise); async/capi/tap_sr_async_capi.h lines 13 and 120. In my runs the effective target went from 48 to 192 (128-frame pulls) and to 341 (240-frame pulls), so designed latency changed by 3 to 6 ms after construction. PLAN.md line 295 for reset().

</details>

<a id="f53"></a>
### 53. D45's manifest: workable for the three C ABIs, but its size and what a named instantiation fixes are not stated

- **Lens:** fit (Do SampleRateTap's three engines fit the contracts as they now are)
- **Where:** D45; C6 ('The C ABI from its manifest, and the executed notebooks'); CF ('a first C ABI manifest'); section 11.1 (notebooks)
- **Severity:** minor as reported, minor after verification
- **Verdict:** partly

**Problem.** Each engine's C ABI today chooses chain, profile, format and channel count at run time behind one handle, and the notebooks use that: matrix.ipynb builds every chain at every profile and stages at matrix divisors; rational_comparison varies profile and format; the async notebooks vary target_latency_frames (48, 128, 480), which also changes the ring's size. With profiles as types (D37) and no handle (D45), parity is a list, not a function: 33 chains x 4 profiles x 3 formats x 2 native counts = 792 chain instantiations for rational alone, before single stages, the recursive stages and the guard band filter, each exporting a function set. D45 as tidied no longer says what an instantiation names (draft 32: 'its sample format, channel count and profile ... its sizes as constants'), and neither version says whether a design value such as async's target fill is a manifest constant or an argument of an exported make(), nor that a caller-supplied state needs an alignment constant as well as a size.

**Recommendation.** Say in D45: an instantiation names type-level choices only (stage or junction type, format, channel count, design type, capacity); run-time design values go through an exported make() per instantiation returning a status; each exports state size and alignment. Generate the manifest per notebook rather than aiming at parity with the handle ABIs, and state the expected count and build time at CF.

**Second reviewer.** Facts I checked hold: draft 32's D45 said an instantiation is 'a chain or a junction with its sample format, channel count and profile ... with its sizes as constants' and the tidied D45 dropped both phrases; the rational C ABI takes chain, profile, format and channels at run time; async sizes its ring as max(1024, 4 x (target + taps)). I did not re-run the 792 count or the 69 s compile. The plan never promises parity with the handle ABIs (D45 rules out run-time choice on purpose), so the count is an argument against a goal the plan does not state; the real gaps are small: what a manifest entry fixes, an alignment constant, and where run-time design values enter.

<details><summary>Evidence as reported (confidence: medium)</summary>

Read the three capi headers and bindings on origin/main (tap_sr_rational_create_format(chain, profile, format, channels); create_stage(L, M, profile, divisor_num, divisor_den, channels); tap_sr_async_create(sample_rate_hz, target_latency_frames, profile, channels)) and extracted the constructor calls from each notebook with a script. Compiled today's rational/capi/tap_sr_rational_capi.cpp from origin/main (g++-13 -O2): 69 s, 2,147,199 bytes of text, 96 basic_stage types, 27 exported functions, with the profile still a run-time value; profiles as types multiply the chain types by four. async auto-sizes its ring as max(1024, 4 x (target + taps)) frames (converter.h line 486), so target 480 needs a 4096-frame capacity where the default is 1024. No notebook uses a custom profile or an unpinned divisor.

</details>
