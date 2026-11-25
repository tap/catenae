#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""Statement and branch coverage of the compiled core and of each instantiation the battery
makes (PLAN.md section 8), measured and reported, never gated.

    cmake -S . -B build-coverage -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DCATENAE_COVERAGE=ON
    cmake --build build-coverage
    scripts/coverage.py --build-dir build-coverage [--summary FILE] [--html DIR]

Runs the battery (ctest) with each process writing its own raw profile, merges them, and
reports Clang's source-based coverage of include/ and src/ alone: lines, regions (statements),
branches and functions, with a summary of each template's instantiations. The configuration is
the one that ships: optimised, at the boundary check level. --summary writes the report as
Markdown (CI's job summary); --html writes llvm-cov's annotated sources.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def tool(name: str) -> str:
    for candidate in (os.environ.get(name.upper().replace("-", "_"), ""), f"{name}-18", name):
        if candidate and shutil.which(candidate):
            return candidate
    raise SystemExit(f"no {name} found")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", required=True, type=pathlib.Path)
    ap.add_argument("--summary", type=pathlib.Path)
    ap.add_argument("--html", type=pathlib.Path)
    args = ap.parse_args()
    build = args.build_dir.resolve()
    raw = build / "coverage-raw"
    shutil.rmtree(raw, ignore_errors=True)
    raw.mkdir()
    env = dict(os.environ, LLVM_PROFILE_FILE=str(raw / "%p-%m.profraw"))
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"], check=True, env=env)
    profiles = sorted(raw.glob("*.profraw"))
    if not profiles:
        raise SystemExit("no raw profiles: was the build configured with -DCATENAE_COVERAGE=ON?")
    merged = build / "coverage.profdata"
    subprocess.run([tool("llvm-profdata"), "merge", "-sparse", "-o", str(merged), *map(str, profiles)], check=True)

    # The objects: every executable the battery ran (the tests and the examples).
    objects = sorted(p for p in build.rglob("*") if p.is_file() and os.access(p, os.X_OK) and p.suffix == ""
                     and (p.name.startswith("catenae_tests") or p.name.startswith("catenae_example_")))
    if not objects:
        raise SystemExit("no test executables found")
    object_args = [str(objects[0])] + [a for o in objects[1:] for a in ("-object", str(o))]
    ignore = "-ignore-filename-regex=" + "|".join(["_deps/", "/tests/", "/examples/", "/bench/", "/tools/"])
    cov = tool("llvm-cov")
    common = [f"-instr-profile={merged}", ignore, "-show-branch-summary", "-show-region-summary"]
    report = subprocess.run([cov, "report", *object_args, *common, "-show-instantiation-summary"],
                            check=True, capture_output=True, text=True).stdout
    print(report)
    if args.summary:
        args.summary.write_text("### Coverage of include/ and src/ (Clang source-based, Release, measured, not gated)\n\n"
                                "```\n" + report + "```\n")
    if args.html:
        subprocess.run([cov, "show", *object_args, f"-instr-profile={merged}", ignore, "-show-branches=count",
                        "-show-instantiations", "-format=html", f"-output-dir={args.html}"], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
