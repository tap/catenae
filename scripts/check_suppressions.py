#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""Every suppression is a recorded deviation, and CI counts them (D31, PLAN.md section 7.3).

    scripts/check_suppressions.py [--root DIR] [--log docs/deviations.md]

A suppression is a diagnostic pragma that turns a warning off (`#pragma GCC|clang diagnostic
ignored`, `#pragma warning(disable|suppress`, and their `_Pragma` and `__pragma` forms) or a
clang-tidy `NOLINT`, `NOLINTNEXTLINE` or `NOLINTBEGIN`, in catenae's own C and C++ files; or a
`-Wno-` or `/wd` flag in a build file outside tools/warnings/, which is the warning sets' one
home and gives each exclusion its reason there. Each suppression cites a deviation-log entry
(DEV-<number>) on its own line or the line before. The log (docs/deviations.md, from C0b) is a
Markdown table whose first column is the entry's identifier and which has a "Uses" column; the
count of citations of each entry must equal its "Uses", so a suppression can be neither added
nor removed without the log saying so. An entry whose "Uses" is "—" (or "-") is a construct,
not a suppression, and a suppression that cites one fails. Without a log, any suppression
fails.
"""
import argparse
import pathlib
import re
import sys

import checks_common as common

PRAGMA = re.compile(
    r"(#\s*pragma\s+(GCC|clang)\s+diagnostic\s+ignored|#\s*pragma\s+warning\s*\(\s*(disable|suppress)"
    r"|_Pragma\s*\(\s*\"\s*(GCC|clang)\s+diagnostic\s+ignored|__pragma\s*\(\s*warning\s*\(\s*(disable|suppress)"
    r"|\bNOLINT(NEXTLINE|BEGIN)?\b)")
FLAG = re.compile(r"(?<![\w-])(-Wno-[\w+=-]+|/wd\d+)")
DEV = re.compile(r"\bDEV-\d{3}\b")
BUILD_FILES = ("CMakeLists.txt", "wscript")


def build_files(root: pathlib.Path) -> list[pathlib.Path]:
    found = []
    for path in root.rglob("*"):
        rel = path.relative_to(root).as_posix()
        if not path.is_file() or common.excluded(rel) or rel.startswith("tools/warnings/"):
            continue
        if path.name in BUILD_FILES or path.suffix == ".cmake":
            found.append(path)
    return sorted(found)


def citations(root: pathlib.Path) -> tuple[dict[str, int], list[str]]:
    counts, failures = {}, []
    scanned = [(p, PRAGMA) for p in common.source_files(root)] + [(p, FLAG) for p in build_files(root)]
    for path, pattern in scanned:
        lines = path.read_text(errors="replace").splitlines()
        for i, line in enumerate(lines):
            # NOLINTEND closes a NOLINTBEGIN and is not counted: no word boundary follows NOLINT in it.
            if not pattern.search(line):
                continue
            cited = DEV.findall(line) or (DEV.findall(lines[i - 1]) if i > 0 else [])
            where = f"{path.relative_to(root).as_posix()}:{i + 1}"
            if not cited:
                failures.append(f"{where}: a suppression that cites no deviation-log entry (DEV-nnn): {line.strip()}")
            for ident in cited:
                counts[ident] = counts.get(ident, 0) + 1
    return counts, failures


NOT_A_SUPPRESSION = ("\u2014", "-")


def log_entries(log: pathlib.Path) -> dict[str, int | None]:
    """The "Uses" of each entry of the log's table: None for a construct, -1 for no number."""
    entries, uses_column = {}, None
    for line in log.read_text().splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")] if line.strip().startswith("|") else []
        if not cells:
            continue
        lowered = [c.lower() for c in cells]
        if "uses" in lowered:
            uses_column = lowered.index("uses")
            continue
        if uses_column is not None and DEV.fullmatch(cells[0]) and uses_column < len(cells):
            uses = cells[uses_column]
            entries[cells[0]] = None if uses in NOT_A_SUPPRESSION else (int(uses) if uses.isdigit() else -1)
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    ap.add_argument("--log", default="docs/deviations.md", help="the deviation log, relative to the root")
    args = ap.parse_args()
    root = args.root.resolve()
    counts, failures = citations(root)
    log = root / args.log
    entries = log_entries(log) if log.is_file() else {}
    for ident, n in sorted(counts.items()):
        if ident not in entries:
            failures.append(f"{ident}: cited {n} time(s), and the deviation log ({args.log}) has no such entry")
        elif entries[ident] is None:
            failures.append(f"{ident}: cited by {n} suppression(s), and the deviation log says it is not one")
        elif entries[ident] != n:
            failures.append(f"{ident}: cited {n} time(s), and the deviation log says {entries[ident]}")
    for ident, n in sorted(entries.items()):
        if ident not in counts and n is not None and n != 0:
            failures.append(f"{ident}: the deviation log says {n} use(s), and nothing cites it")
    total = sum(counts.values())
    return common.report(failures, f"D31: {total} suppression(s), each a recorded deviation, counted against the log.")


if __name__ == "__main__":
    sys.exit(main())
