# catenae — plan (draft 8)

**catenae**: Chainable Audio Transforms for Embedded, Native & Accelerated Environments.

A C++17 library of audio processing *stages*, the *chain* that composes them, and the
*junctions* that join chains across threads and clocks. A stage has a contract stated as
numbers (ports, ratio, latency, tail); a chain is a type the caller writes from stages, and it
composes those numbers; a junction has a contract of the same kind for the place where two
call sites meet.

Status: draft for the maintainer's review. Nothing here is built, and every code listing in
section 9 is a proposal that has not been compiled. Section 12 lists the open questions.

Evidence: the layout spike of November 2026 (DspTap and SampleRateTap branches
`ccr-ef01ff18-nxz8n4`; DspTap `bench.yml` runs 153, 157, 161) and the write-up
"Interleaved or Planar: A Short Book for the Tap Chain". Section 11 says what those two
libraries have changed since and what that does to the evidence.

---

## 0. Why a new library

`tap::dsp::chain` was built as the composition point for SampleRateTap's rate converters. Its
stage contract assumes one sample type, one channel count and interleaved frames, in and out.
That is right for a converter and too narrow for a general library of stages: it cannot
express a format converter, a layout converter, a mixer, a spectral region or an analysis
tap. Widening it in place would break every consumer of `chain.h` for needs those consumers
do not have. catenae starts from the wider contract instead, in C++17, with no compatibility
debt, and with the constraints of safety-related embedded code designed in from the first
header rather than retrofitted.

## 1. Decisions

Status: **settled** is the maintainer's own statement. **accepted** is a proposal the
maintainer reviewed on 2026-11-24 ("I mostly agree") without objecting to that row; it stays
open to change. **proposed** is new in this draft or not yet reviewed.

| # | Decision | Status |
|---|---|---|
| D1 | The name is `catenae`; the repository is `tap/catenae`. | settled |
| D2 | The language is C++17, strictly: one code path, and every file compiles to the same thing at `-std=c++17`, 20 and 23. No C++20 feature appears, not even behind a feature test. Where the standard library only has a type from C++20 on, catenae writes its own (`catenae::span` first). | settled |
| D3 | DspTap and SampleRateTap are sources of material, not dependencies: no submodule, no link. Code is carried in by copy, ported, and recorded in `NOTICE.md` with the commit it came from. | settled |
| D4 | MIT. Not header-only: templates whose compile-time numbers matter stay in headers, and everything else is compiled once into a static library, `catenae` (section 7). The FFT is behind one compiled interface, its back end chosen when the library is built. | settled in principle (the maintainer rejected header-only); the split is accepted |
| D5 | The chain's native layout is interleaved, with the channel count a compile-time number. | accepted (measured: section 11) |
| D6 | A stage declares its input and output separately, each as a *port type* (section 3). Two stages join when one's output port and the next one's input port are the same type. | accepted; the port-as-a-type form is proposed |
| D7 | Planar is a typed region inside a chain: converter stages at its ends, a wrapper for convenience. | accepted |
| D8 | Byte order and packing exist only in *edge stages* at the ends of a chain: `unpack` and `pack`. They are stages, not junctions: one call, one thread, one clock, ratio 1:1. Only their outer port is unusual, a `wire` port of bytes, which no other stage accepts, so the type system keeps them at the ends. Inside, every sample is native. | accepted; the stage-not-junction reading is this draft's answer |
| D9 | One latency report per stage (nominal, deviation, exactness, group delay) and one flush contract with a stated residue. | accepted |
| D10 | Capabilities are published as constants and checked by one `stage_traits<T>`; nothing detects a capability by probing for a member. | accepted |
| D11 | Read-only analysis (meters, correlation, pitch) is a sibling `analyzer` concept; `tap<Analyzer>` is the stage that puts one in a chain, passing the audio through. | accepted |
| D12 | The Tap libraries' engineering discipline carries over: contracts as numbers pinned by tests; fixed-point profiles opt-in per stage; processing `noexcept`; published literature only; honest limits stated and pinned. Three points of it are made stricter here: D21, D22 and D24. | accepted |
| D13 | A chain is written by the caller as a type. Nothing is looked up from a rate pair or built from a run-time description. | accepted |
| D14 | catenae is not part of the TapHouse family. Its namespace is `catenae::` and its banner is `Copyright 2026 Timothy Place and the catenae contributors`. | settled |
| D15 | catenae and SampleRateTap coexist for good. Neither depends on the other, and catenae's stage contract owes `tap::dsp::sync_stage` nothing. | settled |
| D16 | Once the architecture is settled and stable, catenae duplicates all of SampleRateTap's functionality (`rational`, `bridge`, `async`), re-implemented on catenae's contracts. The architecture is not called stable until it is shown that they fit. | settled |
| D17 | A port has a *signal kind*: time-domain audio, frames of transform coefficients, or wire bytes. Each kind is its own port template, so a transform-domain region (analysis, coefficient stages, synthesis) is typed in a chain and a time-domain stage after an analysis does not compile. | accepted |
| D18 | The stage contract is fixed-ratio: every stage's ratio is a compile-time L/M and `outputs_for` is exact. | accepted |
| D19 | A *junction* is a first-class concept beside the stage, with its own contract (section 6), for the place where two threads or two clocks meet. The asynchronous converter is a junction. A chain never contains one. | accepted |
| D20 | The naming and formatting rules are the Tap rules, enforced by `.clang-format` and `.clang-tidy` in CI; the files are owned here, with no drift check against TapHouse. | settled |
| D21 | No exceptions and no RTTI, ever. Every build is `-fno-exceptions -fno-rtti`; no `throw`, `try`, `dynamic_cast` or `typeid` appears; there is no second configuration. Whatever can fail does so through a returned `status` or `result<T>`, and only outside the audio path (section 3.3). | settled |
| D22 | No heap. The library never calls `new`, `malloc` or an allocating standard container, at construction or afterwards. Every buffer is a member whose size is a template argument, or storage the caller supplies as a span. | proposed (follows from D23; it replaces the Tap rule "allocate at construction") |
| D23 | catenae is written with MISRA C++:2023 and ASIL B in mind, as a precaution: there is no requirement today, one may come, and there is no checker. So the constraints that are expensive to retrofit are designed in now (section 8), and nothing is claimed: no compliance statement, no compliance matrix, no safety case. | settled |
| D24 | `float` is the default sample type and `double` is the golden model. An unsuffixed name is the `float` stage (`gain<2>`); `_f64`, `_q15` and `_q31` name the others. Every profile's numbers are stated against the `double` one, which never changes for speed. | settled (the naming is proposed) |
| D25 | A frame of transform coefficients is, per channel, N/2 + 1 bins of `complex<S>` (real then imaginary, adjacent), DC to Nyquist, all stored; channels follow one another whole (section 5.2). | proposed |
| D26 | Build systems: CMake first; waf supported and built in CI from C0; a Zephyr module when a consumer needs it. One amalgamated source file exists for anything else. | settled as to which; the timing is proposed |
| D27 | Preconditions come in two tiers (section 3.3). A *boundary* check, made once per call where the caller's data enters the library, is on in every build. An *audit* check, inside the library's own loops or on what the host is responsible for, is on in debug builds only. One generated configuration header sets the level for the whole image, and a link-time guard refuses an image that mixes levels. | proposed |
| D28 | FFT back ends (section 7.1): catenae's own engines are the default on every target and the only ones in this repository. CMSIS-DSP and Apple's vDSP are opt-in, chosen when the library is built, and never vendored: the consumer's own copy is used. | proposed |

## 2. Charter

**In:** 1:1 stages (filters, gain, delay, mixing, nonlinearities, dynamics without a side
chain), rate-changing stages by a compile-time ratio L/M, sample-format, layout and wire
converters, transform-domain regions (analysis, coefficient stages, synthesis), the chain,
analyzers and their tap, junctions, and the measurement harness that tells us which layout
each stage wants.

**In, after the architecture is stable (D16):** everything SampleRateTap does: `rational`
first (its stages are already chain stages), then `bridge` (one fixed-ratio converter), then
`async` (a junction).

**Out, at least until 1.0:**

- Stages with two audio inputs (side-chain compressors, convolution against a live signal).
- A run-time graph: no dynamic routing, no plug-in host, no patch format.
- Pitch shifters and other processes with their own framing and latency model.
- Elastic processes inside one clock domain (time-stretch, varispeed).
- Anything defined by imitating a product rather than by a published method.

## 3. Ports and the stage contract

### 3.1 Ports

A port is a type. Two stages join when the types are the same, so the joint rule is one
`std::is_same` and one `static_assert` message.

| Port | Carries | A frame is |
|---|---|---|
| `port<S, Ch, layout::interleaved>` (the default layout) | time-domain audio, `S` one of `float`, `double`, `int16_t` (Q15), `int32_t` (Q31) | `Ch` samples |
| `port<S, Ch, layout::planar>` | the same, a channel at a time | `Ch` samples, each channel's run contiguous within a call |
| `spectrum<S, Ch, Bins>` | transform coefficients (D25) | `Ch * Bins` values of `complex<S>` |
| `wire<Format, Ch>` | bytes on their way in or out (`s16le`, `s24le_packed`, `s24be_in_32`, ...) | `Ch * bytes_per_sample` bytes |

### 3.2 What a stage declares

Read through `stage_traits<T>`, which fails with one readable `static_assert` per missing or
ill-typed member.

| Member | Meaning |
|---|---|
| `input_port`, `output_port` | The two port types. |
| `k_ratio` | `ratio{up, down}`, reduced. For an analysis stage it is 1 / hop. |
| `k_in_place` | Whether `process` may read and write one buffer. |
| `k_exact_latency` | Whether the stage delays every frequency alike. |
| `process(in, out)` | Spans of the ports' value types; consumes all of `in`; returns the frames written. `noexcept`, no allocation, no failure. |
| `outputs_for(n)`, `frames_needed(k)` | Exact, from the current position. |
| `max_outputs_for(n)` | An upper bound from any state, `constexpr`. The chain sizes its scratch from this. |
| `reset()` | Back to the constructed state. |
| `latency()` | `{nominal, deviation}` in output frames; deviation 0 when exact. Plus `group_delay(f)`. |
| `tail_frames()`, `flush_residue()` | Zero input frames after which the output is silence within the residue (0 for FIR; a stated bound for a floating-point recursion). |

### 3.3 Failure, without exceptions (D21)

- **The audio path cannot fail.** `process`, `push`, `pull` and `reset` return counts, never
  errors. What they require of the caller is a precondition.
- **Design is where failure lives.** Anything computed from run-time numbers (a cutoff, a
  set of taps) is a *design*: a small value type produced by `T::make(args)`, which returns
  `result<T>` (a value or a `status`; no heap, C++17). A stage is constructed from a design
  and cannot fail. A design can equally be a `constexpr` table in ROM, in which case no
  designer is linked at all.
- **Preconditions, in two tiers (D27).**
  - A *boundary* check guards what the caller hands in: a span too short for
    `outputs_for(n)` frames, a length that is not a whole number of frames, `reset()` on a
    running junction. It is made once per call, costs a few compares per block, and is on
    in every build, because in a release build an unchecked one is a write past a buffer.
    Its cost is in the instruction-count ratchet like any other.
  - An *audit* check guards what the library itself or the host is responsible for: an
    index inside a kernel's loop, a state invariant, the host's flush-to-zero setting. It is
    on in debug builds and compiled out otherwise: the battery and its coverage are what
    prove these, and a check per sample would cost more than the filter and stop the
    compiler vectorizing.
  - A failed check calls one `[[noreturn]]` handler that the integrator supplies at link
    time (the default traps). It receives a small enumerated code, not a file name and a
    message, so checks add no strings to ROM.
  - Checks are inline functions, not macros. The level is one generated header,
    `catenae/config.h`, the same for every file of an image; each object file refers to a
    symbol named for its level and the compiled library defines only its own, so an image
    that mixes levels fails to link instead of misbehaving (the one-definition hazard the
    Tap libraries document for their per-file macros).

## 4. The chain

- `chain<Stages...>`, built by `a | b | c`.
- At each joint the output port of one stage is the input port of the next, or the chain
  does not compile.
- Composes ratio, `outputs_for`, `frames_needed`, latency, tail and residue. A chain is
  itself a stage, so chains nest.
- Scratch is a member array (D22), sized from each stage's `max_outputs_for` at a
  compile-time block length (64 frames unless the chain is told otherwise). `process`
  accepts any length and works through it a block at a time. A run of `k_in_place` stages
  shares one buffer.
- Bit-identical for any chunking of a stream, under the library's floating-point contraction
  policy.
- One call, one thread, one clock. A chain holds no junction (D19).

## 5. Layout and signal kind

### 5.1 Time-domain audio

- Interleaved, compile-time channel count, is the default for every stage with state.
- `deinterleave<S, Ch>` and `interleave<S, Ch>` are 1:1 stages between the two layouts.
- `planar_section<Stages...>` de-interleaves once per block, runs mono stages over contiguous
  channels, re-interleaves, and presents itself as an interleaved stage.
- Each stage's row in the layout benchmark is the evidence for its preferred layout.

### 5.2 Transform coefficients: the format of a spectrum (D25)

For a real input of N samples per channel, one frame of `spectrum<S, Ch, N/2 + 1>` is:

```
channel 0:  re[0] im[0]  re[1] im[1]  ...  re[N/2] im[N/2]
channel 1:  re[0] im[0]  re[1] im[1]  ...  re[N/2] im[N/2]
...
```

- **Real and imaginary parts are interleaved**, as `catenae::complex<S>`, a plain
  `{S re; S im;}`. (`std::complex` is specified only for floating types, and Q15 and Q31
  need the same shape.) This is the layout of `std::complex`, CMSIS-DSP's real FFT and most
  other back ends, so their output is the frame with no copy.
- **Every bin from DC to Nyquist is stored, zero imaginary parts included**: N + 2 scalars
  per channel. DspTap's packing saves the two zeros by keeping the Nyquist bin's real part
  at index 1; catenae spends them so that a coefficient stage is one uniform loop over bins
  with no special index.
- **Channels are not interleaved.** Each channel's spectrum is contiguous and the channels
  follow one another. An FFT transforms one channel's contiguous samples, so the analysis
  stage de-interleaves as it windows (the FFT FIR stage in the spike already did, and still
  beat a stage per channel by 4 to 16 %). A spectrum is therefore always "planar by
  channel", and has no layout parameter.
- **Sign and scale.** Forward is `sum x[n] exp(-i 2 pi k n / N)`, unnormalized: the textbook
  convention and the accelerated back ends'. DspTap's engine uses `exp(+i)`; the carried
  engine is conjugated in its post-pass. An analysis and synthesis pair states its overall
  gain (1) and its reconstruction error as numbers.
- **Fixed point.** A Q15 or Q31 spectrum needs a block exponent per channel per frame, as
  DspTap's fixed FFT returns one. Where it travels is open (section 12).
- **The alternative, split complex** (all real parts, then all imaginary: vDSP's native
  form) vectorizes magnitude and gain loops more easily. It is measured against the
  interleaved form in the layout benchmark before D25 is frozen; a vDSP back end converts
  at its boundary either way.

A transform-domain region is an analysis stage (audio in, spectrum out, ratio 1 / hop),
stages whose ports are both spectra, and a synthesis stage back to audio.

## 6. Junctions

A stage lives inside one call, on one thread, in one clock domain. A junction is where two
call sites meet: two threads, two clocks, or both. It is defined the way a stage is, by a
contract read through `junction_traits<T>`.

| Member | Meaning |
|---|---|
| `sink_port`, `source_port` | Port types, as a stage's. |
| `k_clocks` | `clocks::shared` (two threads on one clock) or `clocks::independent`. |
| `k_ratio` | The nominal rate ratio between the two sides (1:1 for a near-unity converter). |
| `tolerance_ppm()` | Independent clocks only: the offset and drift the junction absorbs. |
| `push(in)` | Called by exactly one thread. Returns the frames accepted; what happens to the rest (dropped, counted) is stated. `noexcept`, wait-free. |
| `pull(out)` | Called by exactly one other thread. Always fills `out`; the frames it could not supply are silence, and it returns how many were real. |
| `status()` | Any thread: state (filling, locked), fill, underruns, overruns, the rate estimate where there is one. |
| `latency()` | `{nominal, range}` in source frames: a junction's delay breathes with its fill, so it is never exact. |
| `k_capacity_frames` | A template argument (D22). |
| `reset()` | Only while neither side is running; a stated precondition. |

Instances: `handoff<S, Ch, Capacity>` (a single-producer, single-consumer ring: shared clock,
two threads; the trivial junction, which proves the contract as `gain` proves the stage's);
the asynchronous converter (D16); later and outside today's charter, a jitter buffer.

`feeding(chain, junction)` runs a chain and pushes what it wrote; `fed_by(junction, chain)`
pulls what a chain needs and runs it. Each checks the port where it joins. There is no `|`
across a junction and no type that holds both sides: the thread boundary stays visible in
the caller's code. Latency across a junction composes as nominal only.

## 7. Headers, the compiled core and the build

| Parameterized on | Where it lives | Examples |
|---|---|---|
| A compile-time number measured to matter (channel count, section count, ratio, block length), or a loop that must inline into a stage's per-sample loop | Header template | The stages, the chain, `feeding` / `fed_by`, the FIR dot kernels, the biquad block kernel, `span`, the traits |
| Only the sample type (a closed set of four) and sizes passed at run time | Compiled, explicitly instantiated for the four types | The FFT engines, bulk wire `pack` / `unpack`, the asynchronous converter's servo, the analysis instruments |
| Nothing (design-time arithmetic in `double`) | Compiled | The Kaiser and Nyquist designers, the Butterworth and biquad designs, the flush-window computation, group delay |

What this buys:

1. **One definition per image.** The spike hit the header-only failure twice: two files
   built with different contraction flags shared one inline kernel, and DspTap needs an ABI
   tag on every class that embeds the FFT. A compiled FFT has one back end per library build.
   It works on storage it is handed (a member array sized by the transform length, or a
   caller's span); it allocates nothing (D22).
2. **The numeric contract is the library's.** The compiled core is built with catenae's own
   contraction policy, so its bit pins hold whatever the consumer passes. Header templates
   still compile under the consumer's flags, and the contract says so.
3. **ROM.** Compiled code is one copy, can be built for size, and is dropped when unused.
   Design code in `double` need not link at all when a design is a table (section 3.3).
   These are predictions: each component that could go either way gets a size probe in both
   forms on the embedded targets, and the table above is corrected where a probe disagrees.

### 7.1 FFT back ends (D28)

| Back end | Sample types | Where | How it arrives |
|---|---|---|---|
| catenae's own floating engine (DspTap's srdif engine, ported) | `float`, `double` | everywhere; the default | source in this repository |
| catenae's own fixed-point engine (DspTap's, ported) | Q15, Q31 | everywhere; the default | source in this repository |
| CMSIS-DSP | `float` first (Helium on the Cortex-M55, Neon on Cortex-A) | Arm, opt-in | the consumer's copy: an existing CMake target or a path given at configure time. CI fetches one pinned release by hash. |
| vDSP (Accelerate) | `float` first | Apple, opt-in | the system framework; nothing to bring in |

- **Own engines are the reference.** They are the only FFT code in the repository, the only
  ones inside D21 to D23, and the ones whose output bits are pinned. An accelerated back end
  re-presents the same contract (D25's frame, the sign, the scale) within a stated tolerance
  of the `double` engine, proven by a parity battery on the leg that has the hardware.
- **Not vendored, because an embedded consumer nearly always has CMSIS-DSP already**, pinned
  by its silicon vendor's SDK. A second copy inside catenae would define the same symbols
  and either fail to link or, worse, link the wrong one. So catenae compiles its thin
  adapter against the copy it is pointed at, states the versions it is tested with, and
  under Zephyr uses Zephyr's CMSIS-DSP module.
- **One back end per library build**, selected by `CATENAE_FFT_BACKEND` (`own`, `cmsis`,
  `vdsp`). The adapter converts the back end's native packing to D25's frame (CMSIS-DSP
  packs the Nyquist bin beside DC; vDSP is split complex).
- **Honest limits.** A back end's code is not catenae's: vDSP allocates when a setup is
  created, which is a recorded exception to D22 on Apple hosts only; and each back end
  serves its own range of transform lengths, stated as contract numbers.
- **Not planned:** FFTW (GPL); a Hexagon HVX back end until a consumer needs one, where it
  would link the vendor's library from the consumer's SDK in the same way.

**Build systems (D26).** The library is always built from source in the consumer's build,
with the consumer's target flags, and never shipped prebuilt.

- **CMake**: the reference build. `add_subdirectory`, one target.
- **waf**: a maintained `wscript`, built and tested in CI from C0, so it cannot rot.
- **Zephyr**: a module file and Kconfig over the CMake build, added when a consumer needs it.
- **Anything else**: one amalgamated `catenae.cpp`, generated in CI.

## 8. MISRA C++:2023 and ASIL B (D23)

MISRA C++:2023 is written for C++17, which is the language D2 already fixed. ISO 26262 does
not certify a library on its own: a component is developed as a safety element out of
context, against assumed requirements, and the integrator's safety case takes it from
there. What catenae can do is be built so that this is possible and cheap. The posture:

**Designed in from C0, because retrofitting them is expensive:**

- No exceptions, no RTTI (D21); no heap (D22); no recursion; every loop bounded by a
  number the contract states.
- No undefined or implementation-defined behaviour relied on: fixed-width integer types,
  no signed overflow (fixed point saturates or widens by rule), spans in place of pointer
  arithmetic in every interface.
- Every contract point is a requirement with an identifier (`CAT-STAGE-007`), and the test
  that pins it cites the identifier. The house rule "a test is named for the promise it
  pins" becomes requirement-to-test traceability with no new tool.
- Worst-case behaviour is a number: the instruction-count ratchet's worst single call and
  the stack ceilings (both carried from SampleRateTap's harness) are per-stage contract
  points.
- A deviation log from the first commit. Some family habits will need a recorded deviation
  or a replacement: function-like macros for preconditions, `memcpy`-based bit casts, the
  `|` operator as a builder.

**In CI from C0:** statement and branch coverage of the compiled core and of each
instantiation the battery makes; the static analysis that is free (`clang-tidy`'s checks,
compiler warnings as errors). These approximate a MISRA checker; they are not one.

**Deferred until a requirement arrives:** a MISRA checker for the 2023 edition, the
guideline-by-guideline compliance matrix, a safety plan, a safety manual for integrators and
an assessment. The deviation log is kept from the start anyway, as a plain list of the
places where the code knowingly does what a strict reading would forbid, because it is
cheap to write as the code is written and expensive to reconstruct.

A caution on this section: the MISRA document is licensed and was not available while this
plan was drafted. No rule is quoted or numbered here, and the list of likely deviations is
from memory. It should be checked against the text when a copy is to hand.

## 9. What the code is proposed to look like

Sketches, not compiled. Their purpose is to make the contracts above concrete enough to
argue with.

### 9.1 A stateless stage: gain

```cpp
namespace catenae {

    /// out = in * g on every channel. The floating form; Q15 and Q31 go through sample_traits.
    template <typename S, std::size_t Ch>
    class basic_gain {
      public:
        using input_port  = port<S, Ch>;   // interleaved audio unless a port says otherwise
        using output_port = port<S, Ch>;
        static constexpr ratio k_ratio{1, 1};
        static constexpr bool  k_in_place      = true;
        static constexpr bool  k_exact_latency = true;

        explicit constexpr basic_gain(S g) noexcept
            : m_gain(g) {}

        std::size_t process(span<const S> in, span<S> out) noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                out[i] = in[i] * m_gain;
            }
            return in.size() / Ch;
        }

        static constexpr std::size_t    outputs_for(std::size_t n) noexcept { return n; }
        static constexpr std::size_t    frames_needed(std::size_t k) noexcept { return k; }
        static constexpr std::size_t    max_outputs_for(std::size_t n) noexcept { return n; }
        static constexpr latency_report latency() noexcept { return {0.0, 0.0}; }
        static constexpr std::size_t    tail_frames() noexcept { return 0; }
        void                            reset() noexcept {}

      private:
        S m_gain;
    };

    template <std::size_t Ch> using gain     = basic_gain<float, Ch>;  // float is the default (D24)
    template <std::size_t Ch> using gain_f64 = basic_gain<double, Ch>; // the golden model

} // namespace catenae
```

### 9.2 A stage with a design: Butterworth

The design can fail and runs in `double`; the stage cannot fail and runs in its sample type.

```cpp
// At start-up, outside the audio path:
const auto design = catenae::butterworth_design<4>::make(0.1);   // result<butterworth_design<4>>
if (!design) {
    return design.status();                                      // status::cutoff_out_of_range
}
catenae::butterworth<4, 2> lowpass(*design);                     // order 4, stereo, float; noexcept

// Or with no designer in the image: the same value type as a table in ROM.
constexpr catenae::butterworth_design<4> k_lowpass_design = { /* generated, bit-pinned */ };
catenae::butterworth<4, 2> fixed_lowpass(k_lowpass_design);

// In the audio path:
lowpass.process(in, out);
const catenae::latency_report l = lowpass.latency();             // {4.02, 2.51} frames at fc = 0.1
```

### 9.3 A chain: four-times oversampling around a tanh overdrive

```cpp
// A stateless nonlinearity: any noexcept callable, a sample at a time, through map<>.
struct soft_clip {
    float drive;
    float operator()(float x) const noexcept { return std::tanh(drive * x); }
};

using namespace catenae;

// Stereo, float. The type is the topology; nothing is looked up from a rate.
auto overdrive = up2<2>{} | up2<2>{} | map<2, soft_clip>{{4.0f}} | down2<2>{} | down2<2>{};

static_assert(stage_traits<decltype(overdrive)>::k_ratio == ratio{1, 1});

const latency_report l = overdrive.latency();   // delay the dry path by l.nominal to mix it in
overdrive.process(in, out);                     // any length; no allocation; noexcept
```

What the sketch leaves open: `up2` and `down2` take a profile (how much attenuation, how
wide a passband), and the second pair may relax theirs because they run at twice the rate.
The layout benchmark decides whether `map` wants a planar section on a wide vector unit.

### 9.4 A chain with a transform-domain region: a spectral gate

```cpp
using namespace catenae;

// Mono. 512-sample Hann frames every 128 samples: 257 bins of complex<float> per frame.
auto denoise = stft<512, 128, 1>{window::hann}
             | spectral_gate<257, 1>{k_gate_design}
             | istft<512, 128, 1>{window::hann};

//   stft's ports:          port<float, 1>             ->  spectrum<float, 1, 257>
//   spectral_gate's ports: spectrum<float, 1, 257>    ->  spectrum<float, 1, 257>
//   istft's ports:         spectrum<float, 1, 257>    ->  port<float, 1>

static_assert(stage_traits<decltype(denoise)>::k_ratio == ratio{1, 1});   // 1/128 then 128/1

// What the type system refuses:
//   stft<512, 128, 1>{window::hann} | gain<1>{0.5f}
//   error: static assertion failed: catenae: joint 0: spectrum<float, 1, 257> feeds port<float, 1>
```

### 9.5 Edges and a junction

```cpp
using namespace catenae;

// Audio callback thread: 24-bit packed little-endian stereo off the wire, into float.
auto input = unpack<wire::s24le_packed, 2>{}          // wire<s24le_packed, 2> -> port<int32_t, 2>
           | convert<std::int32_t, float, 2>{}        // port<int32_t, 2>      -> port<float, 2>
           | butterworth<4, 2>{k_lowpass_design};

handoff<float, 2, 1024> link;                         // capacity is a template argument: no heap

feeding(input, link).process(wire_bytes_in);          // runs the chain, pushes what it wrote

// Worker thread, same clock: mix to mono and meter it.
auto metering = mix<2, 1>{k_mono_mix} | tap<1, rms_meter>{};
fed_by(link, metering).process(mono_out);             // pulls what the chain needs, runs it
```

### 9.6 Where the memory is, without a heap (D22)

Three patterns cover every case.

**Storage inside the object, sized by template arguments.** The object's size is known to
the compiler, so it can be static, and its RAM is a number in the map file.

```cpp
template <typename S, std::size_t Ch, std::size_t MaxFrames>
class basic_delay {
    // ...
    std::array<S, MaxFrames * Ch> m_line{};   // was: std::vector<S> m_line(max_frames * channels)
    std::size_t                   m_write = 0;
};

static catenae::delay<2, 480> s_align;                    // 10 ms of stereo float at 48 kHz, in .bss
static_assert(sizeof(s_align) <= 3840 + 2 * sizeof(std::size_t));
```

**Constant data stays in ROM and is viewed, not copied.**

```cpp
constexpr std::array<float, 63> k_taps = { /* generated, bit-pinned */ };   // .rodata

catenae::fir<63, 2> lowpass(k_taps);   // holds a span over k_taps: no RAM copy of the taps;
                                       // its only RAM is the history, std::array<float, 62 * 2>
```

**Storage the caller supplies, where a size is only known at run time.** The compiled core
never owns memory; it is handed a workspace and says how much it needs.

```cpp
// Compiled FFT: the length is a run-time number, the workspace is the caller's.
static std::array<float, catenae::fft_workspace_size<float>(1024)> s_workspace;   // constexpr size
const auto plan = catenae::fft_plan<float>::make(1024, s_workspace);              // result<...>:
                                                                                  // status::workspace_too_small

// A header stage does this for its user: stft<512, 128, 1> has the right array as a member.
```

A chain follows the first pattern: its scratch is a member array, so `sizeof(overdrive)` is
the chain's whole memory, known at compile time, and a size probe records it.

What D22 costs, stated plainly:

- A size chosen at run time (taps read from a file) needs a maximum chosen at compile time
  or a caller's workspace.
- Objects are as large as their buffers. They belong in static storage or in a
  caller-owned arena, not on a small thread's stack.
- A template argument per size means an instantiation per size. The rule that keeps ROM
  flat: the storage is in the template, the arithmetic is in a kernel that takes spans, so
  `delay<2, 480>` and `delay<2, 960>` share their code.

## 10. Milestones

Each ends with its battery green on every CI leg and its numbers recorded.

| | Milestone | Contents |
|---|---|---|
| C0 | Skeleton | Repository layout, license, `NOTICE.md`, style files. The CMake build and the waf build, both in CI: hosted at `-std=c++17`, 20 and 23, always `-fno-exceptions -fno-rtti`; the Cortex-M and Hexagon QEMU legs. `span`, `status` / `result`, the precondition handler, `sample_traits`, the ports, `stage_traits`, `junction_traits`, the contract documents with requirement identifiers. The static library with its first compiled unit, the amalgamation, a size probe per embedded target, coverage. The deviation log opened. The two precondition tiers, the handler and the link-time level guard. The compile-fail battery: one test per architectural rule (mismatched ports at a joint in each of the ways a port can differ; a junction inside a chain), each asserting on its message. |
| C1 | The chain on trivial stages | `chain`, `gain`, `map`, integer `delay`, `interleave` / `deinterleave`, the sample converters. `handoff`, with `feeding` and `fed_by`. The instruction-count harness and the layout benchmark, carried from the spike. This proves D6, D7, D9, D10, D19, D21 and D22 on the simplest instances. |
| C2 | The edges | `unpack` / `pack` (16, 24-packed, 24-in-32, 32; either byte order), TPDF dither, the mix matrix. |
| C3 | Filters | The biquad block kernel, the biquad family and the Butterworth with their designs, a direct-form FIR, `up2` / `down2` and the L/M stages. The overdrive chain of 9.3 as a tested example. |
| C4 | Blocks | The FFT behind its compiled interface, the overlap-save FFT FIR, `planar_section`. |
| C5 | Analysis and the transform domain | `analyzer`, `tap`, meters, auto- and cross-correlation. `stft` / `istft` and a spectral gain: the gate of 9.4 as a tested example. D25 frozen after its measurement. |
| C6 | Profiles | Q15 and Q31 for the stages that take them, the accelerated back ends (vDSP, CMSIS-DSP, HVX) behind the golden contract, the C ABI and executed notebooks. |
| C7 | The synchronous converters (D16) | `rational`'s stages, named chains and profiles; `bridge`. Each is compared against SampleRateTap's output as an independent reference. |
| C8 | The asynchronous converter (D16) | `async`, as a junction. |
| C9 | Freeze | The 1.0 contract freeze, once C7 and C8 have shown the architecture holds all of it. |

Optional, after C1: a time-boxed experiment (an hour or two) modelling port, stage, junction
and chain in SysML v2's textual notation, compared against the working code and the
compile-fail battery, and kept only if it exposes something they missed. D23 makes
requirement traceability a real need, but the identifiers in the contract documents and
tests meet it without a modelling tool.

C7 and C8 do not start until the maintainer calls the architecture stable.

## 11. What is carried in, and what moved

### Carried from DspTap (ported, each with its tests)

`sample_traits.h`, `subnormals.h`, `math.h`; `fir_kernels.h`, `kaiser.h`, `nyquist.h`,
`quantize.h`, `decimate.h`; `iir_design.h` (sections, group delay), `iir_kernels.h` (the
biquad cascade, per sample and per block); `butterworth.h`; the FFT (`fft.h` and the srdif
engine) at C4; `analysis/` for the test instruments; the bench harness, the QEMU counting
plugin (with the page-boundary fix found in the spike) and the style files.

From the spike branch only, not on DspTap's `main`: `fft_fir.h` and the layout benchmark.

From SampleRateTap: `async`'s `spsc_ring.h` for `handoff` at C1; the L/M stage machine
(`rational`'s `basic_stage`) as the model for rate-changing stages at C3; its ratchet and
ceilings scripts. Its engines come later and whole (D16), with their plans' settled
decisions read first.

The port is more than C++20 to C++17. Concepts become traits and `static_assert`s and
`std::span` becomes `catenae::span`, which is mechanical. Three things are not:

- **Allocation.** Every carried class sizes `std::vector`s at construction. Each becomes a
  member array sized by a template argument, or works on a caller's span (D22).
- **Rejection.** Every carried constructor that validates its arguments throws through
  `tap::dsp::raise`. Each becomes a design with `make()` and an infallible constructor
  (D21, section 3.3).
- **Compile-time designs.** They need checking one by one; a design that cannot be
  evaluated in C++17 `constexpr` becomes a generated, committed table with a bit pin.

Fixes will still travel between the libraries (D20's reason), but for these three reasons a
carried file is a port, not a copy.

### What moved since the spike was measured

DspTap went from 0.3.0 to 0.7.0, SampleRateTap from 0.8.0 to 0.9.0 and past it:

- **The Butterworth stage is on DspTap's `main`** (0.6.0), with `set_cutoff()` redesigning it
  in place (0.7.0).
- **`biquad_cascade_block`**: the floating cascade a block at a time, the section count and
  channel count as template arguments, at 0.26 to 0.52 of the per-sample kernel's
  instructions at orders 2 to 8 on the M4F, M33, M55 and Hexagon. The fix the spike asked for.
- **A chain reports `window_frames()`** (0.5.0), so a chain that holds a chain flushes.
- **SampleRateTap 0.9.0** shipped `rational`'s recursive stages, the guard band filter and
  their C ABI.

Consequence for the evidence: the spike's Butterworth layout rows describe two kernels that
are both superseded; the FIR, FFT FIR, converter and gain rows stand. C3 measures the
Butterworth's layout again on the block kernel.

## 12. Open questions

Decided on 2026-11-24: D23 is a precaution with nothing claimed and no checker; cobra needs
nothing from the plan.

Gating C0: nothing, if D22, D27 and D28 are accepted.

Gating C1:

1. **The planar signature.** A span per channel, or one span with the channels end to end?
2. **Run-time channel counts.** In the charter at all? The spike says to serve them through
   planar stages, where a run-time count costs under 2 %. D22 means a maximum count would
   still be a template argument.
3. **Run-time parameters.** Under section 3.3 a parameter change is a new design applied to
   a running stage. Is applying one part of the stage contract (when it takes effect,
   whether it is smoothed), or each stage's own matter?

Gating later milestones:

4. **A fixed-point spectrum's exponent.** In the frame (a header value per channel) or
   beside it?
5. **Analyzer results across threads.** Through a `handoff`, or each analyzer's own rule?
6. **Fixed point.** Q15 and Q31 from each stage's first version, or as the C6 pass?
7. **The FFT.** Carry both of DspTap's engines at C4, or the floating one first and the
   fixed-point one with C6?
