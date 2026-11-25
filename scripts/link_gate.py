#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The link-stage gate (D36, PLAN.md section 7.3): no software double in a float image.

    scripts/link_gate.py [--nm TOOL] [--expect-refusal] IMAGE...

-Wdouble-promotion catches a stray double at compile time but cannot see an explicit cast;
this sees what the linker kept. It runs on named probe images, one per float stage family,
each an audio path built from table designs only (the first is C1's): the linked,
garbage-collected image may contain none of the listed software-double and double-libm
symbols below. An image that links a designer is outside the gate by definition, and since
the designers are in the compiled core, linking one is itself visible as a symbol.

The list is per ABI: the Arm EABI's run-time helpers (__aeabi_d*, the conversions to and from
double), libgcc's generic soft-double routines, and the double functions of the C math library
with newlib's internals beneath them. --expect-refusal inverts the verdict: the gate's own
test on a fixture that must be refused (bench/link_gate/).
"""
import argparse
import re
import shutil
import subprocess
import sys

# The Arm EABI's soft-double helpers and conversions (Run-time ABI for the Arm Architecture).
AEABI = {
    "__aeabi_dadd", "__aeabi_dsub", "__aeabi_drsub", "__aeabi_dmul", "__aeabi_ddiv", "__aeabi_dneg",
    "__aeabi_dcmpeq", "__aeabi_dcmplt", "__aeabi_dcmple", "__aeabi_dcmpge", "__aeabi_dcmpgt", "__aeabi_dcmpun",
    "__aeabi_cdcmpeq", "__aeabi_cdcmple", "__aeabi_cdrcmple",
    "__aeabi_d2f", "__aeabi_f2d", "__aeabi_d2iz", "__aeabi_d2uiz", "__aeabi_d2lz", "__aeabi_d2ulz",
    "__aeabi_i2d", "__aeabi_ui2d", "__aeabi_l2d", "__aeabi_ul2d",
}
# libgcc's generic soft-double routines (any target without double hardware).
LIBGCC = {
    "__adddf3", "__subdf3", "__muldf3", "__divdf3", "__negdf2", "__extendsfdf2", "__truncdfsf2",
    "__floatsidf", "__floatunsidf", "__floatdidf", "__floatundidf", "__fixdfsi", "__fixunsdfsi",
    "__fixdfdi", "__fixunsdfdi", "__eqdf2", "__nedf2", "__gedf2", "__gtdf2", "__ledf2", "__ltdf2",
    "__unorddf2", "__cmpdf2",
}
# The double functions of <cmath> (their float forms end in f and are allowed).
LIBM = {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
    "exp", "exp2", "expm1", "log", "log2", "log10", "log1p", "logb", "pow", "sqrt", "cbrt", "hypot",
    "floor", "ceil", "trunc", "round", "lround", "llround", "rint", "lrint", "llrint", "nearbyint",
    "fmod", "remainder", "remquo", "fma", "fmax", "fmin", "fdim", "ldexp", "frexp", "scalbn", "scalbln",
    "modf", "copysign", "nextafter", "erf", "erfc", "tgamma", "lgamma", "sincos",
}
# newlib's double internals beneath them (their float forms end in f).
INTERNAL = re.compile(r"^__(ieee754|kernel)_[a-z0-9_]+$")


def refused(name: str) -> bool:
    if name in AEABI or name in LIBGCC or name in LIBM:
        return True
    return bool(INTERNAL.match(name)) and not name.endswith("f")


def symbols(nm: str, image: str) -> set[str]:
    out = subprocess.run([nm, image], check=True, capture_output=True, text=True).stdout
    return {parts[-1] for parts in (line.split() for line in out.splitlines()) if len(parts) >= 2}


def find_nm(explicit: str | None) -> str:
    for name in (explicit, "arm-none-eabi-nm", "llvm-nm", "nm"):
        if name and shutil.which(name):
            return name
    raise SystemExit("no nm found")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("images", nargs="+")
    ap.add_argument("--nm", help="the nm of the image's toolchain (default: arm-none-eabi-nm)")
    ap.add_argument("--expect-refusal", action="store_true", help="pass only if every image is refused")
    args = ap.parse_args()
    nm = find_nm(args.nm)
    verdicts = []
    for image in args.images:
        found = sorted(s for s in symbols(nm, image) if refused(s))
        if found:
            print(f"{image}: REFUSED, {len(found)} software-double or double-libm symbol(s): {', '.join(found)}")
        else:
            print(f"{image}: passes, no software-double or double-libm symbol")
        verdicts.append(bool(found))
    if args.expect_refusal:
        return 0 if all(verdicts) else 1
    return 1 if any(verdicts) else 0


if __name__ == "__main__":
    sys.exit(main())
