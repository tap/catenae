#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The mailbox's lifetime rule (CAT-MAILBOX-003, DEV-013, PLAN.md section 7.3).

    scripts/check_slots.py [--root DIR]

A post begins its value's lifetime with placement new in the poster's slot; an assignment into
a slot would not begin it for a T with no trivial default constructor, which a design is, and no
sanitizer sees the difference. So every header of include/ is read for a mailbox's slots
(`m_slots`), and an assignment into one, or a memcpy, memmove or std::copy into one, fails.
"""
import argparse
import pathlib
import re
import sys

import checks_common as common

# An assignment to a slot, an element of one, or a member of one: `m_slots[i] = v`,
# `m_slots[i].bytes[0] = b`, `m_slots = other`; not a comparison.
ASSIGN = re.compile(r"\bm_slots\s*(?:\[[^\]]*\]\s*)?(?:\.\s*\w+\s*)?(?:\[[^\]]*\]\s*)?(?:[-+*/|&^]?=)(?!=)")
# A copy into a slot by a library routine.
COPY = re.compile(r"\b(?:std::)?(?:memcpy|memmove|copy|copy_n)\s*\([^;]*\bm_slots\b")


def check(text: str, rel: str) -> list[str]:
    problems = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("//", 1)[0]
        if "m_slots" not in line:
            continue
        if ASSIGN.search(line):
            problems.append(f"{rel}:{number}: an assignment into a mailbox slot; a post constructs with placement new")
        elif COPY.search(line):
            problems.append(f"{rel}:{number}: a copy into a mailbox slot; a post constructs with placement new")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    args = ap.parse_args()
    root = args.root.resolve()
    headers = [p for p in common.source_files(root, suffixes=(".h",))
               if p.relative_to(root).as_posix().startswith("include/")]
    failures: list[str] = []
    slots = 0
    for path in headers:
        text = path.read_text(encoding="utf-8")
        slots += text.count("m_slots")
        failures += check(text, path.relative_to(root).as_posix())
    return common.report(failures, f"CAT-MAILBOX-003: {slots} use(s) of a mailbox's slots, none an assignment.")


if __name__ == "__main__":
    sys.exit(main())
