# catenae — plan (draft 2)

**catenae**: Chainable Audio Transforms for Embedded, Native & Accelerated Environments.

A C++17 header-only library of audio processing *stages* and the *chain* that composes them.
A stage has a contract stated as numbers (ratio, channels, layout, sample types, latency,
tail); a chain is a type the caller writes from stages, and it composes those numbers.

Status: draft for the maintainer's review. Nothing here is built. Each decision in section 1
is marked **settled** (the maintainer said so) or **proposed** (this draft's recommendation,
open until confirmed). Section 8 lists the questions that gate the first milestone.

Evidence: the layout spike of November 2026 (DspTap and SampleRateTap branches
`ccr-ef01ff18-nxz8n4`; DspTap `bench.yml` runs 153, 157, 161) and the write-up
"Interleaved or Planar: A Short Book for the Tap Chain". Section 7 says what those two
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
| D5 | The chain's native layout is interleaved, with the channel count a compile-time number. | proposed (measured: section 7) |
| D6 | A stage declares its input and output separately: sample type, channel count and layout on each side. | proposed |
| D7 | Planar is a typed region inside a chain: converter stages at its ends, a wrapper for convenience. The chain never accepts either layout at a joint. | proposed |
| D8 | Byte order and packing exist only in wire stages at the ends of a chain. Inside, every sample is native. | proposed |
| D9 | One latency report per stage (nominal, deviation, exactness, group delay) and one flush contract with a stated residue. | proposed |
| D10 | Capabilities are published as constants and checked by one `stage_traits<T>`; nothing detects a capability by probing for a member. | proposed (follows from D2) |
| D11 | Analysis (meters, correlation, pitch) is a sibling `analyzer` concept; `tap<Analyzer>` is the stage that puts one in a chain. Feature vectors are never passed off as audio channels. | proposed |
| D12 | The Tap libraries' engineering discipline carries over whole: contracts as numbers pinned by tests; double the golden model, float the embedded profile, Q15/Q31 opt-in per stage; geometry fixed at construction, processing `noexcept` and allocation-free; published literature only; honest limits stated and pinned; exceptions and RTTI optional. | proposed |
| D13 | A chain is written by the caller as a type. Nothing is looked up from a rate pair or built from a run-time description. | proposed (SampleRateTap's D12) |
| D14 | catenae is not part of the TapHouse family. Its namespace is `catenae::`, its style files are its own (started from a copy, free to diverge, no drift check against TapHouse), and its banner is `Copyright 2026 Timothy Place and the catenae contributors`. | settled (the namespace, the style's starting point and the banner are this draft's reading of it) |
| D15 | catenae and SampleRateTap coexist for good. `tap::dsp::chain` stays SampleRateTap's composition point; catenae's stage contract owes `sync_stage` nothing and may differ wherever that is better. Neither library depends on the other. | settled |

## 2. Charter

**In:** 1:1 stages (filters, gain, delay, mixing, dynamics without a side chain), rate-changing
stages by a compile-time ratio L/M, format and layout converters, the chain, analyzers and
their tap, and the measurement harness that tells us which layout each stage wants.

**Out, at least until 1.0:**

- Asynchronous rate conversion and clock recovery (SampleRateTap's `async`).
- Stages with two audio inputs (side-chain compressors, convolution against a live signal).
- A run-time graph: no dynamic routing, no plug-in host, no patch format.
- Pitch shifters and other processes with their own framing and latency model.
- Anything defined by imitating a product rather than by a published method.

## 3. The stage contract

What every stage declares, as compile-time constants unless noted. C++17 spelling: static
constexpr members and member types, read through `stage_traits<T>`.

| Member | Meaning |
|---|---|
| `input_sample`, `output_sample` | The arithmetic types on each side (`double`, `float`, `int16_t` Q15, `int32_t` Q31; `std::byte`-packed for a wire stage). |
| `k_channels_in`, `k_channels_out` | Frame widths. |
| `k_layout_in`, `k_layout_out` | `layout::interleaved` or `layout::planar`. |
| `k_up`, `k_down` | The rate ratio, reduced. |
| `k_in_place` | Whether `process` may read and write one buffer. |
| `k_exact_latency` | Whether the stage delays every frequency alike. |
| `process(in, n, out)` | n input frames in; returns frames written; `noexcept`, no allocation. |
| `outputs_for(n)`, `frames_needed(k)` | Exact, from the current position. |
| `reset()` | Back to the constructed state. |
| `latency()` | `{nominal, deviation}` in output frames; deviation 0 when exact. Plus `group_delay(f)`. |
| `tail_frames()`, `flush_residue()` | Zero input frames after which the output is silence within the residue (0 for FIR; a stated bound for a floating-point recursion). |

Open inside the contract (section 8): the planar `process` signature, and whether run-time
parameter changes are part of the contract or a per-stage matter.

## 4. The chain

- `chain<Stages...>`, built by `a | b | c`.
- Static checks at each joint: sample type, channel count and layout of one stage's output
  equal the next stage's input. One readable `static_assert` per mismatch.
- Composes ratio, `outputs_for`, `frames_needed`, latency, tail and residue. A chain is
  itself a stage, so chains nest.
- Scratch sized at construction; a run of `k_in_place` stages shares one buffer.
- Bit-identical for any chunking of a stream, under the stated floating-point contraction
  policy (the spike showed this claim needs `-ffp-contract=off` to hold across code paths;
  the policy is written down once and the test targets carry the flag).

## 5. Layout

- Interleaved, compile-time channel count, is the default for every stage with state.
- `deinterleave<S, Ch>` and `interleave<S, Ch>` are 1:1 stages.
- `planar_section<Stages...>` de-interleaves once per block, runs mono stages over contiguous
  channels, re-interleaves, and presents itself as an interleaved stage.
- Each stage's row in the layout benchmark is the evidence for its preferred layout.

## 6. Milestones

Each ends with its battery green on every CI leg and its numbers recorded.

| | Milestone | Contents |
|---|---|---|
| C0 | Skeleton | Repository layout, license, `NOTICE.md`, style files, CI: hosted builds at `-std=c++17` (and at 20 and 23, to prove nothing depends on 17's absence of a feature), the no-exceptions/no-RTTI build, the Cortex-M and Hexagon QEMU legs. `span`, `expects`/`raise`, `sample_traits`, `stage_traits`, the contract document. |
| C1 | The chain on trivial stages | `chain`, `gain`, integer `delay`, `interleave`/`deinterleave`, sample converters (`to<float>`, `to<int16_t>`). The instruction-count harness and the layout benchmark, carried from the spike. This milestone proves D6, D7, D9 and D10 before any filter depends on them. |
| C2 | The edges | Wire `pack`/`unpack` (16, 24-packed, 24-in-32, 32; either byte order), TPDF dither, the mix matrix. |
| C3 | Filters | The biquad block kernel, the biquad family and the Butterworth, a direct-form FIR, the FIR decimators and L/M stages. |
| C4 | Blocks | The FFT, the overlap-save FFT FIR, `planar_section`. |
| C5 | Analysis | `analyzer`, `tap`, peak/RMS meters, auto- and cross-correlation. |
| C6 | Profiles and freeze | Q15/Q31 for the stages that take them, accelerated back ends (vDSP, CMSIS-DSP, HVX) behind the golden contract, the C ABI and executed notebooks, the 1.0 contract freeze. |

## 7. What is carried in, and what moved

### Carried from DspTap (ported to C++17, each with its tests)

`sample_traits.h`, `detail/expects.h`, `subnormals.h`, `math.h`; `fir_kernels.h`, `kaiser.h`,
`nyquist.h`, `quantize.h`, `decimate.h`; `iir_design.h` (sections, group delay),
`iir_kernels.h` (the biquad cascade, per sample and per block); `butterworth.h`; the FFT
(`fft.h` and the srdif engine) at C4; `analysis/` for the test instruments; the bench
harness, the QEMU counting plugin (with the page-boundary fix found in the spike) and the
style files, as a starting point (D14).

From the spike branch only, not on DspTap's `main`: `fft_fir.h` and the layout benchmark.

From SampleRateTap: the L/M stage machine (`rational`'s `basic_stage`) as the model for
rate-changing stages, and its ratchet and ceilings scripts. Not its engines.

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

## 8. Open questions

Decided on 2026-11-24 and moved to section 1: catenae's identity (D14), its relationship
to SampleRateTap (D15) and strict C++17 (D2).

Gating C0:

1. **House rules.** D14 leaves the style open. This draft starts from the Tap naming and
   layout rules (snake_case types and functions, `m_` members, the same `.clang-format`
   and `.clang-tidy`), owned here. Say if catenae should look different.

Gating C1:

2. **The planar signature.** A pointer per channel (`const S* const*`), or one base pointer
   with a stride? The first matches every plug-in API; the second keeps one buffer.
3. **Run-time channel counts.** Are they in the charter at all? If so, the spike says to
   serve them through planar stages, where a run-time count costs under 2 %.
4. **Run-time parameters.** Part of the stage contract from the start (when a change takes
   effect, whether it is smoothed), or left to each stage as `set_cutoff()` is today?

Gating later milestones:

5. **Rate-changing stages.** In at C3 as drafted, or is catenae 1:1 only? Since the two
   libraries coexist (D15), a caller who needs a high-quality converter has SampleRateTap;
   the question is whether catenae also carries simple L/M stages (decimate by 2 before an
   analyzer, oversample around a nonlinearity) so that such a chain needs one library.
6. **Fixed point.** Q15/Q31 from each stage's first version, or as a C6 pass?
7. **The FFT.** Carry the srdif engine and its fixed-point profiles whole, or only the
   floating engine the FFT FIR needs?
