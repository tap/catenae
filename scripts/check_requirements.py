#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The @req trace (PLAN.md sections 8 and 11.2): every requirement is cited by a test, and
every test cites one.

    scripts/check_requirements.py [--root DIR] [--trace FILE]

A requirement is declared once, as `@req{CAT-<AREA>-<nnn>}` in a `///` comment of catenae's
own headers or sources (include/, src/), where Doxygen collects it into the requirements page.
A test cites the requirements it pins by naming their identifiers in the comment directly
above it (`// Verifies CAT-VERSION-001.`). A test is any GoogleTest TEST, TEST_F, TEST_P,
TYPED_TEST or TYPED_TEST_P under tests/. Failures: a requirement declared twice, one no test
cites, a test that cites none, and a citation of an identifier nothing declares. --trace
writes the trace as a Markdown table.
"""
import argparse
import pathlib
import re
import sys

import checks_common as common

IDENT = r"CAT-[A-Z][A-Z0-9]*(?:-[A-Z][A-Z0-9]*)*-\d{3}"
DECLARED = re.compile(r"@req\{(" + IDENT + r")\}")
CITED = re.compile(r"\b(" + IDENT + r")\b")
TEST = re.compile(r"^\s*(TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)")


def declarations(root: pathlib.Path) -> tuple[dict[str, str], list[str]]:
    declared, failures = {}, []
    for path in common.source_files(root, common.LIBRARY_DIRS):
        rel = path.relative_to(root).as_posix()
        for number, line in enumerate(path.read_text().splitlines(), 1):
            for ident in DECLARED.findall(line):
                if ident in declared:
                    failures.append(f"{rel}:{number}: {ident} is declared again (first at {declared[ident]})")
                else:
                    declared[ident] = f"{rel}:{number}"
    return declared, failures


def tests(root: pathlib.Path) -> list[tuple[str, str, list[str]]]:
    """(location, suite.name, identifiers in the comment block directly above) of every test."""
    found = []
    for path in common.source_files(root, ("tests",), (".cpp", ".h")):
        rel = path.relative_to(root).as_posix()
        lines = path.read_text().splitlines()
        for i, line in enumerate(lines):
            m = TEST.match(line)
            if not m:
                continue
            cited, j = [], i - 1
            while j >= 0 and lines[j].strip().startswith("//"):
                cited += CITED.findall(lines[j])
                j -= 1
            found.append((f"{rel}:{i + 1}", f"{m.group(2)}.{m.group(3)}", cited))
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    ap.add_argument("--trace", type=pathlib.Path, help="write the trace here as a Markdown table")
    args = ap.parse_args()
    root = args.root.resolve()
    declared, failures = declarations(root)
    cited_by: dict[str, list[str]] = {ident: [] for ident in declared}
    found = tests(root)
    for where, name, cited in found:
        if not cited:
            failures.append(f"{where}: {name} cites no requirement (name its CAT-... identifier in the comment above it)")
        for ident in cited:
            if ident not in declared:
                failures.append(f"{where}: {name} cites {ident}, which nothing declares")
            else:
                cited_by[ident].append(name)
    for ident, names in sorted(cited_by.items()):
        if not names:
            failures.append(f"{declared[ident]}: {ident} is cited by no test")
    if args.trace:
        rows = ["| Requirement | Declared | Tests |", "|---|---|---|"]
        rows += [f"| {i} | `{declared[i]}` | {', '.join(f'`{n}`' for n in cited_by[i])} |" for i in sorted(declared)]
        args.trace.write_text("\n".join(rows) + "\n")
    return common.report(failures, f"@req trace: {len(declared)} requirement(s), {len(found)} test(s), each traced.")


if __name__ == "__main__":
    sys.exit(main())
