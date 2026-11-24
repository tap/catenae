# catenae — plan (draft 5)

**catenae**: Chainable Audio Transforms for Embedded, Native & Accelerated Environments.

A C++17 header-only library of audio processing *stages*, the *chain* that composes them,
and the *junctions* that join chains across threads and clocks. A stage has a contract stated
as numbers (ratio, channels, layout, sample types, latency, tail); a chain is a type the
caller writes from stages, and it composes those numbers; a junction has a contract of the
same kind for the place where two call sites meet.

Status: draft for the maintainer's review. Nothing here is built. Each decision in section 1
is marked **settled** (the maintainer said so) or **proposed** (this draft's recommendation,
open until confirmed). Section 9 lists the questions that gate the first milestone.

Evidence: the layout spike of November 2026 (DspTap and SampleRateTap branches
`ccr-ef01ff18-nxz8n4`; DspTap `bench.yml` runs 153, 157, 161) and the write-up
"Interleaved or Planar: A Short Book for the Tap Chain". Section 8 says what those two
libraries have changed since and what that does to the evidence.

---

## 0. Why a new library

`tap::dsp::chain` was built as the composition point for SampleRateTap's rate converters. Its
stage contract assumes one sample type, one channel count and interleaved frames, in and out.
That is right for a converter and too narrow for a general library of stages: it cannot
express a format converter, a layout converter, a mixer or an analysis tap. Widening it in
place would break every consumer of `chain.h` for needs those consumers do not have. catenae
starts from the wider contract instead, in C++17, with no compatibility debt.

## 1. Decisions

| # | Decision | Status |
|---|---|---|
| D1 | The name is `catenae`; the repository is `tap/catenae`. | settled |
| D2 | The language is C++17, strictly: one code path, and every header compiles to the same thing at `-std=c++17`, 20 and 23. No C++20 feature appears in any header, not even behind a feature test. Where the standard library only has a type from C++20 on, catenae writes its own (`catenae::span` first). | settled |
| D3 | DspTap and SampleRateTap are sources of material, not dependencies: no submodule, no link. Code is carried in by copy, ported to C++17, and recorded in `NOTICE.md` with the commit it came from. | settled |
| D4 | Header-only, standard library only, MIT. | proposed (the Tap libraries' rule) |
| D5 | The chain's native layout is interleaved, with the channel count a compile-time number. | proposed (measured: section 8) |
| D6 | A stage declares its input and output separately. Each side is a *port*: sample type, channel count, layout and signal kind (D17). | proposed |
| D7 | Planar is a typed region inside a chain: converter stages at its ends, a wrapper for convenience. The chain never accepts either layout at a joint. | proposed |
| D8 | Byte order and packing exist only in wire stages at the ends of a chain. Inside, every sample is native. | proposed |
| D9 | One latency report per stage (nominal, deviation, exactness, group delay) and one flush contract with a stated residue. | proposed |
| D10 | Capabilities are published as constants and checked by one `stage_traits<T>`; nothing detects a capability by probing for a member. | proposed (follows from D2) |
| D11 | Read-only analysis (meters, correlation, pitch) is a sibling `analyzer` concept; `tap<Analyzer>` is the stage that puts one in a chain, passing the audio through. Its results are read beside the chain, never passed down it. | proposed |
| D12 | The Tap libraries' engineering discipline carries over whole: contracts as numbers pinned by tests; double the golden model, float the embedded profile, Q15/Q31 opt-in per stage; geometry fixed at construction, processing `noexcept` and allocation-free; published literature only; honest limits stated and pinned; exceptions and RTTI optional. | proposed |
| D13 | A chain is written by the caller as a type. Nothing is looked up from a rate pair or built from a run-time description. | proposed (SampleRateTap's D12) |
| D14 | catenae is not part of the TapHouse family. Its namespace is `catenae::`, and its banner is `Copyright 2026 Timothy Place and the catenae contributors`. | settled (the namespace and the banner are this draft's reading of it) |
| D15 | catenae and SampleRateTap coexist for good. `tap::dsp::chain` stays SampleRateTap's composition point; catenae's stage contract owes `sync_stage` nothing and may differ wherever that is better. Neither library depends on the other. | settled |
| D16 | Once the architecture is settled and stable, catenae duplicates all of SampleRateTap's functionality: the synchronous L/M stages and chains (`rational`), the 44.1 ↔ 48 kHz converter (`bridge`) and the asynchronous converter (`async`). They are re-implemented here on catenae's contract, not wrapped and not linked (D3, D15). The architecture is therefore designed so that they fit, and is not called stable until it is shown that they do. | settled |
| D17 | A port carries a *signal kind*: time-domain audio or frames of transform coefficients. The chain checks it at every joint, so analysis, coefficient-domain stages and synthesis form a typed region in a chain (a Gabor or short-time Fourier analysis, a spectral gain, the synthesis), as planar does (D7), and a time-domain stage after an analysis does not compile. | proposed |
| D18 | The stage contract is fixed-ratio: every stage's ratio is a compile-time L/M, and `outputs_for` is exact. Nothing in it is reserved for a ratio that moves. | proposed |
| D19 | A *junction* is a first-class concept beside the stage, with its own contract (section 6): two ports and two entry points, `push` and `pull`, for the place where two threads or two clocks meet. The asynchronous converter is a junction, not a stage. A chain never contains a junction; the caller writes one between two chains. | proposed |
| D20 | The naming and formatting rules are the Tap rules (the standard library's conventions: snake_case types and functions, `k_` constants, `m_` members, PascalCase template parameters only), enforced by `.clang-format` and `.clang-tidy` in CI. The files are copied in once and owned here: no drift check against TapHouse (D14). The reasons: carried code ports with only the changes C++17 forces, fixes move between catenae and the Tap libraries nearly verbatim for as long as they coexist (D15), and the surface is standard-library-shaped (`catenae::span`, `stage_traits<T>`). | settled |

## 2. Charter

**In:** 1:1 stages (filters, gain, delay, mixing, dynamics without a side chain), rate-changing
stages by a compile-time ratio L/M, format and layout converters, transform-domain regions
(analysis, coefficient stages, synthesis), the chain, analyzers and their tap, junctions, and
the measurement harness that tells us which layout each stage wants.

**In, after the architecture is stable (D16):** everything SampleRateTap does. Its three
engines arrive in the order of their distance from the stage contract: `rational` first (its
stages are already chain stages), then `bridge` (one fixed-ratio converter), then `async`
(a junction, not a stage: section 6).

**Out, at least until 1.0:**

- Stages with two audio inputs (side-chain compressors, convolution against a live signal).
- A run-time graph: no dynamic routing, no plug-in host, no patch format.
- Pitch shifters and other processes with their own framing and latency model.
- Elastic processes inside one clock domain (time-stretch, varispeed). They are the only
  other users a variable-ratio stage would have, and D18 leaves them out with it.
- Anything defined by imitating a product rather than by a published method.

## 3. The stage contract

What every stage declares, as compile-time constants unless noted. C++17 spelling: static
constexpr members and member types, read through `stage_traits<T>`.

| Member | Meaning |
|---|---|
| `input_sample`, `output_sample` | The arithmetic types on each side (`double`, `float`, `int16_t` Q15, `int32_t` Q31; `std::byte`-packed for a wire stage). |
| `k_channels_in`, `k_channels_out` | Frame widths. |
| `k_layout_in`, `k_layout_out` | `layout::interleaved` or `layout::planar`. |
| `k_kind_in`, `k_kind_out` | `kind::audio` or `kind::coefficients` (D17). |
| `k_up`, `k_down` | The rate ratio, reduced. For an analysis stage it is 1 / hop. |
| `k_in_place` | Whether `process` may read and write one buffer. |
| `k_exact_latency` | Whether the stage delays every frequency alike. |
| `process(in, n, out)` | n input frames in; returns frames written; `noexcept`, no allocation. |
| `outputs_for(n)`, `frames_needed(k)` | Exact, from the current position. |
| `max_outputs_for(n)` | An upper bound from any state, `constexpr`. The chain sizes its scratch from this, not from the ratio, so the ratio is never wired into a buffer size. |
| `reset()` | Back to the constructed state. |
| `latency()` | `{nominal, deviation}` in output frames; deviation 0 when exact. Plus `group_delay(f)`. |
| `tail_frames()`, `flush_residue()` | Zero input frames after which the output is silence within the residue (0 for FIR; a stated bound for a floating-point recursion). |

The first four rows describe a port. A stage has two, in one call on one thread.

Open inside the contract (section 9): the planar `process` signature, how a frame of
coefficients is laid out, and whether run-time parameter changes are part of the contract or
a per-stage matter.

## 4. The chain

- `chain<Stages...>`, built by `a | b | c`.
- Static checks at each joint: the output port of one stage equals the input port of the
  next (sample type, channel count, layout, signal kind). One readable `static_assert` per
  mismatch.
- Composes ratio, `outputs_for`, `frames_needed`, latency, tail and residue. A chain is
  itself a stage, so chains nest.
- Scratch sized at construction from each stage's `max_outputs_for`; a run of `k_in_place`
  stages shares one buffer.
- One call, one thread, one clock. A chain holds no junction (D19).
- Bit-identical for any chunking of a stream, under the stated floating-point contraction
  policy (the spike showed this claim needs `-ffp-contract=off` to hold across code paths;
  the policy is written down once and the test targets carry the flag).

## 5. Layout and signal kind

- Interleaved, compile-time channel count, is the default for every stage with state.
- `deinterleave<S, Ch>` and `interleave<S, Ch>` are 1:1 stages.
- `planar_section<Stages...>` de-interleaves once per block, runs mono stages over contiguous
  channels, re-interleaves, and presents itself as an interleaved stage.
- Each stage's row in the layout benchmark is the evidence for its preferred layout.
- A transform-domain region is built the same way (D17): an analysis stage (audio in,
  coefficient frames out, ratio 1 / hop), stages whose ports are both coefficients, and a
  synthesis stage back to audio. The pair states its reconstruction error and its round-trip
  latency as numbers. DspTap's `subband.h` is the prior art for that statement.

## 6. Junctions

A stage lives inside one call, on one thread, in one clock domain. A junction is where two
call sites meet: two threads, two clocks, or both. It is defined the way a stage is, by a
contract of constants and numbers, read through `junction_traits<T>`.

| Member | Meaning |
|---|---|
| The sink port and the source port | The same four constants as a stage's ports, once per side. |
| `k_clocks` | `clocks::shared` (two threads on one clock) or `clocks::independent`. |
| `k_up`, `k_down` | The nominal rate ratio between the two sides, a compile-time number (1:1 for a near-unity converter). |
| `tolerance_ppm()` | Independent clocks only: the offset and drift the junction absorbs. |
| `push(in, n)` | Called by exactly one thread. Returns the frames accepted; what happens to the rest (dropped, counted) is stated. `noexcept`, wait-free, no allocation. |
| `pull(out, k)` | Called by exactly one other thread. Always writes k frames; the frames it could not supply are silence, and it returns how many were real. Same guarantees. |
| `status()` | Any thread: state (filling, locked), fill, underruns, overruns, the rate estimate where there is one. |
| `latency()` | `{nominal, range}` in source frames: a junction's delay breathes with its fill, so it is never exact. |
| `capacity_frames()` | Fixed at construction. |
| `reset()` | Only while neither side is running; a precondition, stated. |

What makes it worth a concept is that it has more than one instance:

- `handoff<S, Ch>`: a single-producer, single-consumer ring. Shared clock, two threads (an
  audio callback feeding a worker). The trivial junction, and the one that proves the
  contract, as `gain` proves the stage's.
- The asynchronous converter (D16): independent clocks, a ring, a servo and a fractional
  resampler.
- Later, outside the charter today: a jitter buffer.

Joining chains to a junction: `feeding(chain, junction)` runs the chain and pushes what it
wrote; `fed_by(junction, chain)` pulls what the chain needs for k outputs and runs it. Each
checks the ports where it joins, as a chain checks a joint. (A known detail: a chain with an
interpolating stage can write more than k frames from `frames_needed(k)` inputs, so the
pull side keeps a carry of at most L - 1 frames.) There is deliberately no `|` across a
junction and no type that holds both sides' chains: the thread boundary stays visible in the
caller's code, which is SampleRateTap's rule that the caller declares clock topology by
choosing a type.

Latency across a junction composes as nominal only: chain, junction, chain.

## 7. Milestones

Each ends with its battery green on every CI leg and its numbers recorded.

| | Milestone | Contents |
|---|---|---|
| C0 | Skeleton | Repository layout, license, `NOTICE.md`, style files, CI: hosted builds at `-std=c++17` (and at 20 and 23, to prove nothing depends on 17's absence of a feature), the no-exceptions/no-RTTI build, the Cortex-M and Hexagon QEMU legs. `span`, `expects`/`raise`, `sample_traits`, the port, `stage_traits` and `junction_traits`, the contract documents for both. |
| C1 | The chain on trivial stages | `chain`, `gain`, integer `delay`, `interleave`/`deinterleave`, sample converters (`to<float>`, `to<int16_t>`). The instruction-count harness and the layout benchmark, carried from the spike. `handoff`, with `feeding` and `fed_by`. This milestone proves D6, D7, D9, D10 and D19 on the simplest instances, before any filter or converter depends on them. |
| C2 | The edges | Wire `pack`/`unpack` (16, 24-packed, 24-in-32, 32; either byte order), TPDF dither, the mix matrix. |
| C3 | Filters | The biquad block kernel, the biquad family and the Butterworth, a direct-form FIR, the FIR decimators and L/M stages. |
| C4 | Blocks | The FFT, the overlap-save FFT FIR, `planar_section`. |
| C5 | Analysis and the transform domain | `analyzer`, `tap`, peak/RMS meters, auto- and cross-correlation. The coefficient signal kind (D17) on its first region: a Gabor (short-time Fourier) analysis and synthesis pair with a spectral gain between them. |
| C6 | Profiles | Q15/Q31 for the stages that take them, accelerated back ends (vDSP, CMSIS-DSP, HVX) behind the golden contract, the C ABI and executed notebooks. |
| C7 | The synchronous converters (D16) | `rational`'s stages, named chains and profiles; `bridge`. Each engine's battery is carried with it, and its output is compared against SampleRateTap's as an independent reference. |
| C8 | The asynchronous converter (D16) | `async`, as a junction (D19). |
| C9 | Freeze | The 1.0 contract freeze, once C7 and C8 have shown the architecture holds all of it. |

C7 and C8 do not start until the maintainer calls the architecture stable. What C0 and C1 owe
them is narrower: a stage contract `rational` and `bridge` fit, and a junction contract, proven
on `handoff`, that `async` fits.

## 8. What is carried in, and what moved

### Carried from DspTap (ported to C++17, each with its tests)

`sample_traits.h`, `detail/expects.h`, `subnormals.h`, `math.h`; `fir_kernels.h`, `kaiser.h`,
`nyquist.h`, `quantize.h`, `decimate.h`; `iir_design.h` (sections, group delay),
`iir_kernels.h` (the biquad cascade, per sample and per block); `butterworth.h`; the FFT
(`fft.h` and the srdif engine) at C4; `analysis/` for the test instruments; the bench
harness, the QEMU counting plugin (with the page-boundary fix found in the spike) and the
style files, as a starting point (D14).

From the spike branch only, not on DspTap's `main`: `fft_fir.h` and the layout benchmark.

From SampleRateTap: `async`'s `spsc_ring.h` for `handoff` at C1; the L/M stage machine (`rational`'s `basic_stage`) as the model for
rate-changing stages at C3, and its ratchet and ceilings scripts. Its engines come later and
whole (D16, C7 and C8), with their plans' settled decisions read first: the rule that the
caller declares clock topology by choosing a type and nothing routes by rate, the profile
vocabulary, the cross-validation's independence.

The port is mostly mechanical: concepts become traits and `static_assert`s, `std::span`
becomes `catenae::span`, `std::numbers` and `std::bit_cast` get local equivalents. The
compile-time filter designs need checking one by one; a design that cannot be evaluated in
C++17 constexpr becomes a generated, committed table with a bit pin.

### What moved since the spike was measured

DspTap went from 0.3.0 to 0.7.0, SampleRateTap from 0.8.0 to 0.9.0 and past it:

- **The Butterworth stage is on DspTap's `main`** (0.6.0), with `set_cutoff()` redesigning it
  in place (0.7.0): a first run-time parameter, with its cost measured.
- **`biquad_cascade_block`** (after 0.6.0): the floating cascade a block at a time, the
  section count and channel count as template arguments. It runs at 0.26 to 0.52 of the
  per-sample kernel's instructions at orders 2 to 8 on the M4F, M33, M55 and Hexagon. This
  is the fix the spike asked for.
- **A chain reports `window_frames()`** (0.5.0), so a chain that holds a chain flushes.
- **SampleRateTap 0.9.0** shipped `rational`'s recursive stages, the guard band filter and
  their C ABI.

Consequence for the evidence: the spike's Butterworth layout rows describe two kernels that
are both superseded. The FIR, FFT FIR, converter and gain rows stand. The Butterworth's
layout must be measured again on the block kernel, which C3 does as a matter of course.

## 9. Open questions

Decided on 2026-11-24 and moved to section 1: catenae's identity (D14), its relationship
to SampleRateTap (D15), strict C++17 (D2), and that catenae will in time duplicate all of
SampleRateTap (D16), which also settles that rate-changing stages are in. Draft 4 answers
draft 3's question about a variable ratio with D18 and D19 (proposed): no, and a junction.

Gating C0: nothing. The style is settled (D20).

Gating C1:

1. **The planar signature.** A pointer per channel (`const S* const*`), or one base pointer
   with a stride? The first matches every plug-in API; the second keeps one buffer.
2. **Run-time channel counts.** Are they in the charter at all? If so, the spike says to
   serve them through planar stages, where a run-time count costs under 2 %.
3. **Run-time parameters.** Part of the stage contract from the start (when a change takes
   effect, whether it is smoothed), or left to each stage as `set_cutoff()` is today?

Gating later milestones:

4. **A frame of coefficients.** How a transform-domain port lays out its data: bins as
   channels of a complex sample type, or a frame type of its own? Decided before C5; the
   port's four constants are meant to hold either.
5. **Analyzer results across threads.** An analyzer read from a non-audio thread crosses
   the boundary a junction is for. Does `tap` hand results over through a `handoff`, or
   does each analyzer state its own rule?
6. **Fixed point.** Q15/Q31 from each stage's first version, or as a C6 pass?
7. **The FFT.** Carry the srdif engine and its fixed-point profiles whole, or only the
   floating engine the FFT FIR needs?
