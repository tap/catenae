# Sketch: configuration, state and workspace; input groups and steady chains; a junction; a mailbox

Throwaway code written to show what decisions of `PLAN.md` look like and to measure them.
The stages are toys and nothing here is a contract.

| File | What it is |
|---|---|
| `sketch.h` | The vocabulary, six toy stages, the chain, `steady<>`, `handoff`, `mailbox` and `fed_by` |
| `demo.cpp` | The decisions at work on a host |
| `must_not_compile.cpp`, `verify.sh` | What is refused at compile time, each case with its message; `verify.sh` builds and runs everything on the host |
| `embedded.cpp`, `embedded_design.cpp`, `embedded_main.cpp`, `measure.sh`, `stack_path.py` | The chains in audio callbacks on a Cortex-M33: where their memory lands, what each call costs, what each image links |
| `pace_model.py`, `pace.cpp` | The arrival numbers (`input_group`, `max_shortfall`, `max_lead`) and their composition: by brute force on a count model, then against the real stages with `steady<>` and the pull side over each chain |
| `mailbox_stress.cpp`, `handoff_stress.cpp` | Two threads through a `mailbox` and through a `handoff`, for the thread sanitizer |
| `trace.cpp`, `figure.py`, `steady.svg` | What a bursting region and its `steady<>` form do call by call, and the figure drawn from that output: `./trace \| python3 figure.py > steady.svg` |

```sh
./verify.sh         # g++ and clang++ at C++17, 20 and 23; the model; the demo and pace under the sanitizers,
                    # audit checks on; the two stress programs under the thread sanitizer; the refusals
./measure.sh        # needs arm-none-eabi-g++
```

It compiles without a warning under GCC 13 and Clang 18 at C++17, 20 and 23 with
`-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wdouble-promotion -Wold-style-cast -Werror`; the demo and `pace` are clean under GCC's
address and undefined-behaviour sanitizers and the two stress programs under its thread
sanitizer. Clang's sanitizers have not been run.

## What it covers, and what it does not

| Covered: compiled, run, and measured where a number is quoted | Decision |
|---|---|
| Configuration apart from state; the configuration resets the state; a state is plain storage in `.bss` | D34, D49 |
| A stage declares how its state comes to rest: `reset`, or `rests_at_zero = true`, exactly one | D49, D10 |
| A workspace apart from the state, one per thread, shared by every voice; both plain aggregates, with no start-up code at C++17, 20 or 23 | D50 |
| A chain's configuration held in a plain aggregate, so it is trivially copyable | D54 |
| A design with `make()` that can fail and `from_table()` that is checked at compile time | D37 |
| `input_group`, `max_shortfall`, `max_lead`, `output_quantum` and their composition; the derived `is_frame_synchronous`; the fixed-block entry; `steady<>` | D35 |
| A junction as configuration and state, its state plain words; a fill policy that discards down to the target; `serves()`; a consumer-side restart and a report | D19, D51 |
| The exact pull, at most one junction pull per request, refused at compile time behind a junction whose target is too small | D35, D52 |
| The mailbox | D44 |
| Joints proven when the chain's type is formed; `\|` flattening either side | D6, D27 |

Not in the sketch, so not yet shown by code: `stage_traits`, `junction_traits` and the named
`joint<>` messages per kind of mismatch; `latency()`, `exact_latency` and `parallel<>`;
`tail_frames()` and the resumable `flush`; `is_in_place` buffer sharing (so the strip's
workspace below is larger than the library's will be); `with_block<>` (the block is one
global constant here); `oversample<>`; `feeding`; analyzers and `tap<>`; planar and spectrum
ports; `fixed<>` formats; the inline namespace; the check levels as a build option (the audit
level here is one macro, `CATENAE_SKETCH_AUDIT`).

What to read in `sketch.h`, in order: `map` and `times` (a fixed gain is only a `map`),
`gain` (a level that ramps when it is changed, and a stage that writes its own `reset`),
`lowpass_design` and `lowpass` (a parameter that can be wrong, so it is designed apart from
the stage), `down2`, `down2_late` and `blockwise` (input groups, a stage that leads, one
that trails, one that writes in blocks), `chain`, `steady`, `handoff`, `mailbox`, then
`fed_by`.

## Measured

2026-11-25, arm-none-eabi-g++ 13.2, Cortex-M33, `-Os`. Every number below is printed by
`measure.sh`.

The overdrive chain of PLAN.md 9.4, as two voices on one thread:

| | One object per chain (the first audit's mock) | Configuration, state and workspace |
|---|---:|---:|
| Flash for the chain's data (`.data` image) | 8,180 bytes | 0 |
| The configuration (`.rodata`: the drive, and a byte for each stage with no data) | | 16 bytes |
| RAM a voice (`.bss`): its state | 8,180 bytes | 48 bytes |
| RAM a thread (`.bss`): the workspace | | 6,152 bytes |
| RAM for sixteen voices | | 6,920 bytes |
| Start-up code or stack to construct it | none, or a 12 KB frame | none, at C++17, 20 and 23 |
| `reset` of both voices, once before the first callback | | 44 bytes of code, 8 bytes of stack |
| Stack of the audio callback: its own frame | not measured | 40 bytes |
| Stack of the audio callback: static worst case down its call path | not measured | 184 bytes, plus `tanhf` |

The first column is from different mock stages, measured in the first audit and not printed
by `measure.sh`; the flash row and the shape of the RAM rows are the point. With the scratch
inside each state, as the sketch had it before D50, a voice was 6,184 bytes and sixteen were
98,944.

A stereo channel strip, `lowpass | gain`, whose cutoff and level change at run time:

| | |
|---|---:|
| Its configuration, a variable (`.data`: five coefficients, a latency, a level, a ramp length) | 32 bytes |
| Its state (`.bss`) | 36 bytes |
| Its workspace (`.bss`; the library shares one buffer along a run of in-place stages, the sketch does not) | 516 bytes |
| `reset` of the strip | 48 bytes of code, 8 bytes of stack, plus `memset` |
| Stack of its audio callback: its own frame; worst case down its call path | 40 bytes; 96 bytes |
| The same with two mailboxes collected at the top of the callback | 56 bytes; 112 bytes |
| A mailbox of a design; of a float (`.bss`) | 84 bytes; 24 bytes |
| Software floating point called from the audio path's file | none |
| Called from the designer's file, which runs off the audio path | `tan` and seven `__aeabi_d*` routines |

The linked image (`--gc-sections`, newlib-nano), with table designs only and with the
run-time designer reachable:

| | Text | Software-double and double-`libm` symbols |
|---|---:|---:|
| Table designs only | 3,080 bytes | 0 |
| With `set_cutoff()`, which calls `make()` | 11,400 bytes | 21 |

So the designer costs 8,320 bytes of flash in this image, and an image without it links no
`double` at all: that second row is what the link-stage gate of D36 checks.

A junction, `handoff<2, 1024, 128, 64>`: its configuration is empty and its state is 8,212
bytes of `.bss` (the ring and five plain words); `push` is 96 bytes of code and a 24-byte
frame, `pull` 168 and 24.

Start-up code: `measure.sh` builds `embedded.cpp` at C++17, 20 and 23 and counts its
`.init_array` and `.text.startup` sections: none at any of the three. Every static object in
that file is declared `CATENAE_SKETCH_CONSTINIT`, so one that needed a constructor would
not compile.

## What the demo and `pace` print

- **Reset.** Every chain, reset over storage filled with `0xA5` and over zeroed storage,
  writes bit-equal output. A state that was never reset is caught at the audit level
  (check 10); at the boundary level a `gain` in it fades in from wherever the storage
  happened to be. A stage whose `reset` is not `const`, or that declares both ways to rest,
  does not compile.
- **Input groups.** `down2 | up2` is 1:1 with an input group of 2: fixed 64-frame blocks
  return 64, three-frame calls return 4, 2, 4, 2, and a three-frame fixed block does not
  compile. A block stage of 128 takes 128-frame fixed blocks with no `steady<>`.
- **Leading and trailing.** `down2` writes on a pair's first frame, so `down2 | up2` never
  trails and can be a frame ahead: inside `steady<>` it costs no latency, and the FIFO has
  room for the frame. `down2_late | up2` trails by a frame and `steady<>` adds one frame of
  latency. Both reproduce one straight run exactly, including after one odd call followed
  by full blocks, which is the pattern that finds a FIFO one frame short.
- **`steady<>` over two block stages**: 96 \| 128 (latency 191), 480 \| 512 (959), 7 \| 5
  (10) and 128 \| 64 (127) are all exact, and those four latencies are the true worst
  backlogs.
- **`pace`**: thirty chains of real stages (decimators of both conventions, interpolators,
  block stages, nested chains, `steady<>` inside a chain). For each: the group, the worst
  trail and the worst lead found by counting, against what the chain publishes (never
  smaller; equal in 27 of the 30); `steady<>` over nine call patterns with no frame wrong;
  and the pull side over random requests, exact and bit-equal to a straight run, never
  above its staging. `pace_model.py` checks the same rules on 20,870 chains of a count
  model that also has L/M stages.
- **The junction.** With a target of 128 and 64-frame pushes and pulls, the first real pull
  sees exactly 128 frames with the producer 0, 1, 2 and 100 periods ahead and with the
  consumer ahead, and every later pull sees 128. A producer held up for five periods that
  then delivers all five costs one underrun, and the next real pull sees 128. With 480-frame
  pushes beside 64-frame pulls a target of 512 never underruns and 480 does.
- **The pull.** A request is at most one pull on the junction: 102 frames from a chain by 4
  pull 26 and hold 2 over; 2 more make no pull; 700 frames pull 175 at once, though the
  chain's block is 64. Behind a junction whose target is too small for the request, the
  pull side does not compile.
