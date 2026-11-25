# Copyright 2026 Timothy Place and the catenae contributors
"""What the check scripts share: the files they read and the clang-query runner.

The checks of PLAN.md section 7.3 that a compiler cannot make (D30's prefixes, D40's guards,
raw signed shifts, the suppression count, the @req trace, the length budgets) are scripts in
the tidy job. Each takes --root, the tree it checks (the repository by default), so its
self-test (scripts/tests/) can point it at a fixture tree.
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# Never checked: the sketch is throwaway evidence for PLAN.md, the fixtures are deliberately
# wrong, and build trees are not source.
EXCLUDED = ("docs/sketches/", "scripts/tests/fixtures/", "build", ".git/", "_deps/")

# The directories catenae's own C and C++ code lives in.
CODE_DIRS = ("include", "src", "tests", "examples", "bench", "tools", "platform")
LIBRARY_DIRS = ("include", "src")


def excluded(rel: str) -> bool:
    return any(rel.startswith(prefix) for prefix in EXCLUDED)


def source_files(root: pathlib.Path, dirs=CODE_DIRS, suffixes=(".h", ".c", ".cpp")) -> list[pathlib.Path]:
    """Every file under the given top-level directories with one of the suffixes, sorted."""
    found = []
    for top in dirs:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            rel = path.relative_to(root).as_posix()
            if path.is_file() and path.suffix in suffixes and not excluded(rel):
                found.append(path)
    return sorted(found)


def compile_db(build: pathlib.Path) -> list[str]:
    """The files of a compile database, each once, in database order."""
    db = build / "compile_commands.json"
    if not db.is_file():
        raise SystemExit(f"{db}: no compile database (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)")
    files = [entry["file"] for entry in json.loads(db.read_text())]
    return [f for f in dict.fromkeys(files) if "_deps" not in f]


def path_regex(root: pathlib.Path, dirs) -> str:
    """An llvm::Regex matching files under root's given top-level directories. Specials are
    bracketed, since clang-query's string literals have no escapes."""
    special = set(".^$*+?(){}|[")
    text = "".join(f"[{c}]" if c in special else c for c in root.resolve().as_posix())
    return f"^{text}/({'|'.join(dirs)})/"


def find_clang_query() -> str:
    for name in (os.environ.get("CLANG_QUERY", ""), "clang-query-18", "clang-query"):
        if name and shutil.which(name):
            return name
    raise SystemExit("no clang-query found (clang-tools-18), or set CLANG_QUERY")


NOTE = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): note: "(?P<bind>\w+)" binds here$')


def clang_query(build: pathlib.Path, files: list[str], matchers: list[str]) -> list[tuple[str, int, str, str]]:
    """Runs each matcher over each file; returns (file, line, binding, source line) for every
    binding other than "root", each location once."""
    if not files:
        return []
    args = [find_clang_query(), "-p", str(build)]
    for matcher in matchers:
        args += ["-c", "set output diag", "-c", f"match {matcher}"]
    found = {}
    # A batch at a time keeps the command line short.
    for start in range(0, len(files), 40):
        proc = subprocess.run(args + files[start:start + 40], capture_output=True, text=True)
        if proc.returncode != 0 and "binds here" not in proc.stdout:
            sys.stderr.write(proc.stdout + proc.stderr)
            raise SystemExit("clang-query failed (see above)")
        for line in proc.stdout.splitlines():
            m = NOTE.match(line)
            if m and m.group("bind") != "root":
                key = (m.group("file"), int(m.group("line")), m.group("bind"))
                found.setdefault(key, "")
    results = []
    for (file, line, bind) in sorted(found):
        text = ""
        try:
            text = pathlib.Path(file).read_text(errors="replace").splitlines()[line - 1].strip()
        except (OSError, IndexError):
            pass
        results.append((file, line, bind, text))
    return results


def report(failures: list[str], ok: str) -> int:
    for failure in failures:
        print(failure)
    if failures:
        print(f"{len(failures)} finding(s).")
        return 1
    print(ok)
    return 0
