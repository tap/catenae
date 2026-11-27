# Copyright 2026 Timothy Place and the catenae contributors
"""The check scripts' own battery: each refuses what it exists to refuse, and passes what it
must pass. Every tree is written into a temporary directory, so nothing deliberately wrong
is committed where the real checks, the formatter or clang-tidy would see it.

    python3 -m unittest discover -s scripts/tests -v
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

SCRIPTS = pathlib.Path(__file__).resolve().parent.parent
CLANG_QUERY = shutil.which("clang-query-18") or shutil.which("clang-query")
CLANGXX = shutil.which("clang++-18") or shutil.which("clang++")
# On CI a missing tool is a failure, never a skip: a skipped gate is not a passed gate.
HAVE_CLANG = bool(CLANG_QUERY and CLANGXX) or bool(os.environ.get("CI"))


def write(root: pathlib.Path, files: dict[str, str]) -> None:
    for rel, text in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text).lstrip("\n"))


def run(script: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(SCRIPTS / script), *args], capture_output=True, text=True)


class Tree(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._dir.name)

    def tearDown(self):
        self._dir.cleanup()

    def compile_db(self, *sources: str) -> None:
        entries = [{"directory": str(self.root), "file": str(self.root / s),
                    "command": f"{CLANGXX} -std=c++17 -I{self.root / 'include'} -c {self.root / s}"} for s in sources]
        (self.root / "compile_commands.json").write_text(json.dumps(entries))

    def assertRefuses(self, proc, *needles):
        self.assertEqual(proc.returncode, 1, proc.stdout + proc.stderr)
        for needle in needles:
            self.assertIn(needle, proc.stdout)

    def assertPasses(self, proc):
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)


class Guards(Tree):
    GOOD = """
        /// @file good.h
        // Copyright 2026 Timothy Place and the catenae contributors
        #ifndef CATENAE_DETAIL_GOOD_H
        #define CATENAE_DETAIL_GOOD_H
        int f();
        #endif // CATENAE_DETAIL_GOOD_H
        """

    def test_a_guard_spelled_from_the_path_passes(self):
        write(self.root, {"include/catenae/detail/good.h": self.GOOD,
                          "tools/probe/meter.h": "#ifndef CATENAE_TOOLS_PROBE_METER_H\n#define CATENAE_TOOLS_PROBE_METER_H\n#endif // CATENAE_TOOLS_PROBE_METER_H\n"})
        self.assertPasses(run("check_guards.py", "--root", str(self.root)))

    def test_pragma_once_a_misspelt_guard_and_a_bare_endif_are_refused(self):
        write(self.root, {
            "include/catenae/once.h": "#pragma once\nint f();\n",
            "include/catenae/wrong.h": "#ifndef CATENAE_RIGHT_H\n#define CATENAE_RIGHT_H\n#endif // CATENAE_RIGHT_H\n",
            "include/catenae/bare.h": "#ifndef CATENAE_BARE_H\n#define CATENAE_BARE_H\n#endif\n",
            "include/catenae/late.h": "#include <x>\n#ifndef CATENAE_LATE_H\n#define CATENAE_LATE_H\n#endif // CATENAE_LATE_H\n",
        })
        self.assertRefuses(run("check_guards.py", "--root", str(self.root)),
                           "once.h: uses #pragma once", "wrong.h: does not open with #ifndef CATENAE_WRONG_H",
                           "bare.h: does not close with #endif // CATENAE_BARE_H", "late.h: does not open")


@unittest.skipUnless(HAVE_CLANG, "needs clang-query and clang++")
class Naming(Tree):
    def test_d30_prefixes_follow_access(self):
        write(self.root, {"include/catenae/n.h": """
            #ifndef CATENAE_N_H
            #define CATENAE_N_H
            namespace catenae {
                inline constexpr int k_public_bad = 1;
                inline constexpr int public_ok = 2;
                namespace detail {
                    inline constexpr int unprefixed_bad = 3;
                    inline constexpr int k_detail_ok = 4;
                }
                struct s {
                    static constexpr int rate_ok = 1;
                    static constexpr int k_member_bad = 2;
                  private:
                    static constexpr int k_private_ok = 3;
                    static constexpr int private_bad = 4;
                };
                inline int f() { static constexpr int k_local_ok = 5; constexpr int local_ok = 6; return k_local_ok + local_ok; }
            }
            #endif // CATENAE_N_H
            """, "src/n.cpp": '#include "catenae/n.h"\nint g() { return catenae::f(); }\n'})
        self.compile_db("src/n.cpp")
        proc = run("check_naming.py", "-p", str(self.root), "--root", str(self.root))
        self.assertRefuses(proc, "k_public_bad", "unprefixed_bad", "k_member_bad", "private_bad")
        for fine in ("public_ok", "k_detail_ok", "rate_ok", "k_private_ok", "k_local_ok", "local_ok = 6"):
            self.assertNotIn(fine, proc.stdout)
        self.assertIn("4 finding(s)", proc.stdout)


@unittest.skipUnless(HAVE_CLANG, "needs clang-query and clang++")
class Shifts(Tree):
    def test_signed_shifts_are_refused_after_promotion_and_instantiation(self):
        write(self.root, {"include/catenae/s.h": """
            #ifndef CATENAE_S_H
            #define CATENAE_S_H
            #include <cstdint>
            namespace catenae {
                inline int a(std::int16_t x) { return x << 2; }
                inline std::uint32_t b(std::uint32_t x) { return x >> 3; }
                inline int c(std::uint16_t x) { return x << 1; }
                template <typename T> T d(T x) { return x >> 1; }
                inline void e(int& x) { x <<= 1; }
            }
            #endif // CATENAE_S_H
            """, "src/s.cpp": '#include "catenae/s.h"\nunsigned f() { return catenae::d(4u) + catenae::b(8u); }\n'
                              'int g() { return catenae::d(4); }\n'})
        self.compile_db("src/s.cpp")
        proc = run("check_shifts.py", "-p", str(self.root), "--root", str(self.root))
        self.assertRefuses(proc, "s.h:5:", "s.h:7:", "s.h:8:", "s.h:9:", "4 finding(s)")
        self.assertNotIn("s.h:6:", proc.stdout)


class Suppressions(Tree):
    def test_an_uncited_pragma_nolint_and_flag_are_refused(self):
        write(self.root, {
            "src/a.cpp": '#pragma GCC diagnostic ignored "-Wconversion"\nint x; // NOLINT\n',
            "tests/b.cpp": "// NOLINTNEXTLINE(readability-identifier-naming)\nint Y;\n// NOLINTEND\n",
            "cmake/t.cmake": 'set(CMAKE_CXX_FLAGS_INIT "-Wno-psabi")\n',
            "tools/warnings/gcc.txt": "-Wno-padded\n",
        })
        proc = run("check_suppressions.py", "--root", str(self.root))
        self.assertRefuses(proc, "src/a.cpp:1:", "src/a.cpp:2:", "tests/b.cpp:1:", "cmake/t.cmake:1:", "4 finding(s)")
        self.assertNotIn("gcc.txt", proc.stdout)

    def test_citations_are_counted_against_the_log(self):
        write(self.root, {
            "src/a.cpp": "// DEV-001: the reason is in the log\n#pragma clang diagnostic ignored \"-Wpadded\"\n"
                         "int x; // NOLINT(readability-x) DEV-001\n",
            "docs/deviations.md": "| Entry | What | Uses |\n|---|---|---|\n| DEV-001 | padding | 2 |\n| DEV-002 | spare | 0 |\n",
        })
        self.assertPasses(run("check_suppressions.py", "--root", str(self.root)))
        (self.root / "docs/deviations.md").write_text("| Entry | What | Uses |\n|---|---|---|\n| DEV-001 | padding | 3 |\n")
        self.assertRefuses(run("check_suppressions.py", "--root", str(self.root)), "DEV-001: cited 2 time(s), and the deviation log says 3")
        (self.root / "docs/deviations.md").write_text(
            "| Entry | What | Uses |\n|---|---|---|\n| DEV-001 | padding | \u2014 |\n| DEV-002 | a construct | \u2014 |\n")
        self.assertRefuses(run("check_suppressions.py", "--root", str(self.root)),
                           "DEV-001: cited by 2 suppression(s), and the deviation log says it is not one", "1 finding(s)")
        (self.root / "docs/deviations.md").unlink()
        self.assertRefuses(run("check_suppressions.py", "--root", str(self.root)), "has no such entry")


class Requirements(Tree):
    HEADER = "/// @req{CAT-AREA-001} one.\n/// @req{CAT-AREA-002} two.\nint f();\n"

    def test_a_complete_trace_passes(self):
        write(self.root, {"include/catenae/r.h": self.HEADER,
                          "tests/t.cpp": "// Verifies CAT-AREA-001 and CAT-AREA-002.\nTEST(r, Both) {}\n"})
        self.assertPasses(run("check_requirements.py", "--root", str(self.root)))

    def test_every_break_in_the_trace_is_refused(self):
        write(self.root, {"include/catenae/r.h": self.HEADER + "/// @req{CAT-AREA-001} again.\n",
                          "tests/t.cpp": "// Verifies CAT-AREA-001.\nTEST(r, One) {}\n\nTEST(r, None) {}\n"
                                         "// Verifies CAT-AREA-009.\nTYPED_TEST(r, Dangling) {}\n"})
        self.assertRefuses(run("check_requirements.py", "--root", str(self.root)),
                           "CAT-AREA-001 is declared again", "CAT-AREA-002 is cited by no test",
                           "r.None cites no requirement", "r.Dangling cites CAT-AREA-009, which nothing declares")


    def test_contract_pages_declare_and_whole_files_cite(self):
        write(self.root, {"include/catenae/r.h": "/// @req{CAT-AREA-001} one.\nint f();\n",
                          "docs/contracts/area.md": "# Area\n\n@req{CAT-AREA-002} two.\n",
                          "tests/t.cpp": "// Verifies CAT-AREA-001.\nTEST(r, One) {}\n",
                          "tests/compile_fail/two.cpp": "// A case.\n// Verifies CAT-AREA-002.\n#include \"x.h\"\n",
                          "tests/compile_fail/cf.h": "// A template, not a case.\n"})
        self.assertPasses(run("check_requirements.py", "--root", str(self.root)))
        write(self.root, {"tests/link/probe.cpp": "// A probe.\nint main() {}\n// Verifies CAT-AREA-001.\n"})
        self.assertRefuses(run("check_requirements.py", "--root", str(self.root)),
                           "link.probe cites no requirement", "1 finding(s)")


class Budgets(Tree):
    def test_each_budget_is_enforced(self):
        write(self.root, {"README.md": "word " * 1000,
                          "guide/src/SUMMARY.md": "word " * 5000,
                          "guide/src/page.md": "word " * 1500 + "\n{{#include ../../examples/x.cpp}}\n```cpp\n" + "code " * 900 + "\n```\n",
                          "include/catenae/h.h": "/// line\n" * 60})
        self.assertPasses(run("check_budgets.py", "--root", str(self.root)))
        write(self.root, {"README.md": "word " * 1001, "guide/src/page.md": "word " * 1501,
                          "include/catenae/h.h": "/// line\n" * 61})
        self.assertRefuses(run("check_budgets.py", "--root", str(self.root)),
                           "README.md: 1001 words", "guide/src/page.md: 1501 words", "include/catenae/h.h:1: a header comment of 61 lines")


class Slots(Tree):
    def test_an_assignment_into_a_slot_is_refused(self):
        write(self.root, {"include/catenae/box.h":
                          "::new (static_cast<void*>(m_slots[m_poster].bytes)) T(value);\n"
                          "apply(*std::launder(static_cast<const T*>(static_cast<const void*>(m_slots[i].bytes))));\n"
                          "// m_slots[0] = v;  a comment is not code\n"
                          "if (m_slots[0].bytes[0] == 1) {}\n"})
        self.assertPasses(run("check_slots.py", "--root", str(self.root)))
        write(self.root, {"include/catenae/box.h": "m_slots[m_poster] = value;\n"
                                                   "std::memcpy(m_slots[1].bytes, &value, sizeof value);\n"})
        self.assertRefuses(run("check_slots.py", "--root", str(self.root)),
                           "box.h:1: an assignment into a mailbox slot", "box.h:2: a copy into a mailbox slot")


class SteadyFigure(unittest.TestCase):
    TRACE = ("call,frames_in,bare_out,fifo_held,steady_out\n"
             "1,64,0,63,64\n2,64,128,127,64\n"
             "CATENAE_EXAMPLE_DONE ok=1\n")

    def test_draws_a_lane_a_column_and_a_bar_a_call(self):
        with tempfile.TemporaryDirectory() as tmp:
            trace = pathlib.Path(tmp) / "trace.txt"
            trace.write_text(self.TRACE)
            done = run("figure_steady.py", str(trace))
            self.assertEqual(done.returncode, 0, done.stderr)
            svg = done.stdout
            self.assertEqual(svg.count('class="title"'), 4)
            self.assertEqual(svg.count("call 1:"), 4)  # one bar, or a mark for none, in each lane
            self.assertEqual(svg.count('class="axis"'), 2)
            self.assertIn("holds 63 or 127 frames", svg)
            self.assertIn("@media (prefers-color-scheme: dark)", svg)

    def test_is_the_same_drawing_every_time(self):
        with tempfile.TemporaryDirectory() as tmp:
            trace = pathlib.Path(tmp) / "trace.txt"
            trace.write_text(self.TRACE)
            self.assertEqual(run("figure_steady.py", str(trace)).stdout, run("figure_steady.py", str(trace)).stdout)


class PretendArgs(unittest.TestCase):
    """GCC's static frame on Arm leaves out the area a prologue reserves before its register
    save; the stack bound adds it back from the disassembly."""
    DISASSEMBLY = textwrap.dedent("""
        10000000 <split_span>:
        10000000:	sub	sp, #8
        10000002:	stmdb	sp!, {r4, r5, r6, lr}
        10000006:	sub	sp, #20
        10000100 <variadic>:
        10000100:	push	{r1, r2, r3}
        10000102:	push	{r4, lr}
        10000200 <plain>:
        10000200:	push	{r4, lr}
        10000202:	sub	sp, #16
        10000300 <leaf>:
        10000300:	sub	sp, #8
        10000302:	add	sp, #8
        10000304:	bx	lr
    """)

    def test_the_area_before_the_save_is_counted_and_nothing_else(self):
        sys.path.insert(0, str(SCRIPTS))
        import ceilings
        from unittest import mock
        done = subprocess.CompletedProcess([], 0, stdout=self.DISASSEMBLY, stderr="")
        with mock.patch.object(ceilings.subprocess, "run", return_value=done):
            area = ceilings.pretend_args("image")
        self.assertEqual(area, {"split_span": 8, "variadic": 12})


if __name__ == "__main__":
    unittest.main()
