# The response to the audit of draft 19

The audit is `2026-11-24-plan-draft-19.md`. This file records what was done about it, so
that `PLAN.md` can state only what is decided. It is a record: it is not kept up to date
with the plan, and where the two differ the plan is right.

## The six decisions the audit asked for

Each row is a problem the audit confirmed, what was decided, and what else was considered.
All six were accepted by the maintainer on 2026-11-24, D38 in the revised form shown.
"Section" numbers are those of the plan at draft 32 (commit `12849d4`).

| # | The problem | Recommendation | Other options |
|---|---|---|---|
| D34 (accepted) | **Where a chain's memory lands.** An object with one non-zero member (a gain, a coefficient, a pointer to taps) is initialised data: its scratch and histories, nearly all zeros, are stored in flash as well as RAM (8,180 bytes measured for the overdrive chain of 9.3). The alternative is construction at start-up, with a 12 to 20 KB stack frame, which `a \| b \| c` over objects that carry their histories makes worse. (Findings 8, 10, 17, 36.) | **Split configuration from state.** A stage's *configuration* is a small constant value (its design, a view of its taps): `constexpr`, in ROM. Its *state* (histories, scratch) is a separate type that is valid when all zero, so it lives in `.bss` and costs no flash and no start-up code. `a \| b \| c` composes configurations, which are a few bytes each; `state_of<Chain>` is the memory. One configuration can serve many states (voices), and a run-time parameter change becomes "swap the configuration", which also answers how a new design reaches a running chain. | (a) Keep one object and accept flash equal to its size for every chain with a non-zero member. (b) One object, built in place at start-up from designs, with the start-up stack and ordering that implies. |
| D35 (accepted) | **A 1:1 ratio is not n frames out for n in.** `stft \| ... \| istft` with a hop of 128, fed 64 frames, returns 0 then 128; a by-3 down then up returns 66, 63, 63. The fixed-extent entry cannot compile for such a chain, a callback that must return n for n cannot use it, and `fed_by` overshoots: a chain by 16 asked for 100 frames writes 112. SampleRateTap's `pull(out, k)` stops at exactly k; the stage contract has no such form. (Findings 23, 33, 37.) | **State it, and provide the two adapters.** (1) A stage publishes `is_frame_synchronous` (`outputs_for(n)` is always n L/M); the fixed-extent entry and an n-for-n callback require it. (2) `steady<Chain>` wraps a bursting chain with a member FIFO and presents a frame-synchronous stage with its true, larger latency (511 frames for the 512/128 region, not 384); it is how a spectral region meets an audio callback. (3) `fed_by` owns a holdover array of the chain's worst overshoot, a `constexpr` number from the traits, and so delivers exactly k: that is where SampleRateTap's `pull` lands. `frames_needed(k)` is defined as the smallest n with `outputs_for(n) >= k`. | Add an output-bounded `run` to every stage (it may leave input unconsumed), and redo the chain's scratch proof for banked outputs. More general; it complicates every stage for the sake of two adapters. |
| D36 (accepted) | **The float default runs in software double on two targets.** The carried FIR arithmetic accumulates a `float` filter in `double`, on purpose and with explicit casts no warning sees. On the Cortex-M4F and M33 (single-precision FPUs) every FIR, `up2` / `down2`, L/M stage and converter would do a software-double multiply-add per tap; DspTap's own header records about 19 times Q15's instruction count on the M33. That contradicts D24 and the purpose section 7.2 gives for the double-promotion warning. (Findings 1, 26.) | **The `float` profile accumulates in `float`.** It is the profile for cores with a single-precision FPU, so it must not leave it. Its noise floor is measured and stated per stage (lower than the carried one; `_f64` stays the golden model and the choice for a host). C7's comparison with SampleRateTap is then to a tolerance for `float` and bit for bit only where the arithmetic is unchanged (`double`, Q15, Q31). A link-stage gate enforces it: a `float`-profile image on those cores may not reference a software-double routine. | Keep `double` accumulation as carried, record it as an exception, and tell M4F and M33 users to use fixed point, which pulls the fixed-point profiles forward from C6. |
| D37 (accepted) | **A design is more than "a small value".** (1) A design's validity depends on the sample format: a cutoff that is stable in `double` can lose a pole in `float`, and the tail is computed from the rounded coefficients. A format-free design cannot give the infallible constructor section 3.3 promises. (2) `bridge` and `async` design tables of 12 to 164 KB from run-time arguments, using up to 328 KB of `double` scratch; such a table cannot be returned by value, and its tap count sizes the stage's storage. (3) `latency()` as one report recomputes a 1,025-point group-delay scan that DspTap split out for its cost (28 M instructions on the M33). (Findings 5, 11, 12, 13, 35, 55.) | **Designs carry their format, hold their numbers, and come in two kinds.** `butterworth_design<S, N>`: `make()` validates for that `S` and stores coefficients already in the kernel's coefficient type, with the tail and the latency numbers computed once. So a stage built from a ROM design links no `double` and no `libm`, and `latency()` returns stored numbers; `group_delay(f)` is a design-time free function, not a stage member. *Small* designs are returned by value. *Table* designs are generated ROM tables with a bit pin, or are built by `make(spec, table, workspace)` into storage the caller supplies, with `constexpr` size functions. Where a profile fixes a tap count that sizes storage, the profile is a template argument. A design is a class with a `constexpr` constructor, not an open aggregate, so only `make()` and the table generator can produce one. | Keep format-free designs and let the stage constructor validate and fail, which gives up "a stage cannot fail". |
| D38 (accepted) | **Fixed-point resynthesis does not survive fixed scaling in 16 bits, and is limited in 32.** A fixed-scaling forward transform followed by its inverse returns the signal about log2 N + 1 bits down (Q15) or log2 N + 3 (Q31). Measured in the audit on DspTap's engine at N = 512, as signal-to-noise for a full-scale signal and one 20 dB below it: Q15 36 dB and 17 dB; Q31 112 dB and 92 dB. The floor is fixed, so the ratio falls dB for dB with the signal's level and with every doubling of N (Q15 at 2048: 24 dB and 3 dB). This is a property of fixed scaling, not of a back end: CMSIS-DSP's fixed-point transforms have only that kind (its Q31 round trip was not measured). The one remedy that exists is catenae's own engine's block-floating policy, which measured 81 to 88 dB in Q15 and about 150 dB in Q31, and which needs an exponent to travel with every frame. `float` and `double` are unaffected on every back end. (Finding 25.) | **At 1.0: Q15 spectra are for analysis only; Q31 resynthesis is offered under fixed scaling with its floor stated per length.** A Q15 or Q31 `stft` feeds analyzers and magnitude consumers, its format in the spectrum's type (D29), on the own engine or CMSIS-DSP. A Q31 `istft` is allowed, on either, with the bits it loses stated as a contract number for each length and the level dependence said plainly; a Q15 `istft` does not compile. Block floating, which only the own engine has, waits for a design of where the exponent travels and what a spectral stage does when two frames' exponents differ. | (a) Fixed-point transform regions are analysis only in both widths, the simplest rule. (b) Carry block floating now, own engine only, and settle the exponent's place in the frame. |
| D39 (accepted) | **Latency needs an exact form, and a 4x oversampler is half a sample out.** A linear-phase half-band's delay is odd at its own rate, so `up2 \| up2 \| ... \| down2 \| down2` has a latency of k + 1/2 base-rate frames (18.5 and 23.5 measured on SampleRateTap's chains), and "delay the dry path by the nominal latency" cannot be done with an integer delay. A pair of doubles also cannot pin "this latency is exactly 80/3". (Findings 27, 55.) | **An exact stage reports an exact rational.** `latency()` returns `{nominal, deviation}` as numbers for every stage, and a stage with `has_exact_latency` also has `exact_latency()`, a reduced rational, which is what a chain of exact stages composes and what a test pins. `oversample<Factor, Stage>` builds the up and down stages with the one-frame delay at the inner rate that makes its latency a whole number of base-rate frames; bare `up2` / `down2` beyond 2x are documented as fractionally delayed. | Doubles only, with the oversampler's delay left to the caller. |


## Where each finding went


Numbers are the audit file's. "Section" numbers are draft 32's.

| Theme | Findings | In this draft |
|---|---|---|
| Where a chain's memory lands | 8, 10, 17, 36 | D34 (accepted); `docs/sketches/config-state/` |
| Per-call behaviour, pull, overshoot | 23, 33, 37, 50 | D35 (accepted); sections 3.2, 3.3, 6; the same sketch |
| The float accumulator | 1, 26, 31 | D36 (accepted); the link-stage gate in 7.2 |
| What a design is | 5, 11, 12, 13, 35, 55 | D37 (accepted) |
| Fixed point | 3, 9, 16, 24, 25, 49, 58 | D38 (accepted); D29 and section 3.1; the sketch of 9.5 |
| Latency | 27, 55 | D39 (accepted) |
| One level per image | 15, 28, 41, 57 | Section 3.3: an explicit option, an inline namespace, no default handler |
| The stage and chain contracts | 6, 42, 43, 45, 54, 56, 63 | Sections 3.2, 3.3 and 4; D42 |
| Junctions | 22, 50 | Section 6: `report()`, the sanitizer and hardware legs, staging |
| Missing contracts | 7, 38, 61, 62 | `flush` in section 4; analyzers in 6.1; D43, D44 and D45 |
| Back ends | 4, 14, 32, 53 | Sections 5.2 and 7.1 |
| Warnings and tools | 30, 31, 46, 47, 59 | Section 7.2; D2, D30 |
| The safety posture | 18, 19, 20, 21, 51, 52 | Section 8 |
| Milestones | 2, 29, 34, 39 | Section 10: C0 split, CF added, the graph and tables reconciled; section 13 |
| The source libraries | 40, 44, 48 | Sections 7 and 12 |
| Documentation | 60 | Section 11 |

Not acted on: none of the 63 was refuted. Fifteen were judged partly right; in each case
the change above follows the second reviewer's narrower reading.

## The evidence the plan started from

The layout spike of November 2026: DspTap and SampleRateTap branches `ccr-ef01ff18-nxz8n4`;
DspTap `bench.yml` runs 153, 157 and 161 (the per-core tables are in DspTap's
`bench/README.md`, "Layout", and SampleRateTap's `tools/layout/README.md` on that branch);
and the write-up "Interleaved or Planar: A Short Book for the Tap Chain".

### What moved in the source libraries after the spike was measured

DspTap went from 0.3.0 to 0.7.0, SampleRateTap from 0.8.0 to 0.9.0 and past it:

- **The Butterworth stage is on DspTap's `main`** (0.6.0), with `set_cutoff()` redesigning it
  in place; 0.7.0 made that cheaper by taking the group-delay scan out of it.
- **The layout benchmark is on DspTap's `main`** too.
- **`biquad_cascade_block`** (0.7.0): the floating cascade a block at a time, the section count and
  channel count as template arguments, at 0.26 to 0.52 of the per-sample kernel's
  instructions at orders 2 to 8 on the M4F, M33, M55 and Hexagon. The fix the spike asked for.
- **A chain reports `window_frames()`** (0.5.0), so a chain that holds a chain flushes.
- **SampleRateTap 0.9.0** shipped `rational`'s recursive stages, the guard band filter and
  their C ABI.

Consequence for the evidence: the spike's Butterworth layout rows describe two kernels that
are both superseded; the FIR, FFT FIR, converter and gain rows stand. C3 measures the
Butterworth's layout again on the block kernel.

