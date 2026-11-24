# Sketch: configuration and state (D34), steady chains and exact pull (D35)

Throwaway code written to show what two proposed decisions of `PLAN.md` would look like and
to measure them. The stages are toys and nothing here is a contract.

| File | What it is |
|---|---|
| `sketch.h` | The vocabulary, five toy stages, the chain, `steady<>`, a `handoff` and `fed_by` |
| `demo.cpp` | Both decisions at work on a host |
| `embedded.cpp`, `embedded_design.cpp`, `measure.sh` | The chains in audio callbacks on a Cortex-M33: where their memory lands and what each file calls |
| `mailbox_stress.cpp` | Two threads through a `mailbox`: build with `-pthread -fsanitize=thread` |
| `trace.cpp`, `figure.py`, `steady.svg` | What a bursting region and its `steady<>` form do call by call, and the figure drawn from that output: `./trace \| python3 figure.py > steady.svg` |

It compiles without a warning under GCC 13 and Clang 18 at C++17, 20 and 23 with
`-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wdouble-promotion -Wold-style-cast -Werror`.

```sh
g++ -std=c++17 -O2 -fno-exceptions -fno-rtti demo.cpp -o demo && ./demo
./measure.sh        # needs arm-none-eabi-g++
```

What to read in `sketch.h`, in order: `map` and `times` (a fixed gain is only a `map`),
`gain` (a level that ramps when it is changed), `lowpass_design` and `lowpass` (a parameter
that can be wrong, so it is designed apart from the stage), `chain`, `steady` and `fed_by`,
then `mailbox` (how a change reaches the audio thread from another one).

Measured on 2026-11-24 (arm-none-eabi-g++ 13.2, Cortex-M33, `-Os`).

The overdrive chain of PLAN.md 9.3, fixed:

| | One object per chain (the audit's measurement) | Configuration and state |
|---|---:|---:|
| Flash for the chain's data (`.data` image) | 8,180 bytes | 0 |
| RAM (`.bss`) | 8,180 bytes | 6,184 bytes |
| Start-up code or stack to construct it | none, or a 12 KB frame | none |
| Stack of the audio callback | not measured | 64 bytes |

The two RAM figures are from different mock stages and are not comparable to the byte; the
flash figure is the point.

A stereo channel strip, `lowpass | gain`, whose cutoff and level change at run time:

| | |
|---|---:|
| Its configuration, a variable (`.data`: five coefficients, a latency, a level, a ramp length) | 32 bytes |
| Its state (`.bss`) | 548 bytes |
| Stack of its audio callback | 20 bytes |
| Software floating point called from the audio path's file | none |
| Called from the designer's file, which runs off the audio path | `tan` and seven `__aeabi_d*` routines |
