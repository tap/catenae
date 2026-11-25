# Notices

catenae has no license yet (D33): it is chosen after the coding is complete, and until then
there is no license file and no SPDX line. This file records every file carried into catenae
from another project, the commit it came from, what changed, and the license it had there
(D3, D33). A carried file is a port, not a copy: each one carries catenae's banner, and this
record is where its origin lives.

## Sources

| Source | Commit | License there |
|---|---|---|
| DspTap (`tap/DspTap`) | `b26a7db24137ba4c61896a39a831509a275f5bd4` (origin/main, 2026-11-23) | MIT, below |
| SampleRateTap (`tap/SampleRateTap`) | `4496888e187dfe094e3e02537bc4a8dcb5ecb028` (origin/main, 2026-11-24) | MIT, below |

Some of DspTap's files came to it from MuTap and SampleRateTap under the same license; their
own records (DspTap's `platform/README.md` and file headers) give that chain.

## Carried from DspTap at `b26a7db`

| Here | There | What changed |
|---|---|---|
| `.clang-format` | `.clang-format` | `Standard: c++17` (D2); the Max/Min macro shapes removed; owned here with no drift check (D20) |
| `.clang-tidy` | `.clang-tidy` | No `k_` prefix required on constants (D30's script replaces it); a per-function complexity ceiling (`readability-function-cognitive-complexity`, threshold 25, macro expansions not counted); every finding an error; the header filter covers catenae's directories |
| `.pre-commit-config.yaml` | `.pre-commit-config.yaml` | The sketch excluded instead of `third_party/` |
| `STYLE.md` | `STYLE.md` | Restated for catenae's decisions: constants by access (D30), include guards (D40), the banner without a license line (D14, D33), the safety idioms of D2, D21, D22 and D27 |
| `cmake/arm-cortex-m4-mps2.cmake`, `cmake/arm-cortex-m33-mps2.cmake`, `cmake/arm-cortex-m55-mps3.cmake` | the same paths | Names (`CATENAE_M4_FPU`, `CATENAE_BARE_METAL`); `-Wno-psabi` removed (a suppression is a recorded deviation, D31, and nothing here needs it); DspTap's FFT test-size setting removed |
| `cmake/hexagon-linux-musl.cmake` | `cmake/hexagon-linux-musl.cmake` | Names; `-ffunction-sections -fdata-sections` and `--gc-sections` added (PLAN.md section 7.1) |
| `platform/cortexm_startup.c` | `platform/cortexm_startup.c` | The fault marker is `CATENAE_TESTS_FAULT` |
| `platform/mps2_an386.ld`, `platform/mps2_an505.ld`, `platform/mps3_an547.ld` | the same paths | `.rodata` in its own output section, so the size probes read `.text` and `.rodata` apart |
| `tools/qemu_insn_plugin/insn_count.c` | `tools/qemu_insn_plugin/insn_count.c` | The marker symbols and output lines renamed (`catenae_call_begin`, `CATENAE_INSN_COUNT`, `CATENAE_CALL_MAX`) |
| `tests/bare_metal_main.cpp` | `tests/bare_metal_main.cpp` | Names (`CATENAE_BARE_METAL_FILTER`, `CATENAE_TESTS_COMPLETE`) |
| `tests/CMakeLists.txt` (the registration helper) | `tests/CMakeLists.txt` | `catenae_add_gtest_executable`; GoogleTest fetched as a system dependency; the battery's own sources are catenae's |
| `bench/icount/smoke.cmake` | `bench/icount/smoke.cmake` | Runs a cross binary under its emulator; the marker is `CATENAE_ICOUNT_DONE` |
| `.github/workflows/ci.yml` (the Cortex-M legs), `.github/workflows/bench.yml` | `.github/workflows/ci.yml`, `.github/workflows/bench.yml` | Restructured for catenae's legs and keys: the cached Arm toolchain, the one-shot QEMU legs, the plugin build and the Hexagon key's pins are theirs |
| `include/catenae/sample_traits.h` | `include/tap/dsp/sample_traits.h` | In catenae's inline namespace (section 3.4); its constants named without `k_` (D30); the float profile accumulates in `float`, not `double` (D36); every signed shift through `catenae/detail/shift.h` and every narrowing through `catenae/detail/convert.h`; the detection trait `is_kernel_sample<T>` in place of the C++20 concept (D2) |
| `tests/test_sample_traits.cpp` | `tests/test_sample_traits.cpp` | The same battery over the renamed constants, at C++17; `FloatMacAccumulatesInFloat` in place of the test of a `double` accumulator (D36); each test cites its requirement |

## Carried from SampleRateTap at `4496888`

| Here | There | What changed |
|---|---|---|
| `scripts/icount.py` | `scripts/icount.py` | catenae's keys (DspTap's set and `host`), markers and binary prefix; DspTap's `--record`; one engine |
| `scripts/ceilings.py` | `scripts/ceilings.py` | The map and call-graph readers, the disassembly bound and the ceiling rule kept; the figures are C0a's (`.text`, `.rodata`, `.data`, `.bss` apart, the start-up functions and stack, each call's static and painted stack) on every Arm key; call graphs merged across the probe and the compiled core |
| `scripts/apt_install.sh` | `scripts/apt_install.sh` | The banner only |
| `scripts/fetch_hexagon_toolchain.sh` | `scripts/fetch_hexagon_toolchain.sh` | The destination is an argument; prints the root the toolchain file reads |
| `scripts/tidy.sh` | `scripts/tidy.sh` | The compile database is configured with Clang and the bench targets |
| `tools/probe/call_marker.h`, `tools/probe/stack_meter.h` | the same paths | Names and include guards (D40) |
| `.claude/hooks/session-start.sh`, `.claude/settings.json` | the same paths | No submodule step (catenae has none) |
| `.github/workflows/ci.yml` (the hosted, sanitizer and Hexagon legs), `.github/workflows/style.yml`, `.github/workflows/docs.yml` | `.github/workflows/ci.yml`, `ci-arm64.yml`, `style.yml`, `book-pages.yml` | Restructured for catenae: C++17, 20 and 23 on every hosted leg with waf beside CMake; the arm64 leg on every pull request; no drift check; no print edition (D46) |

## Fetched at build time, never kept here

- GoogleTest v1.14.0 (commit `f8d7d77c06936315286eb55f8de22cd23c188571`, BSD-3-Clause), the
  battery's framework, fetched by CMake and cloned for waf.
- waf 2.1.9 (BSD), downloaded and digest-verified by CI to build the `wscript`.
- QEMU's `qemu-plugin.h` (v8.2.2, GPL-2.0-or-later) and QEMU 8.2.2's source, digest-verified,
  only to build the counting plugin and a plugin-enabled `qemu-hexagon` for the ratchet: CI
  tools, never shipped and never linked into catenae.
- The CodeLinaro Hexagon toolchain 19.1.5 (BSD-3-Clause), digest-verified.
- mdBook v0.4.40 (MPL-2.0), digest-verified, to build the guide.

## The license of the carried files, as they had it there

DspTap:

```
MIT License

Copyright (c) 2025-2026 Timothy Place and the DspTap contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

SampleRateTap: the same text, with `Copyright (c) 2026 Timothy Place and the SampleRateTap
contributors`.
