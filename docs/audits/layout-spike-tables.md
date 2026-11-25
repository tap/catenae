# The layout spike's tables

`PLAN.md` section 5.1 quotes these. They were measured in DspTap, on its branch
`ccr-ef01ff18-nxz8n4` (commit `0fd75b2`), and are copied here from that branch's
`bench/README.md` because the branch is a source, not a dependency (D3), and need not
outlive the port. Each figure is planar cost over interleaved cost: below 1, planar is
cheaper. The counts are instructions (packets on `hexagon`) of the streaming part of the
benchmark, at 2, 6 and 12 channels.

The Butterworth (`bw`), the overlap-save FFT FIR (`fir`) and the two as a chain, per core.
DspTap `bench.yml` run 161, at `3d0e5ec`, 2026-11-22, with the Butterworth on
`iir_kernels.h`'s per-sample cascade:

| key | `bw` 2 | `bw` 6 | `bw` 12 | `fir` 2 | `fir` 6 | `fir` 12 | `chain` 2 | `chain` 6 | `chain` 12 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `m4-softfp` | 0.985 | 1.010 | 0.996 | 1.002 | 1.006 | 1.004 | 0.998 | 1.001 | 1.007 |
| `m4f` | 0.860 | 0.932 | 0.947 | 1.052 | 1.078 | 1.065 | 1.010 | 1.021 | 1.005 |
| `m33` | 0.860 | 0.932 | 0.947 | 1.051 | 1.074 | 1.056 | 1.010 | 1.021 | 1.000 |
| `m55` | 0.897 | 0.971 | 0.987 | 1.059 | 1.097 | 1.056 | 0.975 | 1.015 | 1.043 |
| `m55-ooura` | 0.897 | 0.971 | 0.987 | 1.040 | 1.064 | 1.042 | 0.999 | 1.015 | 1.044 |
| `hexagon` | 1.022 | 1.074 | 0.950 | 1.137 | 1.158 | 1.162 | 1.100 | 1.129 | 1.101 |

On the host (x86-64, GCC 13.3, callgrind) the same run gave `bw` 0.911 / 0.924 / 0.935,
`fir` 1.036 / 1.061 / 1.064 and `chain` 0.999 / 1.016 / 1.009.

The Butterworth row is of a kernel that runs one channel and one sample at a time through a
run-time section count, which no core vectorizes across channels. Run 153 measured the
branch's earlier kernel (a compile-time section count, the channel loop inside the stage)
at 2.2 times in interleaved's favour on the M55 at 12 channels. That is why the plan
measures the recursive stage again at C3, on the block kernel.

A stateless gain, with the channel count a template argument in both layouts. DspTap
`bench.yml` run 157, at `25aee9e`:

| key | `gain` 2 | `gain` 6 | `gain` 12 |
|---|---:|---:|---:|
| host (x86-64) | 0.878 | 0.471 | 0.515 |
| `m4-softfp` | 1.018 | 1.041 | 1.050 |
| `m4f` | 1.012 | 1.301 | 1.403 |
| `m33` | 1.012 | 1.301 | 1.403 |
| `m55` | 1.248 | 0.276 | 0.291 |
| `m55-ooura` | 1.248 | 0.276 | 0.291 |
| `hexagon` | 1.008 | 1.013 | 1.014 |

A separate measurement of the same gain loops outside a stage showed that planar does not
care whether its channel count is a template argument (within 2 %), while interleaved does,
by up to 12 times on the M55 at 2 channels.

On the `m55` keys the planar and interleaved `fir` outputs differ in their last bits: the
benchmark is built with GCC's default contraction, which fuses the two instantiations
differently under Helium. Every other pair is bit-identical.

## Since the spike: the block kernel, and a Cortex-A78

Two sets of figures are newer than the tables above.

**The Butterworth on the kernel DspTap ships since 0.7.0** (`biquad_cascade_block`: a
compile-time section count, the coefficients and state in locals for a block). From DspTap's
own `bench/README.md` on `main` (`b26a7db`), `bench.yml` run 173 at `5634a73`, 2026-11-23;
every pair bit-identical on every key:

| key | `bw4` 2 | `bw4` 6 | `bw4` 12 | `bw8` 2 | `bw8` 6 | `bw8` 12 |
|---|---:|---:|---:|---:|---:|---:|
| `m4-softfp` | 1.003 | 1.003 | 1.003 | 0.999 | 0.999 | 0.999 |
| `m4f` | 1.004 | 1.001 | 1.001 | 0.948 | 0.949 | 0.948 |
| `m33` | 1.004 | 1.001 | 1.001 | 0.952 | 0.952 | 0.952 |
| `m55` | 1.005 | 1.004 | 1.005 | 0.950 | 0.951 | 0.951 |
| `hexagon` | 1.284 | 1.287 | 1.288 | 1.000 | 1.002 | 1.002 |

Both layouts cost 1.9 to 3.5 times less on this kernel than on the per-sample one the spike
measured. Running the channels in vector lanes was tried for it in DspTap and not kept.
The gain's figures in run 173 are those of run 157 above.

**A Cortex-A78, and the host figures run 173 lacks.** Measured locally on 2026-11-25, not in
CI. Planar over interleaved, with the bracketed instruction counts (interleaved / planar):

| what | tree | 2 ch | 6 ch | 12 ch | counts at 2 ch | at 6 ch | at 12 ch |
|---|---|---:|---:|---:|---|---|---|
| async converter | SampleRateTap spike `8bd3e1f` | 1.213 | 2.486 | 3.208 | 46,455,884 / 56,350,597 | 68,106,174 / 169,310,082 | 106,080,096 / 340,269,288 |
| rational converter | the same | 1.109 | 1.204 | 1.232 | 21,386,727 / 23,710,203 | 59,059,239 / 71,125,359 | 115,443,990 / 142,243,593 |
| bridge converter | the same | 1.034 | 1.046 | 1.059 | 34,683,591 / 35,866,455 | 102,886,392 / 107,594,115 | 203,212,029 / 215,181,105 |
| `fir` | DspTap spike `0fd75b2` | 1.046 | 1.093 | 1.074 | 3,709,696 / 3,878,784 | 10,639,360 / 11,629,056 | 21,056,000 / 22,620,672 |
| `bw4`, block kernel | DspTap main `b26a7db` | 1.066 | 1.060 | 1.060 | 528,640 / 563,456 | 1,594,880 / 1,689,856 | 3,186,816 / 3,377,920 |
| `bw8`, block kernel | the same | 1.001 | 1.001 | 1.001 | 1,065,216 / 1,066,112 | 3,193,728 / 3,197,824 | 6,387,712 / 6,393,472 |
| `gain` | the same | 1.925 | 0.358 | 0.385 | 29,184 / 56,192 | 461,312 / 165,248 | 854,528 / 328,832 |

Every pair's outputs are bit-identical. Also on the A78, from the spike tree: the per-sample
Butterworth 0.915 / 0.869 / 0.886, and the `chain` 1.037 / 1.050 / 1.023 with its two layouts'
last bits different.

On the x86-64 host (GCC 13.3, callgrind), DspTap main: `bw4` 1.036 / 1.035 / 1.035, `bw8`
1.000 / 1.000 / 1.000, `gain` 0.878 / 0.471 / 0.515 as published.

How the A78 figures were made:

- `aarch64-linux-gnu-g++` 13.3.0 (Ubuntu 24.04), `-mcpu=cortex-a78 -O3 -DNDEBUG`, static
  glibc 2.39; CMake Release, as the other keys.
- User-mode `qemu-aarch64` 8.2.2 built from the source and digest the workflows pin, with
  `--enable-plugins` (Ubuntu's packaged one refuses `-plugin`), and each repository's own
  `insn_count.c`.
- `-cpu cortex-a76`: QEMU 8.2.2 has no `cortex-a78` model. The model matters a little.
  `neoverse-n1` gives the same counts, but `max` gives up to 1.4 % fewer, because static glibc
  chooses `memcpy` and its kin by CPU feature and `max` has SVE. `bw4`, which calls no C
  library routine in its bracket, counts the same on all three.
- Checks: every count run twice and equal; the same harness reproduced run 173's Cortex-M33
  row exactly, SampleRateTap's published host ratios (one cell differs in its third decimal)
  and the spike's published DspTap host figures; one A78 count was confirmed from QEMU's own
  execution log without the plugin.

`layout-a78/` holds the toolchain file, the script patches and the notes on what a CI key for
this core would need in DspTap and SampleRateTap. None of it is applied anywhere.

