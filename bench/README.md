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
paints the stack below each call and reads how deep it went. `scripts/ceilings.py` reads, per
key:

- the image's `.text`, `.rodata`, `.data` and `.bss`, apart (`size -A`);
- the same four for the compiled core (`core.*`, its archive members) and for the probe's own
  unit (`unit.*`, where the header templates it instantiates land), from the link map;
- the start-up functions catenae's own objects add (none, by D34), and the static worst-case
  stack of every start-up function in the image, the runtime's included;
- each call's static worst-case stack down its path, from GCC's `-fcallgraph-info` with the
  runtime's routines bounded from their disassembly, and the high-water it actually painted
  (Arm keys).

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

<!-- ceilings:end -->

## The link-stage gate

`scripts/link_gate.py` refuses an image that links a software-double or double-`libm` symbol
(D36). Its probe images, one per `float` stage family, arrive with C1; until then it proves
itself on two fixtures on every Arm key (`bench/link_gate/`, CTest label `link_gate`). On the
M55, GCC's default FPU has double precision, so a double product is an instruction and no
symbol: the gate cannot see it there, and a test pins that limit.

## Recorded

Nothing is recorded yet.
