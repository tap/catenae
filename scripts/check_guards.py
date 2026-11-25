#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""D40's include guards: the C++ Core Guidelines' style, not #pragma once.

    scripts/check_guards.py [--root DIR]

Every header of catenae's own code opens with `#ifndef G` and `#define G` before any other
directive or code, closes with `#endif // G`, and never says `#pragma once`. G is
CATENAE_<PATH>_<FILE>_H, spelled from the header's path under include/catenae/
(catenae/detail/cx_math.h is CATENAE_DETAIL_CX_MATH_H) or, for a header outside it, from the
root (tools/probe/stack_meter.h is CATENAE_TOOLS_PROBE_STACK_METER_H).
"""
import argparse
import pathlib
import re
import sys

import checks_common as common


def expected_guard(rel: str) -> str:
    path = rel[len("include/catenae/"):] if rel.startswith("include/catenae/") else rel
    return "CATENAE_" + re.sub(r"[^A-Za-z0-9]", "_", path).upper()


def code_lines(text: str) -> list[tuple[int, str]]:
    """(line number, stripped text) of every line that is not blank or wholly a comment."""
    out, in_block = [], False
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if in_block:
            if "*/" in line:
                in_block = False
                line = line.split("*/", 1)[1].strip()
            else:
                continue
        if line.startswith("/*") and "*/" not in line:
            in_block = True
            continue
        if not line or line.startswith("//") or (line.startswith("/*") and line.endswith("*/")):
            continue
        out.append((number, line))
    return out


def check(path: pathlib.Path, rel: str) -> list[str]:
    text = path.read_text()
    guard = expected_guard(rel)
    problems = []
    if re.search(r"^\s*#\s*pragma\s+once\b", text, re.M):
        problems.append("uses #pragma once (D40: an include guard instead)")
    lines = code_lines(text)
    if len(lines) < 3 or not re.fullmatch(rf"#\s*ifndef\s+{guard}", lines[0][1]) or \
            not re.fullmatch(rf"#\s*define\s+{guard}", lines[1][1]):
        problems.append(f"does not open with #ifndef {guard} / #define {guard}")
    last = text.rstrip().splitlines()[-1].strip() if text.strip() else ""
    if not re.fullmatch(rf"#\s*endif\s*//\s*{guard}", last):
        problems.append(f"does not close with #endif // {guard}")
    return [f"{rel}: {p}" for p in problems]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    args = ap.parse_args()
    root = args.root.resolve()
    headers = common.source_files(root, suffixes=(".h",))
    failures = []
    for path in headers:
        failures += check(path, path.relative_to(root).as_posix())
    return common.report(failures, f"D40: {len(headers)} header(s), each with its include guard.")


if __name__ == "__main__":
    sys.exit(main())
