#!/usr/bin/env python3
"""Host count of DspTap's bench/layout workloads: callgrind collecting only
layout_step (bench/layout/layout_main.cpp names this toggle), in the pinned
environment SampleRateTap's host ratchet uses. Prints p / i per pair."""
import glob, json, os, re, subprocess, sys
sys.path.insert(0, sys.argv[2])            # a directory holding SampleRateTap's icount.py
import icount
build, out_json = sys.argv[1], sys.argv[3]
bins = sorted(b for b in glob.glob(os.path.join(build, "**", "tap_dsp_layout_*"), recursive=True)
              if os.path.isfile(b) and os.access(b, os.X_OK))
counts, sums = {}, {}
for b in bins:
    name = os.path.basename(b).removeprefix("tap_dsp_layout_")
    p = subprocess.run(["valgrind", "--tool=callgrind", "--toggle-collect=*layout_step*",
                        "--callgrind-out-file=/dev/null", b], capture_output=True, text=True, env=icount.HOST_ENV)
    out = p.stdout + p.stderr
    m = icount.HOST_COLLECTED.search(out); d = re.search(r"TAP_DSP_ICOUNT_DONE ok=1 .*checksum=(\S+)", out)
    if p.returncode or not m or not d: print(out); raise SystemExit(f"{name}: no count")
    counts[name], sums[name] = int(m.group(1)), d.group(1)
def order(n):
    m = re.match(r"(.*)_ch(\d+)_", n); return (m.group(1), int(m.group(2)))
print(f"{'pair':<16} {'interleaved':>12} {'planar':>12} {'p / i':>7}  output")
for n in sorted(counts, key=order):
    if n.endswith("_p"):
        i, p = counts[n[:-2] + "_i"], counts[n]
        print(f"{n[:-2]:<16} {i:>12} {p:>12} {p / i:>7.3f}  {'bit-identical' if sums[n] == sums[n[:-2] + '_i'] else 'DIFFERENT'}")
json.dump(counts, open(out_json, "w"), indent=1, sort_keys=True)
