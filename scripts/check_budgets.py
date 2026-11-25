#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The length budgets (PLAN.md section 11.2, rule 4), enforced like the size ceilings.

    scripts/check_budgets.py [--root DIR]

  README.md                       at most 1,000 words
  a guide page (guide/src/*.md)   at most 1,500 words, not counting its included listings
                                  ({{#include ...}} and the other mdBook directives) or any
                                  fenced code; SUMMARY.md, the table of contents, is not a page
  a header comment                at most 60 lines: each run of consecutive `///` lines in a
                                  header of include/ (a card and its requirements, or a
                                  member's comment)

A word is a whitespace-separated token with a letter or a digit in it.
"""
import argparse
import pathlib
import re
import sys

import checks_common as common

README_WORDS = 1000
PAGE_WORDS = 1500
COMMENT_LINES = 60


def words(text: str) -> int:
    return sum(1 for token in text.split() if re.search(r"[A-Za-z0-9]", token))


def prose(markdown: str) -> str:
    """The page without its fenced code and its mdBook directives."""
    kept, fenced = [], False
    for line in markdown.splitlines():
        if line.lstrip().startswith(("```", "~~~")):
            fenced = not fenced
            continue
        if not fenced:
            kept.append(re.sub(r"\{\{#[^}]*\}\}", "", line))
    return "\n".join(kept)


def comment_runs(text: str) -> list[tuple[int, int]]:
    """(first line, length) of each run of consecutive /// lines."""
    runs, start, length = [], None, 0
    for number, line in enumerate(text.splitlines() + [""], 1):
        if line.lstrip().startswith("///"):
            if start is None:
                start, length = number, 0
            length += 1
        elif start is not None:
            runs.append((start, length))
            start = None
    return runs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    args = ap.parse_args()
    root = args.root.resolve()
    failures, measured = [], []

    readme = root / "README.md"
    n = words(readme.read_text())
    measured.append(f"README.md: {n} of {README_WORDS} words")
    if n > README_WORDS:
        failures.append(f"README.md: {n} words, above its budget of {README_WORDS}")

    for page in sorted((root / "guide" / "src").rglob("*.md")):
        if page.name == "SUMMARY.md":
            continue
        rel = page.relative_to(root).as_posix()
        n = words(prose(page.read_text()))
        measured.append(f"{rel}: {n} of {PAGE_WORDS} words")
        if n > PAGE_WORDS:
            failures.append(f"{rel}: {n} words, above a guide page's budget of {PAGE_WORDS}")

    longest = (0, "")
    for header in common.source_files(root, ("include",), (".h",)):
        rel = header.relative_to(root).as_posix()
        for start, length in comment_runs(header.read_text()):
            longest = max(longest, (length, f"{rel}:{start}"))
            if length > COMMENT_LINES:
                failures.append(f"{rel}:{start}: a header comment of {length} lines, above its budget of {COMMENT_LINES}")
    if longest[0]:
        measured.append(f"longest header comment: {longest[0]} of {COMMENT_LINES} lines ({longest[1]})")

    for line in measured:
        print(line)
    return common.report(failures, "Every document is within its budget.")


if __name__ == "__main__":
    sys.exit(main())
