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
