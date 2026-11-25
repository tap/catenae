#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""No raw shift of a signed value in the library (PLAN.md section 7.3).

    scripts/check_shifts.py -p BUILD [--root DIR]

In C++17 a right shift of a negative value is implementation-defined and a left shift of one
is undefined, so every shift of a signed value goes through one helper and this rejects a raw
one anywhere else in catenae's own headers and sources (include/, src/). The check is on the
shift's type after the usual promotions, as every translation unit of the compile database
sees it, template instantiations included: `std::uint16_t << 2` is a shift of an `int`, and is
rejected too. An unsigned shift is not.
"""
import argparse
import pathlib
import sys

import checks_common as common

# The one helper's header, relative to the root, once it exists: its own shifts are the
# raw ones it wraps. It arrives with the first code that shifts a signed value.
HELPERS: list[str] = []

SHIFT = ('binaryOperator(hasAnyOperatorName("<<", ">>", "<<=", ">>="), hasType(isSignedInteger()), '
         'isExpansionInFileMatching("{files}")).bind("raw_signed_shift")')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-p", dest="build", required=True, type=pathlib.Path, help="a build with a compile database")
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    args = ap.parse_args()
    found = common.clang_query(args.build, common.compile_db(args.build),
                               [SHIFT.format(files=common.path_regex(args.root, common.LIBRARY_DIRS))])
    root = args.root.resolve()
    failures = []
    for file, line, _, text in found:
        rel = pathlib.Path(file).resolve().relative_to(root).as_posix()
        if rel not in HELPERS:
            failures.append(f"{file}:{line}: a raw shift of a signed value; use the shift helper: {text}")
    return common.report(failures, "No raw shift of a signed value in the library.")


if __name__ == "__main__":
    sys.exit(main())
