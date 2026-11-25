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
<!-- ceilings:end -->

## The link-stage gate

`scripts/link_gate.py` refuses an image that links a software-double or double-`libm` symbol
(D36). Its probe images, one per `float` stage family, arrive with C1; until then it proves
itself on two fixtures on every Arm key (`bench/link_gate/`, CTest label `link_gate`). On the
M55, GCC's default FPU has double precision, so a double product is an instruction and no
symbol: the gate cannot see it there, and a test pins that limit.

## Recorded

| What | From |
|---|---|
| `baselines.json`, every key; `ceilings.json`, every key but `host` | Bench run [45614209215](https://github.com/tap/catenae/actions/runs/45614209215) at `1cd47fa` (the run names `c4d0c87`, the same tree before the C0a pull request was rebased onto `main`), 2026-11-25: its `measured-<key>` artifacts, copied unchanged, the ceilings computed by `scripts/ceilings.py`'s rule |
| `baselines.json`: `strip_chain` and `strip_graph`, every key | Bench run [45653902512](https://github.com/tap/catenae/actions/runs/45653902512) at `66debaa`, 2026-11-25: its `measured-<key>` artifacts, copied unchanged; every other count in that run equal to its baseline |

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
| `m4-softfp` | 24,160,128 | 23,574,864 | +585,264 (+2.5 %) |
| `m4f` | 3,796,665 | 3,840,400 | -43,735 (-1.1 %) |
| `m33` | 3,860,694 | 3,841,430 | +19,264 (+0.5 %) |
| `m55` | 2,375,698 | 2,472,434 | -96,736 (-3.9 %) |
| `hexagon` | 1,172,150 packets | 1,299,888 packets | -127,738 (-9.8 %) |
| `host` | 2,777,174 | 3,203,166 | -425,992 (-13.3 %) |

A QEMU key's count is the whole run, start-up, input, checksum and `printf` included; the
`host` count is the loop alone (`catenae_host_counted()`). Those parts are the same in both
binaries, so the difference is the strip's alone. On the Cortex-M cores the two cost about
the same, within 4 % either way; on Hexagon and the host the chain is the cheaper. What the
graph gives up is not speed: a node whose channel count differs from the one before it is
found when it is connected, as the program runs (`graph::connect` returns false), where the
chain refuses it when its type is formed, before the program exists.
