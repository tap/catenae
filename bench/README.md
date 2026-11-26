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
| `c2` | C2's edges in one path, 24-bit packed stereo in and out with a gain in float between: the compiled wire loops (`src/wire.cpp`) and the gate's image for the family | `reset`, `process` |
| `fft` | A float transform of 512 both ways from the constant tables, and a plan of 512 made at run time (`include/catenae/fft.h`): the FFT family's image for the link-stage gate | `forward`, `inverse`, `make` |

`scripts/ceilings.py` reads, per key:

- the image's `.text`, `.rodata`, `.data` and `.bss`, apart (`size -A`);
- the same four for the compiled core (`core.*`, its archive members) and for the probe's own
  unit (`unit.*`, where the header templates it instantiates land), from the link map;
- the start-up functions catenae's own objects add (none, by D34), and the static worst-case
  stack of every start-up function in the image, the runtime's included;
- each call's static worst-case stack down its path, from GCC's `-fcallgraph-info` with the
  runtime's routines bounded from their disassembly, and the high-water it actually painted
  (Arm keys);
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
| `image.bss` | 2,980 / 3,072 | 2,980 / 3,072 | 2,980 / 3,072 | 2,980 / 3,072 | 2,252 / 2,368 |
| `image.data` | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 1,372 / 1,472 | 4 / 64 |
| `image.rodata` | 108 / 128 | 108 / 128 | 108 / 128 | 108 / 128 | 56 / 64 |
| `image.text` | 9,304 / 9,600 | 8,088 / 8,384 | 7,976 / 8,256 | 8,020 / 8,320 | 8,628 / 8,896 |
| `core.bss` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.rodata` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `core.text` | 112 / 128 | 112 / 128 | 112 / 128 | 118 / 128 | 136 / 192 |
| `unit.bss` | 1,964 / 2,048 | 1,964 / 2,048 | 1,964 / 2,048 | 1,964 / 2,048 | 1,964 / 2,048 |
| `unit.data` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `unit.rodata` | 20 / 64 | 20 / 64 | 20 / 64 | 20 / 64 | 20 / 64 |
| `unit.text` | 518 / 576 | 506 / 576 | 506 / 576 | 538 / 576 | 1,336 / 1,408 |
| `sizeof.state` | 36 / 64 | 36 / 64 | 36 / 64 | 36 / 64 |  |
| `sizeof.workspace` | 1,928 / 2,048 | 1,928 / 2,048 | 1,928 / 2,048 | 1,928 / 2,048 |  |
| `startup.functions` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `startup.stack` | 12 / 16 | 12 / 16 | 12 / 16 | 12 / 16 |  |
| `stack.process.measured` | 80 / 88 | 24 / 32 | 24 / 32 | 24 / 32 |  |
| `stack.process.static` | 80 / 88 | 24 / 32 | 24 / 32 | 32 / 40 |  |
| `stack.reset.measured` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |  |
| `stack.reset.static` | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |  |
| `code.process` | 1,686 / 1,792 | 612 / 640 | 612 / 640 | 650 / 704 |  |
| `code.reset` | 44 / 64 | 44 / 64 | 44 / 64 | 44 / 64 |  |

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

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so --prefix catenae_layout_ --report-only
```

## The link-stage gate

`scripts/link_gate.py` refuses an image that links a software-double or double-`libm` symbol
(D36). It runs on named probe images, one per `float` stage family; the first is C1's,
`catenae_probe_c1`, every C1 stage in one float path (`link_gate.C1ImageHasNoSoftwareDouble`).
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

| `baselines.json`: `fft_f32_512`, `fft_f32_2048`, `fft_f64_512` and `callmax_fft_f32_512`, every key | Bench run [45762911188](https://github.com/tap/catenae/actions/runs/45762911188) at `1525e89`, 2026-11-26 (`workflow_dispatch` on the C4 FFT branch): its `measured-<key>` artifacts, copied unchanged; every other count in that run equal to its baseline, C++20 and C++23 equal to C++17, and each FFT workload's checksum the same on all six keys. The kernel's recursion became a loop afterwards (`e7bf0d2`, so that the stack bound reads every frame): Bench run [45771572341](https://github.com/tap/catenae/actions/runs/45771572341) counted every FFT workload within 0.75 % of these on every key, inside the gate, and they stand |
| `ceilings.json`: the probe set `fft`, every key but `host` | Bench run [45821938097](https://github.com/tap/catenae/actions/runs/45821938097) at `4989d0e`, 2026-11-26: its `ceilings-measured-<key>.json` artifacts, the ceilings computed by `scripts/ceilings.py`'s rule. Plans made at run time to 2^20 (Q1) widened the kernel's 29 pending entries from 16 to 32 bits, so `forward` and `inverse` reach 56 B more stack on each Arm key (480 B static on the M33, M4F and M55, 568 B on the soft-float M4), and the code figures moved by under 70 B, inside their ceilings, recorded too; every other figure in that run equal to its record (C2's `c2` probe among them), and the link-stage gate's tests passing on each Arm key. The run before, [45771572341](https://github.com/tap/catenae/actions/runs/45771572341) at `e7bf0d2`, recorded the 16-bit list |

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
frames: 24-bit packed stereo unpacked to `q31`, converted to `float` and halved by a gain, and
converted back and packed. It folds the first and last bytes of each block into a checksum,
`176dacc26ecec638` on every key. The compiled loops it calls are two of `src/wire.cpp`'s twenty
(`core.text` in the `c2` table above). Every key counted C++20 and C++23 equal to C++17.

| Key | `edges` |
|---|--:|
| `m4-softfp` | 53,625,379 |
| `m4f` | 9,195,439 |
| `m33` | 9,195,445 |
| `m55` | 8,527,444 |
| `hexagon` | 3,968,705 packets |
| `host` | 10,309,138 (inside `catenae_host_counted()`) |

### The floating FFT

`fft_f32_512`, `fft_f32_2048` and `fft_f64_512` run a forward and an inverse transform of each
frame over 2^20 samples (`bench/icount/fft_main.cpp`, DspTap's `rfft_*` scenarios on catenae's
plan and frame); `callmax_fft_f32_512` is the worst single transform at 512 in float. Each
prints the same checksum on every key: the engine's bits do not depend on the target.

| Key | `fft_f32_512` | `fft_f32_2048` | `fft_f64_512` | `callmax_fft_f32_512` |
|---|--:|--:|--:|--:|
| `m4-softfp` | 1,827,129,136 | 2,260,341,932 | 2,441,467,727 | 432,961 |
| `m4f` | 79,985,163 | 97,255,819 | 2,434,049,887 | 17,391 |
| `m33` | 79,831,625 | 97,044,425 | 2,435,374,978 | 17,368 |
| `m55` | 83,044,942 | 100,065,742 | 86,936,678 | 18,289 |
| `hexagon` | 26,884,812 packets | 33,173,708 packets | 58,262,005 packets | 6,000 |
| `host` | 96,063,525 | 115,896,869 | 94,748,709 | (QEMU keys only) |

The double scenario is software double on the M4, M4F and M33, whose FPUs are single
precision, and an instruction per operation on the M55 and the host.
