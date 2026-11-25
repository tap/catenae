#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The deterministic instruction-count ratchet (bench/README.md).

Carried from SampleRateTap's scripts/icount.py (NOTICE.md), with DspTap's keys and its
--record; the markers and names are catenae's.

  icount.py --key KEY --build-dir DIR [--plugin LIB] [--baselines FILE] [--tolerance 0.03]
            [--update] [--record FILE] [--compare-json FILE]

KEY is m4-softfp, m4f, m33 or m55 (qemu-system-arm, with the counting plugin of
tools/qemu_insn_plugin), hexagon (qemu-hexagon built with plugin support) or host (x86-64
under valgrind's callgrind). Every catenae_icount_* binary in DIR is a scenario, named by the
rest of its name, and its count is gated two-sided against bench/baselines.json: a regression
beyond the tolerance fails, an improvement beyond it fails too (the baseline must be
re-recorded so the gate stays tight), and so do a scenario with no baseline and a baseline
with no binary. --update rewrites the key's entry to exactly the measured scenarios.

What a count is. Under QEMU, the whole run's executed instructions, start-up included; for a
callmax_* scenario, the most executed between one pair of tools/probe/call_marker.h's markers
(the worst single call). On the host, the instructions callgrind collects inside
catenae_host_counted() alone (--toggle-collect), with the environment pinned so the count
follows the binary and the C library, not the runner's CPU: eager binding, and glibc's
CPU-dispatched routines held to their x86-64 baseline. callmax_* scenarios are QEMU-only.

--record writes {key: {scenario: {insns, checksum}}} whatever the verdict, so a run that
fails for want of a baseline still hands back the numbers to commit. --compare-json gates
exactly (counts and checksums) against such a file instead of the baselines: D2's check that
the counts are equal at C++17, 20 and 23 measures the three trees with one toolchain and
compares the later two to the first.
"""
import argparse
import glob
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
PREFIX = "catenae_icount_"
DONE = re.compile(r"CATENAE_ICOUNT_DONE ok=1 checksum=(\S+)")
COUNT = re.compile(r"CATENAE_INSN_COUNT (\d+)")
CALL_MAX = re.compile(r"CATENAE_CALL_MAX (\d+) call=(\d+) calls=(\d+) total=(\d+)")
CALL_MAX_PREFIX = "callmax_"
MACHINES = {"m4-softfp": "mps2-an386", "m4f": "mps2-an386", "m33": "mps2-an505", "m55": "mps3-an547"}
KEYS = sorted([*MACHINES, "hexagon", "host"])

# qemu-hexagon is a user-mode emulator: the guest's argv[0], its exec path and the host
# environment are copied onto the guest stack and static musl's start-up walks them, so their
# lengths leak into the count. Every workload runs from one fixed path, with a fixed argv[0]
# and an empty environment.
HEXAGON_RUN_DIR = "/tmp/catenae-icount"
HEXAGON_ARGV0 = "w"

HOST_COUNTED = "catenae_host_counted"
HOST_COLLECTED = re.compile(r"Collected : (\d+)")
HOST_ENV = {
    "LD_BIND_NOW": "1",
    "GLIBC_TUNABLES": "glibc.cpu.hwcaps=" + ",".join(
        "-" + f for f in ("AVX", "AVX2", "AVX512F", "AVX512VL", "AVX512BW", "AVX512DQ",
                          "ERMS", "FSRM", "FMA", "FMA4", "BMI1", "BMI2", "LZCNT", "MOVBE",
                          "POPCNT", "SSE4_1", "SSE4_2", "SSSE3", "AVX_Fast_Unaligned_Load",
                          "Prefer_No_VZEROUPPER", "Fast_Unaligned_Copy"))
    + ":glibc.cpu.x86_non_temporal_threshold=0x100000"
    + ":glibc.cpu.x86_rep_movsb_threshold=0x800"
    + ":glibc.cpu.x86_rep_stosb_threshold=0x800",
}


def run(cmd: list[str], env=None) -> str:
    try:
        proc = subprocess.run(cmd, timeout=600, capture_output=True, text=True, env=env)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{cmd[-1]}: timed out after 600 s")
    return proc.stdout + proc.stderr


def measure(key: str, plugin: str | None, binary: str, call_max: bool) -> tuple[int, str]:
    label = os.path.basename(binary)
    if key == "host":
        valgrind = shutil.which("valgrind") or sys.exit("valgrind not found on PATH")
        out = run([valgrind, "--tool=callgrind", f"--toggle-collect={HOST_COUNTED}",
                   "--callgrind-out-file=/dev/null", binary], env=HOST_ENV)
        count = HOST_COLLECTED.search(out)
        if not count or int(count.group(1)) == 0:
            print(out, file=sys.stderr)
            raise SystemExit(f"{label}: callgrind collected nothing (no {HOST_COUNTED}?)")
    else:
        if key == "hexagon":
            qemu = shutil.which("qemu-hexagon") or sys.exit("qemu-hexagon not found on PATH")
            os.makedirs(HEXAGON_RUN_DIR, exist_ok=True)
            fixed = os.path.join(HEXAGON_RUN_DIR, HEXAGON_ARGV0)
            shutil.copyfile(binary, fixed)
            os.chmod(fixed, 0o755)
            out = run([qemu, "-0", HEXAGON_ARGV0, "-d", "plugin", "-plugin", plugin, fixed], env={})
        else:
            # "-d plugin" routes qemu_plugin_outs() to stderr; without it the line is dropped.
            out = run(["qemu-system-arm", "-M", MACHINES[key], "-nographic", "-semihosting",
                       "-d", "plugin", "-plugin", plugin, "-kernel", binary])
        count = COUNT.search(out)
        if not count:
            print(out, file=sys.stderr)
            raise SystemExit(f"{label}: no CATENAE_INSN_COUNT (plugin not loaded?)")
    done = DONE.search(out)
    if not done:
        print(out, file=sys.stderr)
        raise SystemExit(f"{label}: the workload did not complete cleanly")
    if call_max:
        c = CALL_MAX.search(out)
        if not c:
            print(out, file=sys.stderr)
            raise SystemExit(f"{label}: no CATENAE_CALL_MAX (markers not found?)")
        worst, at, calls, total = (int(g) for g in c.groups())
        print(f"{label}: worst call {at} of {calls}: {worst} insns (mean {total / calls:.1f}, "
              f"whole run {count.group(1)})")
        return worst, done.group(1)
    return int(count.group(1)), done.group(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--key", required=True, choices=KEYS)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--plugin", help="the QEMU counting plugin (not used by --key host)")
    ap.add_argument("--baselines", default=str(REPO / "bench" / "baselines.json"))
    ap.add_argument("--tolerance", type=float, default=0.03)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--record", help="write the measured counts and checksums here, whatever the verdict")
    ap.add_argument("--compare-json", help="gate exactly against a --record file instead of the baselines")
    args = ap.parse_args()
    if args.key != "host" and not args.plugin:
        raise SystemExit(f"--plugin is required for --key {args.key}")
    if args.update and args.compare_json:
        raise SystemExit("--update and --compare-json are mutually exclusive")

    binaries = sorted(b for b in glob.glob(os.path.join(args.build_dir, "**", PREFIX + "*"), recursive=True)
                      if os.path.isfile(b) and os.access(b, os.X_OK))
    if args.key == "host":
        binaries = [b for b in binaries if not os.path.basename(b)[len(PREFIX):].startswith(CALL_MAX_PREFIX)]
    if not binaries:
        raise SystemExit(f"no {PREFIX}* binaries under {args.build_dir}")

    tolerance, checksums_ref = args.tolerance, {}
    path = pathlib.Path(args.baselines)
    baselines = json.loads(path.read_text()) if path.exists() else {}
    if args.compare_json:
        ref = json.loads(pathlib.Path(args.compare_json).read_text()).get(args.key, {})
        if not ref:
            raise SystemExit(f"{args.compare_json} has no {args.key} measurements")
        base = {k: v["insns"] for k, v in ref.items()}
        checksums_ref = {k: v["checksum"] for k, v in ref.items()}
        tolerance = 0.0
    elif not path.exists() and not args.update:
        raise SystemExit(f"baselines file {path} not found")
    else:
        base = baselines.get(args.key, {})

    failures, measured = [], {}
    for binary in binaries:
        scenario = os.path.basename(binary)[len(PREFIX):]
        count, checksum = measure(args.key, args.plugin, binary, scenario.startswith(CALL_MAX_PREFIX))
        measured[scenario] = {"insns": count, "checksum": checksum}
        recorded = base.get(scenario)
        if recorded is None:
            print(f"{scenario}: {count} insns, checksum {checksum} (NO BASELINE: commit this value)")
            failures.append(scenario)
            continue
        delta = (count - recorded) / recorded if recorded else float("inf")
        verdict = "ok"
        if tolerance == 0.0 and count != recorded:
            verdict = "MISMATCH (exact)"
        elif delta > tolerance:
            verdict = "REGRESSION"
        elif delta < -tolerance:
            verdict = "IMPROVED beyond tolerance: re-record the baseline (icount.py --update)"
        if verdict != "ok":
            failures.append(scenario)
        print(f"{scenario}: {count} insns vs {recorded} ({count - recorded:+d}, {delta:+.4%}) {verdict}")
        if scenario in checksums_ref and checksums_ref[scenario] != checksum:
            print(f"{scenario}: checksum {checksum} vs {checksums_ref[scenario]} MISMATCH")
            failures.append(scenario)
    # The measured set must be exactly the recorded one: a workload whose binary vanished
    # would otherwise simply stop being gated.
    for scenario in sorted(set(base) - set(measured)):
        print(f"{scenario}: recorded but NOT MEASURED (binary missing)")
        failures.append(scenario)

    if args.record:
        record = pathlib.Path(args.record)
        doc = json.loads(record.read_text()) if record.exists() else {}
        doc[args.key] = dict(sorted(measured.items()))
        record.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
    if args.update:
        baselines[args.key] = {k: v["insns"] for k, v in sorted(measured.items())}
        path.write_text(json.dumps(baselines, indent=2, sort_keys=True) + "\n")
        print(f"updated {path} ({args.key})")
        return 0
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
