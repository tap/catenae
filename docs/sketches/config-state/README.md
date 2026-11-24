# Sketch: configuration and state (D34), steady chains and exact pull (D35)

Throwaway code written to show what two proposed decisions of `PLAN.md` would look like and
to measure them. The stages are toys and nothing here is a contract.

| File | What it is |
|---|---|
| `sketch.h` | The vocabulary, five toy stages, the chain, `steady<>`, a `handoff` and `fed_by` |
| `demo.cpp` | Both decisions at work on a host |
| `embedded.cpp`, `measure.sh` | The overdrive chain in an audio callback on a Cortex-M33, and where its memory lands |

It compiles without a warning under GCC 13 and Clang 18 at C++17, 20 and 23 with
`-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wdouble-promotion -Wold-style-cast -Werror`.

```sh
g++ -std=c++17 -O2 -fno-exceptions -fno-rtti demo.cpp -o demo && ./demo
./measure.sh        # needs arm-none-eabi-g++
```

Measured on 2026-11-24 (arm-none-eabi-g++ 13.2, Cortex-M33, `-Os`), the overdrive chain of
PLAN.md 9.3 as a configuration and a state:

| | One object per chain (the audit's measurement) | Configuration and state |
|---|---:|---:|
| Flash for the chain's data (`.data` image) | 8,180 bytes | 0 |
| RAM (`.bss`) | 8,180 bytes | 6,184 bytes |
| Start-up code or stack to construct it | none, or a 12 KB frame | none |
| Stack of the audio callback | not measured | 64 bytes |

The two RAM figures are from different mock stages and are not comparable to the byte; the
flash figure is the point.
