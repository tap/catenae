# The harness

What catenae's cost is, measured and gated (PLAN.md sections 8 and 10). Every number here
comes from a CI run named below and is reproduced by the command beside it.

## The instruction-count ratchet

Wall clock on a shared runner is noise; executed instructions under QEMU's counting plugin
(`tools/qemu_insn_plugin/`) and under callgrind are exact, so the gate can be hard.

| Key | Target | Counted by |
|---|---|---|
| `m4-softfp` | Cortex-M4, no FPU (`mps2-an386`) | `qemu-system-arm` and the plugin |
| `m4f` | Cortex-M4F, single-precision FPU (`mps2-an386`) | the same |
| `m33` | Cortex-M33, single-precision FPU (`mps2-an505`) | the same |
| `m55` | Cortex-M55, Helium (`mps3-an547`) | the same |
| `hexagon` | Hexagon v68 + HVX-128, CodeLinaro clang 19 | `qemu-hexagon` built with plugins (it counts packets) |
| `host` | x86-64, GCC 13 | callgrind, only inside `catenae_host_counted()`, glibc's dispatch pinned |

A scenario is one binary, `catenae_icount_<scenario>` (`bench/icount/`). Its count is the
whole run under QEMU, start-up included; a `callmax_` scenario's count is its worst single
call, between `tools/probe/call_marker.h`'s markers (QEMU keys only). The rules
(`.github/workflows/bench.yml`):

- The C++17 counts are gated two-sided at ±3 % against `baselines.json`: a regression fails,
  and so does an improvement, which must be re-recorded so the gate stays tight.
- The C++20 and C++23 counts must equal the C++17 counts exactly, checksums included (D2).
- A scenario with no baseline fails, and so does a baseline with no binary.
- A number is never typed in. Each run uploads what it measured (`measured-<key>`); a change
  that moves a count re-records it from that run in the same change, with the reason.

```sh
# A QEMU key (the plugin is built as the workflow builds it):
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
# The host key:
scripts/icount.py --key host --build-dir build-host
```

## The size probes and their ceilings

A probe set is two images of one workload (`bench/probe/`), built MinSizeRel as the QEMU
test legs build: `catenae_probe_<set>`, linked with a map, and `catenae_stack_<set>`, which
paints the stack below each call and reads how deep it went. Each call an application makes
is rooted at a noinline `catenae_probe::probe_<call>`. The sets:

| Set | What it is | Calls |
|---|---|---|
| `version` | `catenae::version()`, the first compiled unit | `version` |
| `strip` | The example chain, `examples/strip.h`: its memory, start-up and stack are C1's pinned numbers | `reset`, `process` |
| `strip_object` | The same strip as a desktop program writes it, one object with its delay line on the heap: the guide's "Where memory lives" measures it against `strip` | `process` |
| `c1` | Every C1 stage once, in one float path: the link-stage gate's first probe image | `reset`, `process` |
| `c2` | Every C2 stage in one path, 24-bit packed stereo in and out with `sanitize`, `mix` and `dither` in float between: the compiled wire loops (`src/wire.cpp`) and the gate's image for the family | `reset`, `process` |
| `dynamics` | The dynamics strip, `examples/dynamics_tables.h`: an expander, a compressor and a limiter 48 frames ahead, from tables in ROM; the dynamics family's image for the link-stage gate | `reset`, `process` (one callback of 64 stereo frames) |
| `fft` | A float transform of 512 both ways from the constant tables, and a plan of 512 made at run time (`include/catenae/fft.h`): the FFT family's image for the link-stage gate | `forward`, `inverse`, `make` |
| `overdrive` | PLAN.md 9.4's chain, `examples/overdrive.h`: a tanh soft clip at four times the rate between `up2` and `down2`, two voices of state and one workspace they share; the rate family's image for the link-stage gate | `reset` (both voices), `process` (one callback of 64 stereo frames) |

`scripts/ceilings.py` reads, per key:

- the image's `.text`, `.rodata`, `.data` and `.bss`, apart (`size -A`);
- the same four for the compiled core (`core.*`, its archive members) and for the probe's own
  unit (`unit.*`, where the header templates it instantiates land), from the link map;
- the start-up functions catenae's own objects add (none, by D34), and the static worst-case
  stack of every start-up function in the image, the runtime's included;
- each call's static worst-case stack down its path, from GCC's `-fcallgraph-info` with the
  runtime's routines bounded from their disassembly, and the high-water it actually painted
  (Arm keys). GCC's frame on Arm leaves out the area a prologue reserves before its register
  save, where an argument split between r3 and the stack (a `span` by value) is spilled, so the
  bound adds that area back from the disassembly: without it the overdrive's `process` was
  bounded 16 bytes below what it painted;
- each call's code (`code.<call>`): the bytes of every function the call can reach by a direct
  call in the linked image, the root and the runtime's routines included (Arm keys);
- where the stack probe prints them, a chain's state and workspace as the target lays them out
  (`sizeof.state`, `sizeof.workspace`) and what the heap holds after start-up (`heap.used`),
  which no section of the image shows (Arm keys).

`strip_object`'s start-up has no static stack bound, since `operator new` calls the
new-handler through a pointer; the script says so for that set alone and records no start-up
stack for it. Any other probe whose start-up cannot be bounded fails.

Each figure has a ceiling, one-sided: the recorded figure plus 3 %, rounded up to 64 bytes for
sizes and 8 for stack (SampleRateTap's rule); a count of start-up functions, and any figure of
0, is its own ceiling. The table below is generated from `ceilings.json`
(`scripts/ceilings.py docs`; CI checks it is current).

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
scripts/ceilings.py measure --key m33 --build-dir build-size
```

<!-- ceilings:begin (scripts/ceilings.py docs; do not edit by hand) -->
Probe `version`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 1,020 / 1,088 | 1,020 / 1,088 | 1,020 / 1,088 | 1,020 / 1,088 | 292 / 320 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 88 / 128 | 88 / 128 | 88 / 128 | 88 / 128 | 40 / 64 |
| `image.text` | 7,384 / 7,616 | 7,448 / 7,680 | 7,296 / 7,552 | 7,300 / 7,552 | 7,092 / 7,360 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 4 / 64 | 4 / 64 | 4 / 64 | 4 / 64 | 4 / 64 |
| `unit.bss` | 4 / 64 | 4 / 64 | 4 / 64 | 4 / 64 | 4 / 64 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.text` | 26 / 64 | 26 / 64 | 26 / 64 | 26 / 64 | 36 / 64 |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.version.measured` | 8 / 16 | 8 / 16 | 8 / 16 | 8 / 16 |  |
| `stack.version.static` | 8 / 16 | 8 / 16 | 8 / 16 | 8 / 16 |  |
| `code.version` | 20 / 64 | 20 / 64 | 20 / 64 | 20 / 64 |  |

Probe `strip`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 1,820 / 1,920 | 1,820 / 1,920 | 1,820 / 1,920 | 1,820 / 1,920 | 1,092 / 1,152 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 100 / 128 | 100 / 128 | 100 / 128 | 100 / 128 | 48 / 64 |
| `image.text` | 9,240 / 9,536 | 8,024 / 8,320 | 7,880 / 8,128 | 7,948 / 8,192 | 8,244 / 8,512 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.bss` | 804 / 832 | 804 / 832 | 804 / 832 | 804 / 832 | 804 / 832 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 12 / 64 | 12 / 64 | 12 / 64 | 12 / 64 | 12 / 64 |
| `unit.text` | 538 / 576 | 522 / 576 | 522 / 576 | 588 / 640 | 1,108 / 1,152 |
| `sizeof.state` | 288 / 320 | 288 / 320 | 288 / 320 | 288 / 320 |  |
| `sizeof.workspace` | 516 / 576 | 516 / 576 | 516 / 576 | 516 / 576 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 64 / 72 | 40 / 48 | 40 / 48 | 40 / 48 |  |
| `stack.process.static` | 64 / 72 | 40 / 48 | 40 / 48 | 40 / 48 |  |
| `stack.reset.measured` | 28 / 32 | 28 / 32 | 32 / 40 | 32 / 40 |  |
| `stack.reset.static` | 28 / 32 | 28 / 32 | 32 / 40 | 32 / 40 |  |
| `code.process` | 1,566 / 1,664 | 488 / 512 | 488 / 512 | 552 / 576 |  |
| `code.reset` | 218 / 256 | 218 / 256 | 218 / 256 | 218 / 256 |  |

Probe `strip_object`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 1,092 / 1,152 | 1,092 / 1,152 | 1,092 / 1,152 | 1,092 / 1,152 | 2,760 / 2,880 |
| `image.data` | 1,380 / 1,472 | 1,380 / 1,472 | 1,380 / 1,472 | 1,380 / 1,472 | 184 / 192 |
| `image.rodata` | 861 / 896 | 861 / 896 | 861 / 896 | 861 / 896 | 20,732 / 21,376 |
| `image.text` | 19,608 / 20,224 | 18,712 / 19,328 | 18,100 / 18,688 | 18,108 / 18,688 | 185,444 / 191,040 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.bss` | 32 / 64 | 32 / 64 | 32 / 64 | 32 / 64 | 32 / 64 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.text` | 320 / 384 | 292 / 320 | 292 / 320 | 294 / 320 | 380 / 448 |
| `startup.functions` | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 |
| `heap.used` | 1,360 / 1,408 | 1,360 / 1,408 | 1,360 / 1,408 | 1,360 / 1,408 |  |
| `stack.process.measured` | 52 / 56 | 16 / 24 | 16 / 24 | 16 / 24 |  |
| `stack.process.static` | 56 / 64 | 16 / 24 | 16 / 24 | 16 / 24 |  |
| `code.process` | 876 / 960 | 136 / 192 | 136 / 192 | 136 / 192 |  |

Probe `c1`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 5,576 / 5,760 | 5,576 / 5,760 | 5,576 / 5,760 | 5,576 / 5,760 | 4,844 / 4,992 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 124 / 128 | 124 / 128 | 124 / 128 | 124 / 128 | 72 / 128 |
| `image.text` | 10,584 / 10,944 | 8,984 / 9,280 | 8,884 / 9,152 | 9,044 / 9,344 | 10,484 / 10,816 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.bss` | 4,560 / 4,736 | 4,560 / 4,736 | 4,560 / 4,736 | 4,560 / 4,736 | 4,560 / 4,736 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 36 / 64 | 36 / 64 | 36 / 64 | 36 / 64 | 36 / 64 |
| `unit.text` | 1,570 / 1,664 | 1,526 / 1,600 | 1,526 / 1,600 | 1,680 / 1,792 | 3,284 / 3,392 |
| `sizeof.state` | 696 / 768 | 696 / 768 | 696 / 768 | 696 / 768 |  |
| `sizeof.workspace` | 3,864 / 4,032 | 3,864 / 4,032 | 3,864 / 4,032 | 3,864 / 4,032 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 96 / 104 | 40 / 48 | 40 / 48 | 40 / 48 |  |
| `stack.process.static` | 96 / 104 | 40 / 48 | 40 / 48 | 40 / 48 |  |
| `stack.reset.measured` | 28 / 32 | 28 / 32 | 32 / 40 | 32 / 40 |  |
| `stack.reset.static` | 28 / 32 | 28 / 32 | 32 / 40 | 32 / 40 |  |
| `code.process` | 3,168 / 3,264 | 1,400 / 1,472 | 1,400 / 1,472 | 1,552 / 1,600 |  |
| `code.reset` | 302 / 320 | 302 / 320 | 302 / 320 | 302 / 320 |  |

Probe `c2`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 3,540 / 3,648 | 3,540 / 3,648 | 3,540 / 3,648 | 3,540 / 3,648 | 2,812 / 2,944 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 128 / 192 | 128 / 192 | 128 / 192 | 128 / 192 | 80 / 128 |
| `image.text` | 9,560 / 9,856 | 8,344 / 8,640 | 8,196 / 8,448 | 8,224 / 8,512 | 9,348 / 9,664 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 112 / 128 | 112 / 128 | 112 / 128 | 118 / 128 | 136 / 192 |
| `unit.bss` | 2,524 / 2,624 | 2,524 / 2,624 | 2,524 / 2,624 | 2,524 / 2,624 | 2,524 / 2,624 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 40 / 64 | 40 / 64 | 40 / 64 | 40 / 64 | 40 / 64 |
| `unit.text` | 734 / 768 | 730 / 768 | 726 / 768 | 742 / 768 | 1,744 / 1,856 |
| `sizeof.state` | 84 / 128 | 84 / 128 | 84 / 128 | 84 / 128 |  |
| `sizeof.workspace` | 2,440 / 2,560 | 2,440 / 2,560 | 2,440 / 2,560 | 2,440 / 2,560 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 80 / 88 | 32 / 40 | 32 / 40 | 32 / 40 |  |
| `stack.process.static` | 80 / 88 | 32 / 40 | 32 / 40 | 40 / 48 |  |
| `stack.reset.measured` | 28 / 32 | 28 / 32 | 32 / 40 | 32 / 40 |  |
| `stack.reset.static` | 28 / 32 | 28 / 32 | 32 / 40 | 32 / 40 |  |
| `code.process` | 2,202 / 2,304 | 784 / 832 | 780 / 832 | 798 / 832 |  |
| `code.reset` | 258 / 320 | 258 / 320 | 258 / 320 | 262 / 320 |  |

Probe `fft`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 5,012 / 5,184 | 5,012 / 5,184 | 5,012 / 5,184 | 5,012 / 5,184 | 4,284 / 4,416 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 2,148 / 2,240 | 2,148 / 2,240 | 2,148 / 2,240 | 2,148 / 2,240 | 2,144 / 2,240 |
| `image.text` | 19,480 / 20,096 | 17,496 / 18,048 | 17,400 / 17,984 | 17,344 / 17,920 | 20,260 / 20,928 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 2,048 / 2,112 | 2,048 / 2,112 | 2,048 / 2,112 | 2,048 / 2,112 | 2,104 / 2,176 |
| `core.text` | 10,724 / 11,072 | 9,396 / 9,728 | 9,396 / 9,728 | 9,326 / 9,664 | 12,640 / 13,056 |
| `unit.bss` | 3,996 / 4,160 | 3,996 / 4,160 | 3,996 / 4,160 | 3,996 / 4,160 | 3,996 / 4,160 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 8 / 64 | 8 / 64 | 8 / 64 | 8 / 64 | 0 / 0 |
| `unit.text` | 184 / 192 | 184 / 192 | 184 / 192 | 196 / 256 | 232 / 256 |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.forward.measured` | 568 / 592 | 464 / 480 | 464 / 480 | 464 / 480 |  |
| `stack.forward.static` | 568 / 592 | 480 / 496 | 480 / 496 | 480 / 496 |  |
| `stack.inverse.measured` | 568 / 592 | 464 / 480 | 464 / 480 | 464 / 480 |  |
| `stack.inverse.static` | 568 / 592 | 480 / 496 | 480 / 496 | 480 / 496 |  |
| `stack.make.measured` | 540 / 560 | 540 / 560 | 540 / 560 | 540 / 560 |  |
| `stack.make.static` | 540 / 560 | 540 / 560 | 540 / 560 | 540 / 560 |  |
| `code.forward` | 5,634 / 5,824 | 3,874 / 4,032 | 3,874 / 4,032 | 3,898 / 4,032 |  |
| `code.inverse` | 5,636 / 5,824 | 3,890 / 4,032 | 3,890 / 4,032 | 3,914 / 4,032 |  |
| `code.make` | 3,374 / 3,520 | 2,388 / 2,496 | 2,388 / 2,496 | 2,274 / 2,368 |  |

Probe `recursive`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 1,588 / 1,664 | 1,588 / 1,664 | 1,588 / 1,664 | 1,588 / 1,664 | 860 / 896 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 156 / 192 | 156 / 192 | 156 / 192 | 156 / 192 | 104 / 128 |
| `image.text` | 9,432 / 9,728 | 8,216 / 8,512 | 8,060 / 8,320 | 8,084 / 8,384 | 8,404 / 8,704 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.bss` | 572 / 640 | 572 / 640 | 572 / 640 | 572 / 640 | 572 / 640 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 68 / 128 | 68 / 128 | 68 / 128 | 68 / 128 | 68 / 128 |
| `unit.text` | 730 / 768 | 700 / 768 | 700 / 768 | 722 / 768 | 1,264 / 1,344 |
| `sizeof.state` | 56 / 64 | 56 / 64 | 56 / 64 | 56 / 64 |  |
| `sizeof.workspace` | 516 / 576 | 516 / 576 | 516 / 576 | 516 / 576 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 180 / 192 | 128 / 136 | 128 / 136 | 128 / 136 |  |
| `stack.process.static` | 184 / 192 | 128 / 136 | 128 / 136 | 128 / 136 |  |
| `stack.reset.measured` | 20 / 24 | 20 / 24 | 24 / 32 | 24 / 32 |  |
| `stack.reset.static` | 20 / 24 | 20 / 24 | 24 / 32 | 24 / 32 |  |
| `code.process` | 2,118 / 2,240 | 674 / 704 | 674 / 704 | 694 / 768 |  |
| `code.reset` | 210 / 256 | 210 / 256 | 210 / 256 | 210 / 256 |  |

Probe `fir`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 2,568 / 2,688 | 2,568 / 2,688 | 2,568 / 2,688 | 2,568 / 2,688 | 1,836 / 1,920 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 348 / 384 | 348 / 384 | 348 / 384 | 348 / 384 | 296 / 320 |
| `image.text` | 9,368 / 9,664 | 8,088 / 8,384 | 7,992 / 8,256 | 8,012 / 8,256 | 8,372 / 8,640 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.bss` | 1,552 / 1,600 | 1,552 / 1,600 | 1,552 / 1,600 | 1,552 / 1,600 | 1,552 / 1,600 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 260 / 320 | 260 / 320 | 260 / 320 | 260 / 320 | 260 / 320 |
| `unit.text` | 700 / 768 | 630 / 704 | 634 / 704 | 652 / 704 | 1,236 / 1,280 |
| `sizeof.state` | 1,036 / 1,088 | 1,036 / 1,088 | 1,036 / 1,088 | 1,036 / 1,088 |  |
| `sizeof.workspace` | 516 / 576 | 516 / 576 | 516 / 576 | 516 / 576 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 104 / 112 | 64 / 72 | 64 / 72 | 64 / 72 |  |
| `stack.process.static` | 104 / 112 | 64 / 72 | 64 / 72 | 64 / 72 |  |
| `stack.reset.measured` | 20 / 24 | 20 / 24 | 24 / 32 | 24 / 32 |  |
| `stack.reset.static` | 20 / 24 | 20 / 24 | 24 / 32 | 24 / 32 |  |
| `code.process` | 2,076 / 2,176 | 592 / 640 | 596 / 640 | 612 / 640 |  |
| `code.reset` | 222 / 256 | 222 / 256 | 222 / 256 | 222 / 256 |  |

Probe `overdrive`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 13,408 / 13,824 | 13,408 / 13,824 | 13,408 / 13,824 | 13,408 / 13,824 | 12,676 / 13,120 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 676 / 704 | 676 / 704 | 676 / 704 | 676 / 704 | 616 / 640 |
| `image.text` | 12,760 / 13,184 | 11,096 / 11,456 | 10,864 / 11,200 | 10,916 / 11,264 | 14,500 / 14,976 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.bss` | 12,392 / 12,800 | 12,392 / 12,800 | 12,392 / 12,800 | 12,392 / 12,800 | 12,392 / 12,800 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 576 / 640 | 576 / 640 | 576 / 640 | 576 / 640 | 576 / 640 |
| `unit.text` | 2,104 / 2,176 | 2,090 / 2,176 | 2,086 / 2,176 | 2,124 / 2,240 | 4,412 / 4,608 |
| `sizeof.state` | 3,888 / 4,032 | 3,888 / 4,032 | 3,888 / 4,032 | 3,888 / 4,032 |  |
| `sizeof.workspace` | 4,616 / 4,800 | 4,616 / 4,800 | 4,616 / 4,800 | 4,616 / 4,800 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 368 / 384 | 352 / 368 | 352 / 368 | 356 / 368 |  |
| `stack.process.static` | 376 / 392 | 352 / 368 | 352 / 368 | 356 / 368 |  |
| `stack.reset.measured` | 52 / 56 | 52 / 56 | 56 / 64 | 56 / 64 |  |
| `stack.reset.static` | 52 / 56 | 52 / 56 | 56 / 64 | 56 / 64 |  |
| `code.process` | 5,220 / 5,440 | 3,492 / 3,648 | 3,416 / 3,520 | 3,452 / 3,584 |  |
| `code.reset` | 366 / 384 | 366 / 384 | 366 / 384 | 366 / 384 |  |

Probe `designers`: each cell is the recorded figure / its ceiling, in bytes (start-up functions: a count).

| figure | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` |
|---|--:|--:|--:|--:|--:|
| `image.bss` | 9,204 / 9,536 | 9,204 / 9,536 | 9,204 / 9,536 | 9,204 / 9,536 | 8,476 / 8,768 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 648 / 704 | 648 / 704 | 648 / 704 | 9,148 / 9,472 | 11,344 / 11,712 |
| `image.text` | 38,744 / 39,936 | 39,000 / 40,192 | 38,852 / 40,064 | 26,308 / 27,136 | 51,588 / 53,184 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 36 / 64 |
| `core.text` | 5,146 / 5,312 | 5,508 / 5,696 | 5,500 / 5,696 | 3,568 / 3,712 | 7,468 / 7,744 |
| `unit.bss` | 8,188 / 8,448 | 8,188 / 8,448 | 8,188 / 8,448 | 8,188 / 8,448 | 8,188 / 8,448 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 64 / 128 | 64 / 128 | 64 / 128 | 64 / 128 | 0 / 0 |
| `unit.text` | 8,066 / 8,320 | 7,990 / 8,256 | 7,990 / 8,256 | 6,936 / 7,168 | 10,408 / 10,752 |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.butterworth_16.measured` | 22,000 / 22,664 | 22,000 / 22,664 | 22,000 / 22,664 | 21,800 / 22,456 |  |
| `stack.butterworth_16.static` | 22,008 / 22,672 | 22,008 / 22,672 | 22,008 / 22,672 | 21,824 / 22,480 |  |
| `stack.butterworth_2.measured` | 1,000 / 1,032 | 992 / 1,024 | 992 / 1,024 | 800 / 824 |  |
| `stack.butterworth_2.static` | 1,292 / 1,336 | 1,300 / 1,344 | 1,300 / 1,344 | 1,200 / 1,240 |  |
| `stack.butterworth_4.measured` | 1,992 / 2,056 | 2,000 / 2,064 | 2,000 / 2,064 | 1,800 / 1,856 |  |
| `stack.butterworth_4.static` | 2,000 / 2,064 | 2,008 / 2,072 | 2,008 / 2,072 | 1,824 / 1,880 |  |
| `stack.butterworth_8.measured` | 6,112 / 6,296 | 6,112 / 6,296 | 6,112 / 6,296 | 5,904 / 6,088 |  |
| `stack.butterworth_8.static` | 6,120 / 6,304 | 6,120 / 6,304 | 6,120 / 6,304 | 5,928 / 6,112 |  |
| `stack.cookbook.measured` | 1,056 / 1,088 | 1,056 / 1,088 | 1,056 / 1,088 | 864 / 896 |  |
| `stack.cookbook.static` | 1,364 / 1,408 | 1,380 / 1,424 | 1,380 / 1,424 | 1,288 / 1,328 |  |
| `stack.fir_1023.measured` | 392 / 408 | 392 / 408 | 392 / 408 | 288 / 304 |  |
| `stack.fir_1023.static` | 1,084 / 1,120 | 1,076 / 1,112 | 1,076 / 1,112 | 984 / 1,016 |  |
| `stack.fir_63.measured` | 1,400 / 1,448 | 1,400 / 1,448 | 1,400 / 1,448 | 1,296 / 1,336 |  |
| `stack.fir_63.static` | 2,092 / 2,160 | 2,084 / 2,152 | 2,084 / 2,152 | 1,992 / 2,056 |  |
| `stack.fir_q15_255.measured` | 3,024 / 3,120 | 3,024 / 3,120 | 3,024 / 3,120 | 2,904 / 2,992 |  |
| `stack.fir_q15_255.static` | 3,716 / 3,832 | 3,708 / 3,824 | 3,708 / 3,824 | 3,600 / 3,712 |  |
| `code.butterworth_16` | 17,612 / 18,176 | 17,322 / 17,856 | 17,322 / 17,856 | 9,416 / 9,728 |  |
| `code.butterworth_2` | 17,386 / 17,920 | 17,088 / 17,664 | 17,160 / 17,728 | 9,072 / 9,408 |  |
| `code.butterworth_4` | 17,440 / 17,984 | 17,122 / 17,664 | 17,122 / 17,664 | 9,224 / 9,536 |  |
| `code.butterworth_8` | 17,576 / 18,112 | 17,248 / 17,792 | 17,248 / 17,792 | 9,338 / 9,664 |  |
| `code.cookbook` | 22,144 / 22,848 | 22,134 / 22,848 | 22,154 / 22,848 | 11,398 / 11,776 |  |
| `code.fir_1023` | 13,064 / 13,504 | 13,588 / 14,016 | 13,552 / 14,016 | 6,186 / 6,400 |  |
| `code.fir_63` | 13,606 / 14,016 | 14,130 / 14,592 | 14,022 / 14,464 | 6,648 / 6,848 |  |
| `code.fir_q15_255` | 14,352 / 14,784 | 14,892 / 15,360 | 14,784 / 15,232 | 7,354 / 7,616 |  |
<!-- ceilings:end -->

## The layout benchmark (informational, never a gate)

`bench/layout/` (carried from DspTap's) counts C1's stages in both channel layouts on every
ratchet key, so the layout a stage prefers (PLAN.md section 5.1: interleaved is the default
for every stage with state) has a number per core. It is informational by decision: no
baselines, no tolerance, no failure.

- **Binaries:** `catenae_layout_<kind>_f32_ch<C>_<i|p>`, kind `gain` (0.5, ramping over 96
  frames, set to 0.75 once after reset), `delay` (32 frames), `map` (a halving) or `strip`
  (the three as one chain); C = 1, 2, 6, 12; `i` = one configuration of C channels over
  interleaved frames, `p` = C mono configurations, each with its own state, each over its own
  contiguous channel buffer. One channel builds `_i` only. 16,384 frames per channel in calls
  of 128, float.
- **What a count is:** the sum over the 128 steps (one call interleaved, C calls planar), each
  inside `catenae_host_counted()` between the call markers: under QEMU the plugin's sum over
  the brackets, on the host callgrind's count inside that function. The input, the checksum
  and start-up are outside it.
- **Agreement:** both layouts of a pair print one checksum when their outputs are
  bit-identical; the job prints `bit-identical` or `DIFFERENT` per pair, and on the host CTest
  pins it (`bench.layout.<pair>_layouts_agree`).
- **The job:** `bench.yml`'s `Layout counts (informational, never a gate)` step, after the
  ratchet's count, `continue-on-error`: `icount.py --prefix catenae_layout_ --report-only`
  prints every count and the table and uploads `layout-<key>.json` in `measured-<key>`.

Planar over interleaved (below 1, the mono configurations are cheaper), from Bench run
[45715344934](https://github.com/tap/catenae/actions/runs/45715344934) at `0de8a2b`,
2026-11-25, its `layout-<key>.json` artifacts; every pair bit-identical on every key; `hexagon`
counts packets:

| key | `gain` 2 | `gain` 6 | `gain` 12 | `delay` 2 | `delay` 6 | `delay` 12 | `map` 2 | `map` 6 | `map` 12 | `strip` 2 | `strip` 6 | `strip` 12 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `m4-softfp` | 1.020 | 1.036 | 1.040 | 1.187 | 1.559 | 1.766 | 1.004 | 1.006 | 1.006 | 1.004 | 1.070 | 1.082 |
| `m4f` | 1.103 | 1.189 | 1.212 | 1.306 | 1.486 | 1.653 | 1.018 | 1.026 | 1.028 | 1.023 | 1.282 | 1.344 |
| `m33` | 1.100 | 1.185 | 1.208 | 1.233 | 1.486 | 1.653 | 1.018 | 1.026 | 1.028 | 0.997 | 1.281 | 1.344 |
| `m55` | 1.129 | 1.400 | 1.520 | 1.145 | 2.204 | 4.988 | 1.085 | 1.132 | 1.144 | 1.182 | 1.909 | 2.971 |
| `hexagon` | 1.093 | 1.185 | 1.219 | 1.534 | 1.494 | 1.506 | 0.981 | 0.915 | 0.980 | 1.331 | 1.428 | 1.425 |
| host (x86-64) | 1.316 | 1.626 | 1.749 | 1.827 | 3.490 | 5.792 | 1.076 | 1.110 | 1.121 | 1.614 | 2.553 | 3.215 |

From 6 channels up, interleaved is the cheaper layout for every stage with state (the gain,
the delay and the strip) on every key, by 1.04 to 5.8 times. Stereo is where it is close: the
gain is 2 to 32 % cheaper interleaved on every key, and the mono strip is within 3 % of the
interleaved one on the M4, M4F and M33. The stateless map is within 3 % on the M4, M4F and
M33, up to 14 % in interleaved's favour on the M55 and 12 % on the host, and 2 to 9 % in the
mono maps' favour on Hexagon. The host's ranking is not a Cortex-M's: it puts the 12-channel
delay's mono form at 5.8 times, the M33 at 1.65.

The gain and the strip columns were measured again when `gain`'s steady level moved into a
local and its loop into runs of 16 samples. Before that, in run
[45704804502](https://github.com/tap/catenae/actions/runs/45704804502), the mono gain was 5 %
the cheaper for stereo on the M4F and M33 (0.948, 0.947) and from 1.44 to 1.95 times the
dearer at 6 and 12 channels on every key but the soft-float M4. The delay and the map columns
did not move.

The recursive stage, `butterworth` of order 4 at 0.1 from its table (kind `biquad`), planar over
interleaved from Bench run [45837885047](https://github.com/tap/catenae/actions/runs/45837885047)
at `79b6ad7`, 2026-11-26, every pair bit-identical on every key:

| key | `biquad` 2 | `biquad` 6 | `biquad` 12 |
|---|---:|---:|---:|
| `m4-softfp` | 1.002 | 1.003 | 1.003 |
| `m4f` | 1.017 | 1.019 | 1.020 |
| `m33` | 1.017 | 1.018 | 1.019 |
| `m55` | 1.006 | 1.008 | 1.008 |
| `hexagon` | 1.285 | 1.286 | 1.284 |
| host (x86-64) | 1.027 | 0.998 | 0.999 |

Unlike the stages above, the recursion costs nearly the same in either layout on the Cortex-M
and the host (within 3 %): its block kernel already runs one channel's history at a time in
registers, interleaved or not. Hexagon pays 28 % more for the mono configurations.

The direct-form FIR, `fir` of 63 taps, the low-pass at 0.1 from its table (kind `fir`), planar
over interleaved from Bench run [45866666441](https://github.com/tap/catenae/actions/runs/45866666441)
at `7e711bf`, 2026-11-26, every pair bit-identical on every key:

| key | `fir` 2 | `fir` 6 | `fir` 12 |
|---|---:|---:|---:|
| `m4-softfp` | 0.996 | 1.000 | 1.002 |
| `m4f` | 0.996 | 0.990 | 0.990 |
| `m33` | 0.993 | 0.986 | 0.986 |
| `m55` | 1.000 | 0.989 | 0.990 |
| `hexagon` | 0.989 | 0.791 | 0.800 |
| host (x86-64) | 0.990 | 0.981 | 0.994 |

The FIR runs each channel's history through the whole block before the next channel's, so it
too costs the same in either layout on the Cortex-M and the host (within 2 %); on Hexagon the
mono configurations are about a fifth cheaper at 6 and 12 channels.

The overdrive, `examples/overdrive.h` at C channels (kind `overdrive`), planar over interleaved
from Bench run [45922339828](https://github.com/tap/catenae/actions/runs/45922339828) at
`3d5df56`, 2026-11-27, every pair bit-identical on every key:

| key | `overdrive` 2 | `overdrive` 6 | `overdrive` 12 |
|---|---:|---:|---:|
| `m4-softfp` | 1.007 | 1.017 | 1.019 |
| `m4f` | 1.010 | 1.171 | 1.200 |
| `m33` | 1.002 | 1.157 | 1.205 |
| `m55` | 1.004 | 1.130 | 1.152 |
| `hexagon` | 1.102 | 1.069 | 1.074 |
| host (x86-64) | 0.991 | 1.185 | 1.227 |

For stereo the two layouts cost within 1 % of each other on the Cortex-M and the host, and the
mono configurations 10 % more on Hexagon. From 6 channels up interleaved is the cheaper on every
key: by 13 to 23 % on the M4F, M33, M55 and the host, 7 % on Hexagon and 2 % on the soft-float
M4.

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so --prefix catenae_layout_ --report-only
```

## The link-stage gate

`scripts/link_gate.py` refuses an image that links a software-double or double-`libm` symbol
(D36). It runs on named probe images, one per `float` stage family; the first is C1's,
`catenae_probe_c1`, every C1 stage in one float path (`link_gate.C1ImageHasNoSoftwareDouble`);
then C2's edges (`catenae_probe_c2`, `link_gate.C2ImageHasNoSoftwareDouble`) and the recursive
family, a biquad cascade whose design is a table in ROM with a gain after it
(`catenae_probe_recursive`, `link_gate.RecursiveImageHasNoSoftwareDouble`), and the FIR
family, a 63-tap low-pass from its table with a gain after it (`catenae_probe_fir`,
`link_gate.FirImageHasNoSoftwareDouble`), and the rate family, the overdrive at four times the
rate, whose half-bands are generated constants and whose soft clip is `tanhf`
(`catenae_probe_overdrive`, `link_gate.OverdriveImageHasNoSoftwareDouble`), and the dynamics
family, whose per-frame `log` and `exp` are float's `logf` and `expf`
(`catenae_probe_dynamics`, `link_gate.DynamicsImageHasNoSoftwareDouble`). The `designers`
probe links `make()` and so its software double on purpose, and the gate does not read it.
It also proves itself on two fixtures on every Arm key (`bench/link_gate/`, CTest label
`link_gate`). On the M55, GCC's default FPU has double precision, so a double product is an
instruction and no symbol: the gate cannot see it there, and a test pins that limit.

Its first run refused the `c1` image on the M4, M4F and M33: `convert`'s rounding truncated
the scaled value to `int64_t`, which on the Cortex-M is libgcc's `__aeabi_f2lz`, written in
software double (19 routines came with it on each of the three; the M55 passed, for the reason
above). The value is below 2^31 in magnitude, so `convert` now truncates to `int32_t`, one FPU
instruction each way, with the same results.

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
ctest --test-dir build-size -L link_gate
```

## Recorded

| What | From |
|---|---|
| `baselines.json`, every key; `ceilings.json`, every key but `host` | Bench run [45614209215](https://github.com/tap/catenae/actions/runs/45614209215) at `1cd47fa` (the run names `c4d0c87`, the same tree before the C0a pull request was rebased onto `main`), 2026-11-25: its `measured-<key>` artifacts, copied unchanged, the ceilings computed by `scripts/ceilings.py`'s rule |
| `baselines.json`: `strip_chain` and `strip_graph`, every key | Bench run [45706225434](https://github.com/tap/catenae/actions/runs/45706225434) at `f344455`, 2026-11-25: the counts its six jobs printed, copied unchanged; every other count in that run equal to its baseline. Re-recorded because `gain` now applies its steady level from a local in runs of 16 samples, and the graph's gain node was given the same loops: both strips moved on every key, beyond tolerance on four (`m33`, `m4f`, `hexagon`, and `strip_graph` on `host`). They were first recorded from run [45653902512](https://github.com/tap/catenae/actions/runs/45653902512) at `66debaa` |
| `ceilings.json`: the probe sets `strip`, `strip_object` and `c1`, and `code.version`, every key but `host`; the layout table above | Bench run [45704804502](https://github.com/tap/catenae/actions/runs/45704804502) at `fed09b6`, 2026-11-25 (`workflow_dispatch` on the C1 branch): each figure as its "Sizes, start-up and stack" step printed it, the ceilings computed by `scripts/ceilings.py`'s rule; every figure recorded before it measured equal, every ratchet count equal to its baseline, the link-stage gate's three tests passing on each Arm key; the same toolchain packages as the first run |
| `ceilings.json`: 50 figures of the probe sets `strip` and `c1`, every key but `host`; the layout table above | Bench run [45715344934](https://github.com/tap/catenae/actions/runs/45715344934) at `0de8a2b`, 2026-11-25: its `ceilings-measured-<key>.json` and `layout-<key>.json` artifacts, each figure that differed from its record copied and its ceiling computed by `scripts/ceilings.py`'s rule. Re-recorded because `gain`'s new loops cost code: the strip's `process` reaches 168 more bytes on the M4F and M33 (320 to 488), 208 on the M55 and 196 on the soft-float M4, and its stack is 8 bytes deeper (24 on the soft-float M4), for the counts in the row above. `c1`'s `reset` figures fell in the same run and are recorded as measured. Every ratchet count in that run equal to its baseline |
| `baselines.json`: `edges`, every key; `ceilings.json`: the probe set `c2`, every key but `host` | Bench run [45754937497](https://github.com/tap/catenae/actions/runs/45754937497) at `a1ac08b`, 2026-11-26 (`workflow_dispatch` on the C2 branch `claude/catenae-c2-pack`): its `measured-<key>.json` and `ceilings-measured-<key>.json` artifacts, the counts copied unchanged and the ceilings computed by `scripts/ceilings.py`'s rule. Every other count and figure in that run equal to its record, C++20 and C++23 equal to C++17 on every key, and the link-stage gate's four tests passing on each Arm key, `C2ImageHasNoSoftwareDouble` among them |
| `baselines.json`: `edges`, every key; `ceilings.json`: the probe set `c2`, every key but `host` | Bench run [45759547555](https://github.com/tap/catenae/actions/runs/45759547555) at `316cbfd`, 2026-11-26 (`workflow_dispatch` on `claude/catenae-c2-edges`): its artifacts, copied and computed as in the row above. Re-recorded because C2's second part puts `sanitize`, `mix` and `dither` in the path where the gain was: `edges` costs 41 % more on the M33 (9,195,445 to 12,963,516) and the checksum is now `e4697eabbeb92f5c`. Every other count and figure in that run equal to its record, C++20 and C++23 equal to C++17 on every key, and the link-stage gate passing on each Arm key |
| `baselines.json`: `recursive`, every key, and the five `callmax_design_` workloads, every key but `host`; `ceilings.json`: the probe sets `recursive` and `designers`, every key but `host`; the biquad layout table below | Bench run [45837885047](https://github.com/tap/catenae/actions/runs/45837885047) at `79b6ad7`, 2026-11-26 (`workflow_dispatch` on `claude/catenae-c3-recursive`): its `measured-<key>.json`, `ceilings-measured-<key>.json` and `layout-<key>.json` artifacts, the counts copied and the ceilings computed by `scripts/ceilings.py`'s rule. The recursive filters' first figures. Every other count and figure in that run equal to its record, C++20 and C++23 counted equal to C++17 on every key, and the link-stage gate passing on each Arm key, `link_gate.RecursiveImageHasNoSoftwareDouble` among its tests |
| `baselines.json`: `fir_f32`, `fir_f64`, `fir_q15` and `fir_q31`, every key, and the three `callmax_design_fir_` workloads, every key but `host`; `ceilings.json`: the probe set `fir`, and the `designers` probe's figures, every key but `host`; the FIR layout table above | Bench run [45866666441](https://github.com/tap/catenae/actions/runs/45866666441) at `7e711bf`, 2026-11-26 (`workflow_dispatch` on `claude/catenae-c3-fir`): its `measured-<key>.json`, `ceilings-measured-<key>.json` and `layout-<key>.json` artifacts, the counts copied and the ceilings computed by `scripts/ceilings.py`'s rule. The FIR's first figures; the `designers` probe gained its three FIR calls, so its image, core and unit figures rose (on the M33 `core.text` from 3,968 to 5,500 B, and `unit.bss` from 0 to 8,188 B, the 1,023-tap table and its workspace) and are recorded as measured. Every other count and figure in that run equal to its record, C++20 and C++23 counted equal to C++17 on every key, and the link-stage gate passing on each Arm key, `link_gate.FirImageHasNoSoftwareDouble` among its tests |

| `baselines.json`: `fft_f32_512`, `fft_f32_2048`, `fft_f64_512` and `callmax_fft_f32_512`, every key | Bench run [45834933446](https://github.com/tap/catenae/actions/runs/45834933446) at `a23f18f`, 2026-11-26 (`workflow_dispatch` on the C4 FFT branch, the merge of `main` at `10b6fd9`): its `measured-<key>` artifacts, copied unchanged; each FFT workload's checksum the same on all six keys, C++20 and C++23 equal to C++17, and every other count in that run equal to its baseline (C2's `edges` among them). The counts recorded first, from run [45762911188](https://github.com/tap/catenae/actions/runs/45762911188) at `1525e89`, came before the kernel's recursion became a loop (`e7bf0d2`) and before the run-time plans to 2^20 and the static-extent frames (`4989d0e`); this run counted them from 1.33 % below to 2.88 % above those (the M55's `fft_f64_512`), inside the gate, and replaces them |
| `baselines.json`: `overdrive`, `up2_f32`, `down2_f32`, `up2_q15`, `down2_q15`, `up2_q31` and `down2_q31`, every key; `ceilings.json`: the probe set `overdrive`, every key but `host`; the overdrive layout table above | Bench run [45922339828](https://github.com/tap/catenae/actions/runs/45922339828) at `3d5df56`, 2026-11-27 (`workflow_dispatch` on `claude/catenae-c3-rate`): its `measured-<key>.json`, `ceilings-measured-<key>.json` and `layout-<key>.json` artifacts, the counts copied and the ceilings computed by `scripts/ceilings.py`'s rule. The rate family's first figures, measured with the stack bound's pretend-argument area counted (the overdrive's `process` 352 bytes static and painted on the M33). Every other count and figure in that run equal to its record, C++20 and C++23 counted equal to C++17 on every key, and the link-stage gate passing on each Arm key, `link_gate.OverdriveImageHasNoSoftwareDouble` among its tests |
| `ceilings.json`: the probe set `fft`, every key but `host` | Bench run [45821938097](https://github.com/tap/catenae/actions/runs/45821938097) at `4989d0e`, 2026-11-26: its `ceilings-measured-<key>.json` artifacts, the ceilings computed by `scripts/ceilings.py`'s rule. Plans made at run time to 2^20 (Q1) widened the kernel's 29 pending entries from 16 to 32 bits, so `forward` and `inverse` reach 56 B more stack on each Arm key (480 B static on the M33, M4F and M55, 568 B on the soft-float M4), and the code figures moved by under 70 B, inside their ceilings, recorded too; every other figure in that run equal to its record (C2's `c2` probe among them), and the link-stage gate's tests passing on each Arm key. The run before, [45771572341](https://github.com/tap/catenae/actions/runs/45771572341) at `e7bf0d2`, recorded the 16-bit list. Run [45834933446](https://github.com/tap/catenae/actions/runs/45834933446) at `a23f18f`, after the merge of `main`, measured every figure of every probe set equal to its record, `c2` among them: the two `scripts/ceilings.py` fixes leave C2's bounds where they were recorded |

That run's toolchains, from its "Toolchain versions" steps: `arm-none-eabi-gcc` 13.2.1
(Ubuntu's `gcc-arm-none-eabi` 15:13.2.rel1-2) and `qemu-system-arm` 8.2.2 for the Arm keys;
CodeLinaro clang 19.1.5 and `qemu-hexagon` 8.2.2, built with plugins, for `hexagon`; GCC 13.3.0
and valgrind's callgrind for `host`. Every key counted C++20 and C++23 equal to C++17, every
workload's checksum is `94eb5aa73e186ba5`, and the counts equal those measured beforehand in a
development container with the same packages.

| Key | `version` (whole run) | `callmax_version` (worst call) |
|---|--:|--:|
| `m4-softfp` | 31,871 | 9 |
| `m4f` | 31,887 | 9 |
| `m33` | 31,916 | 9 |
| `m55` | 31,921 | 9 |
| `hexagon` | 20,886 packets | 5 |
| `host` | 21,013 (inside `catenae_host_counted()`) | (QEMU keys only) |

The whole-run counts are almost all start-up and the semihosted `printf`: the thousand calls
to `catenae::version()` cost 9 instructions each, a call and a return. They are the harness
proving itself on the first compiled unit, not a cost of the library.

### The strip, as a chain and as a graph

`strip_chain` runs the examples' strip (`examples/strip.h`: a ramping gain, a 32-frame delay
and a halving map, stereo) as a chain over 1000 blocks of 64 frames. `strip_graph` runs the
same arithmetic as three virtual nodes joined at run time (`examples/run_time_graph.h`), the
shape a framework without compile-time joints takes. Both feed the same pseudo-random input and
fold the same two samples of each block into a checksum, and on every key both print
`9edaac15a16f6c7f`: the same output, bit for bit, which `bench/icount/agree.cmake` also pins on
the host. Every key counted C++20 and C++23 equal to C++17. The compilers are those of the
first run above (GCC 13.2.1 for the Arm keys and 13.3.0 for `host`, as the run's configure
steps print them; CodeLinaro clang 19.1.5, pinned by its SHA256, for `hexagon`).

| Key | `strip_chain` | `strip_graph` | chain less graph |
|---|--:|--:|--:|
| `m4-softfp` | 23,888,126 | 23,446,864 | +441,262 (+1.9 %) |
| `m4f` | 3,592,666 | 3,453,400 | +139,266 (+4.0 %) |
| `m33` | 3,656,695 | 3,453,430 | +203,265 (+5.9 %) |
| `m55` | 2,339,702 | 2,440,434 | -100,732 (-4.1 %) |
| `hexagon` | 1,030,150 packets | 1,135,888 packets | -105,738 (-9.3 %) |
| `host` | 2,711,175 | 3,051,166 | -339,991 (-11.1 %) |

A QEMU key's count is the whole run, start-up, input, checksum and `printf` included; the
`host` count is the loop alone (`catenae_host_counted()`). Those parts are the same in both
binaries, so the difference is the strip's alone. On the Cortex-M cores the two cost about
the same, within 6 % either way; on Hexagon and the host the chain is the cheaper. What the
graph gives up is not speed: a node whose channel count differs from the one before it is
found when it is connected, as the program runs (`graph::connect` returns false), where the
chain refuses it when its type is formed, before the program exists.

### The edges

`edges` runs C2's path, the `c2` probe's (`bench/probe/c2_probe.cpp`), over 1000 blocks of 64
frames: 24-bit packed stereo unpacked to `q31` and converted to `float`; sanitized, mixed
through a crossfeed matrix and dithered at 24 bits; and converted back and packed. It folds the first and last bytes of each block into a checksum,
`e4697eabbeb92f5c` on every key. The compiled loops it calls are two of `src/wire.cpp`'s twenty
(`core.text` in the `c2` table above). Every key counted C++20 and C++23 equal to C++17.

| Key | `edges` |
|---|--:|
| `m4-softfp` | 83,590,529 |
| `m4f` | 13,090,510 |
| `m33` | 12,963,516 |
| `m55` | 11,819,295 |
| `hexagon` | 6,227,294 packets |
| `host` | 13,072,650 (inside `catenae_host_counted()`) |

### The recursive filters

`recursive` runs the `recursive` probe's path (`bench/probe/recursive_probe.cpp`) over 1000
blocks of 64 stereo frames: the order-4 Butterworth at 0.1 from its table
(`examples/lowpass_table.h`) and a gain. It folds the first and last samples of each block into
a checksum: `9c50ad246f351745` where the compiler fuses no multiply-add (the soft-float M4,
x86-64), `adc3c78fb10efbd9` where it does (the M4F, M33, M55 and Hexagon), the header kernel
compiled under the workload's flags as the stage's card says. Every key counted C++20 and C++23
equal to C++17.

| Key | `recursive` |
|---|--:|
| `m4-softfp` | 121,018,351 |
| `m4f` | 4,198,120 |
| `m33` | 4,325,126 |
| `m55` | 3,698,128 |
| `hexagon` | 1,369,906 packets |
| `host` | 5,887,167 (inside `catenae_host_counted()`) |

### What a recursive design's make() costs

Each `callmax_design_` workload brackets every `make()` it calls and counts the worst single
call (`bench/icount/design_main.cpp`, QEMU keys only): the cookbook's nine at 0.05 and 0.2, and
each Butterworth at 0.45 / 2, 4, 8 and 16, in float. The stack is the `designers` probe's
(`bench/probe/designers_probe.cpp`): the static worst path, then the painted high-water, in
bytes. Both from Bench run [45837885047](https://github.com/tap/catenae/actions/runs/45837885047);
the checksums of what `make()` returned are the same on every key. The M4F and M33 pay for
software double, as the soft-float M4 does; the M55 has it in hardware.

| `make()` | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` | stack, `m33` |
|---|--:|--:|--:|--:|--:|--:|
| cookbook, one section | 28,069,964 | 28,115,880 | 28,189,561 | 919,750 | 5,866,129 packets | 1,380 / 1,056 |
| Butterworth, order 2 | 9,721,879 | 9,740,915 | 9,788,708 | 311,692 | 1,456,174 packets | 1,300 / 992 |
| Butterworth, order 4 | 16,627,420 | 16,661,667 | 16,757,532 | 481,214 | 2,080,746 packets | 2,008 / 2,000 |
| Butterworth, order 8 | 32,722,345 | 32,782,041 | 32,954,144 | 900,043 | 3,329,290 packets | 6,120 / 6,112 |
| Butterworth, order 16 | 74,346,227 | 74,436,657 | 74,826,914 | 2,249,441 | 6,743,188 packets | 22,008 / 22,000 |

### The FIR filters

`fir_f32`, `fir_f64`, `fir_q15` and `fir_q31` run one filter in four formats
(`bench/icount/fir_main.cpp`): the 63-tap low-pass at 0.1 of `examples/fir_tables.h`, stereo, over
1000 blocks of 64 frames of the same input scaled to the format, every design a constant. Each
folds the first and last samples of each block into a checksum. The fixed formats and `double`
print the same checksum on every key (`2c794b223684365e`, `315dd35e45c86e20`,
`8c3f64e99e63dea1`); `float` prints `a26c8c32d8c562c8` where the compiler fuses no multiply-add
(the soft-float M4, x86-64) and `92f3bc45c548056d` where it does, the header kernel compiled
under the workload's flags as the stage's card says. From Bench run
[45866666441](https://github.com/tap/catenae/actions/runs/45866666441); every key counted C++20
and C++23 equal to C++17.

| Key | `fir_f32` | `fir_f64` | `fir_q15` | `fir_q31` |
|---|--:|--:|--:|--:|
| `m4-softfp` | 705,440,641 | 1,011,192,415 | 45,501,874 | 85,963,195 |
| `m4f` | 35,779,214 | 997,753,627 | 45,501,885 | 85,963,206 |
| `m33` | 35,779,220 | 997,625,632 | 45,373,890 | 85,963,211 |
| `m55` | 33,861,216 | 33,991,851 | 37,824,889 | 69,450,210 |
| `hexagon` | 9,741,896 packets | 34,898,696 packets | 10,750,683 packets | 10,961,895 packets |
| `host` | 11,711,178 | 36,540,301 | 76,985,103 | 77,115,167 |

A run writes 128,000 outputs, so on the M33 the float filter costs 280 instructions an output,
4.4 a tap, and the double one 7,794, software double on an FPU of single precision. The scalar
Q15 kernel is dearer than float there (354 an output): one 16 by 16 multiply-accumulate into 64
bits per tap, with the paired SMLALD and Helium kernels waiting for C6. Q31 pre-shifts each 64-bit
product, 672 an output.

### What an FIR design's make() costs

Each `callmax_design_fir_` workload brackets every `make()` and counts the worst single call
(`bench/icount/fir_design_main.cpp`, QEMU keys only): a low-pass at 0.05, 0.1, 0.2 and 0.4, 80 dB
down, of 63 float taps and 255 Q15 taps by value, and of 1,023 float taps into a table through a
workspace. The stack is the `designers` probe's: the static worst path, then the painted
high-water, in bytes. From Bench run
[45866666441](https://github.com/tap/catenae/actions/runs/45866666441); the checksums of what
`make()` returned are the same on every key.

| `make()` | `m4-softfp` | `m4f` | `m33` | `m55` | `hexagon` | stack, `m33` |
|---|--:|--:|--:|--:|--:|--:|
| 63 float taps | 795,614 | 796,085 | 795,684 | 19,018 | 94,804 packets | 2,084 / 1,400 |
| 255 Q15 taps | 3,293,769 | 3,297,314 | 3,294,376 | 88,170 | 394,638 packets | 3,708 / 3,024 |
| 1,023 float taps, a table | 12,706,746 | 12,715,386 | 12,708,776 | 294,524 | 1,538,792 packets | 1,076 / 392 |

### The rate family

`overdrive` runs the guide's overdrive (`examples/overdrive.h`: `oversample<4, ...>` around a
tanh soft clip, stereo) a callback of 64 frames at a time, and `up2_f32`, `down2_f32`, `up2_q15`,
`down2_q15`, `up2_q31` and `down2_q31` run the half-band at divisor 1 alone, 64 stereo frames in
a call, 128 or 32 out (`bench/icount/resample_main.cpp`), each 1000 times, every design a
generated constant. Each folds the first and last value written each call into a checksum. The
fixed formats print one checksum on every key (`b4fa33dcea64831c` and `5d5b173c1b7c6c9e` in Q15,
`cdffc6290306816e` and `01485ab363c5bcaa` in Q31). The float half-bands print three: one where
the compiler fuses no multiply-add (the soft-float M4, x86-64: `6a34b58fb45bd514` up,
`fbf41613c04c15aa` down), one where the scalar code fuses (M4F, M33, Hexagon: `85a712805a4dc03e`,
`573a571ae81794cd`), and the M55's (`8cbec0cbb9244db1`, `063406a5b267751d`), whose Helium code
fuses differently; built with `-ffp-contract=off` the M55 prints the first pair. The overdrive
prints four: `709de89e007bb0c3` (soft-float M4, x86-64), `af5651ac7ac6397a` (M4F, M33),
`d541e226d25adf60` (Hexagon) and `76a1d686e73b97cc` (M55). Float carries a floor against the
golden model, not a bit pin across targets (the stage's card). From Bench run
[45922339828](https://github.com/tap/catenae/actions/runs/45922339828); every key counted C++20
and C++23 equal to C++17.

| Key | `overdrive` | `up2_f32` | `down2_f32` | `up2_q15` | `down2_q15` | `up2_q31` | `down2_q31` |
|---|--:|--:|--:|--:|--:|--:|--:|
| `m4-softfp` | 2,143,113,838 | 281,690,843 | 148,045,279 | 29,475,515 | 14,827,871 | 41,068,828 | 21,861,098 |
| `m4f` | 206,448,666 | 26,858,507 | 15,210,034 | 29,475,525 | 14,827,881 | 41,068,840 | 21,861,110 |
| `m33` | 206,284,021 | 27,050,511 | 15,498,006 | 29,219,530 | 15,147,884 | 40,752,813 | 21,863,115 |
| `m55` | 248,344,025 | 34,674,494 | 17,744,116 | 27,050,529 | 13,263,853 | 38,514,811 | 20,077,113 |
| `hexagon` | 150,069,607 packets | 15,192,782 packets | 2,596,906 packets | 11,210,793 packets | 3,617,257 packets | 12,635,995 packets | 5,778,408 packets |
| `host` | 199,631,655 | 23,740,916 | 12,563,680 | 30,133,944 | 15,489,936 | 33,659,882 | 18,001,695 |

On the M33 the float up2 costs 106 instructions a sample it writes and down2 242: up2's two
phases are its 20 even taps and the centre, a copy, so an output dots 10.5 taps on average,
where every one of down2's dots 21. Q15 costs about what float does there (8 % more up, 2 %
less down) and Q31 1.4 to 1.5 times it. The M55 runs the float half-bands 14 to 28 % dearer than
the M33 and the fixed formats 5 to 12 % cheaper. The overdrive costs 1,612 instructions a sample on the M33: four half-bands and
four `tanhf` calls a sample at four times the rate. Its state is 3,888 bytes a voice and its
workspace 4,616 (`sizeof.state`, `sizeof.workspace` in the probe table above), and its
`process` reaches 352 bytes of stack, static and painted alike.

### The floating FFT

`fft_f32_512`, `fft_f32_2048` and `fft_f64_512` run a forward and an inverse transform of each
frame over 2^20 samples (`bench/icount/fft_main.cpp`, DspTap's `rfft_*` scenarios on catenae's
plan and frame); `callmax_fft_f32_512` is the worst single transform at 512 in float. Each
prints the same checksum on every key: the engine's bits do not depend on the target.

| Key | `fft_f32_512` | `fft_f32_2048` | `fft_f64_512` | `callmax_fft_f32_512` |
|---|--:|--:|--:|--:|
| `m4-softfp` | 1,830,311,721 | 2,261,476,005 | 2,445,329,815 | 432,919 |
| `m4f` | 79,981,060 | 96,163,204 | 2,436,625,831 | 17,243 |
| `m33` | 79,768,128 | 95,909,313 | 2,438,010,308 | 17,151 |
| `m55` | 82,942,533 | 98,849,221 | 89,437,278 | 18,046 |
| `hexagon` | 26,970,829 packets | 33,241,293 packets | 58,352,117 packets | 6,021 |
| `host` | 96,305,187 | 115,922,467 | 94,855,204 | (QEMU keys only) |

The double scenario is software double on the M4, M4F and M33, whose FPUs are single
precision, and an instruction per operation on the M55 and the host.
