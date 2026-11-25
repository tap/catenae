#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""D30's prefix rule, which clang-tidy cannot check because it cannot see a constant's access.

    scripts/check_naming.py -p BUILD [--root DIR]

Public constants have no prefix and read as the standard library's traits do (`rate_ratio`,
`is_in_place`); `k_` is for private and protected members and for `detail`. A constant here is
a const-qualified variable with static storage outside any function: at namespace scope, or a
static data member, in catenae's own headers and sources (include/, src/), as every
translation unit of the compile database sees them, template instantiations included. Locals
stay bare (clang-tidy's LocalConstantCase); the case of every name is clang-tidy's.
"""
import argparse
import pathlib
import sys

import checks_common as common

CONSTANT = ('varDecl(hasGlobalStorage(), hasType(isConstQualified()), unless(hasAncestor(functionDecl())), '
            '{access}, {name}, isExpansionInFileMatching("{files}")).bind("{bind}")')
PUBLIC = 'unless(hasAncestor(namespaceDecl(hasName("detail")))), unless(isPrivate()), unless(isProtected())'
PRIVATE = 'anyOf(isPrivate(), isProtected(), hasAncestor(namespaceDecl(hasName("detail"))))'
PREFIXED = 'matchesName("::k_[^:]*$")'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-p", dest="build", required=True, type=pathlib.Path, help="a build with a compile database")
    ap.add_argument("--root", type=pathlib.Path, default=common.REPO)
    args = ap.parse_args()
    files = common.path_regex(args.root, common.LIBRARY_DIRS)
    matchers = [
        CONSTANT.format(access=PUBLIC, name=PREFIXED, files=files, bind="public_constant_with_k_prefix"),
        CONSTANT.format(access=PRIVATE, name=f"unless({PREFIXED})", files=files,
                        bind="private_or_detail_constant_without_k_prefix"),
    ]
    found = common.clang_query(args.build, common.compile_db(args.build), matchers)
    failures = [f"{file}:{line}: {bind.replace('_', ' ')} (D30): {text}" for file, line, bind, text in found]
    return common.report(failures, "D30: every constant's prefix matches its access.")


if __name__ == "__main__":
    sys.exit(main())
