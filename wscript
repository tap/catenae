#! /usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""catenae's waf build (D26, PLAN.md section 7.4): the same files as CMakeLists.txt.

    waf configure --std=17 --gtest=/path/to/googletest   # --std 17, 20 or 23
    waf configure --checks=audit ...                      # the check level, boundary by default
    waf build                                             # the library, the checks, the battery
    waf build --alltests                                  # and run every test and example again

It writes catenae/config.h for the check level (tools/config/config.h.in, the template CMake
fills too), builds the compiled core (libcatenae) and the trapping handler, compiles each
public header alone under the compiler's warning set (the consumer's view) and each
compile-fail case's control, and, given a GoogleTest source tree, builds and runs the battery
and the examples. The compile-fail cases themselves and the link tests are CMake's: they are
properties of the headers, checked by the reference build (CLAUDE.md). The warning sets are read from tools/warnings/, their one
home, which CMake reads too. Warnings are errors here, as catenae is the project being built;
--no-werror turns that off. Every build is -fno-exceptions -fno-rtti (D21).
"""
import os

from waflib import Logs, Utils
from waflib.Tools import waf_unit_test

APPNAME = "catenae"
# The version, in one place per build: here and in CMakeLists.txt's project(), both kept equal
# to include/catenae/version.h by the battery (CAT-VERSION-001).
VERSION = "0.0.0"

top = "."
out = "build-waf"


def options(opt):
    opt.load("compiler_cxx waf_unit_test")
    group = opt.add_option_group("catenae")
    group.add_option("--std", action="store", default="17", choices=["17", "20", "23"],
                     help="C++ standard: 17, 20 or 23 (D2: the same files at each) [default: 17]")
    group.add_option("--checks", action="store", default="boundary", choices=["boundary", "audit"],
                     help="the check level written into catenae/config.h (D27) [default: boundary]")
    group.add_option("--no-werror", action="store_true", default=False,
                     help="do not treat warnings as errors")
    group.add_option("--gtest", action="store", default="",
                     help="a GoogleTest source tree (the pinned v1.14.0 commit); without it no tests are built")


def read_flags(ctx, name):
    """One flag per line of tools/warnings/<name>; '#' comments and blank lines are ignored."""
    flags = []
    for line in ctx.path.find_node("tools/warnings/" + name).read().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            flags.append(line)
    return flags


def configure(conf):
    conf.load("compiler_cxx waf_unit_test")
    name = conf.env.CXX_NAME
    std = conf.options.std
    env = conf.env
    env.CATENAE_STD = std
    env.CATENAE_CHECKS = conf.options.checks
    if name == "msvc":
        env.CATENAE_WARNINGS = read_flags(conf, "msvc.txt")
        env.CATENAE_WERROR = ["/WX"]
        env.CATENAE_LIBRARY_ONLY = []
        env.CATENAE_TEST_ONLY = []
        env.append_value("CXXFLAGS", ["/std:c++latest" if std == "23" else "/std:c++" + std,
                                      "/EHs-c-", "/GR-", "/nologo", "/O2", "/MD", "/Zc:__cplusplus"])
        env.append_value("DEFINES", ["_HAS_EXCEPTIONS=0"])
        env.CATENAE_CONTRACTION = ["/fp:precise"]
        env.CATENAE_SECTIONS = []
        env.CATENAE_SYSTEM_INCLUDE = ["/external:W0", "/external:I"]
    elif name in ("gcc", "clang"):
        if name == "gcc":
            env.CATENAE_WARNINGS = read_flags(conf, "gcc.txt")
            env.CATENAE_LIBRARY_ONLY = read_flags(conf, "gcc-library.txt")
            env.CATENAE_TEST_ONLY = []
        else:
            env.CATENAE_WARNINGS = read_flags(conf, "clang.txt")
            env.CATENAE_LIBRARY_ONLY = []
            env.CATENAE_TEST_ONLY = read_flags(conf, "clang-tests.txt")
        env.CATENAE_WERROR = ["-Werror"]
        env.append_value("CXXFLAGS", ["-std=c++" + std, "-O2", "-fno-exceptions", "-fno-rtti"])
        env.CATENAE_CONTRACTION = ["-ffp-contract=off"]
        env.CATENAE_SECTIONS = ["-ffunction-sections", "-fdata-sections"]
        env.CATENAE_SYSTEM_INCLUDE = ["-isystem"]
        if Utils.unversioned_sys_platform() not in ("darwin", "win32"):
            env.append_value("LINKFLAGS", ["-pthread"])
            env.append_value("CXXFLAGS", ["-pthread"])
    else:
        conf.fatal("catenae: no warning set for compiler %r (tools/warnings/)" % name)
    if conf.options.no_werror:
        env.CATENAE_WERROR = []

    env.CATENAE_GTEST = ""
    if conf.options.gtest:
        root = conf.root.find_dir(os.path.abspath(os.path.expanduser(conf.options.gtest)))
        if root is None or root.find_node("googletest/src/gtest-all.cc") is None:
            conf.fatal("--gtest: %s is not a GoogleTest source tree" % conf.options.gtest)
        env.CATENAE_GTEST = root.abspath()
    conf.msg("catenae C++ standard", "C++" + std)
    conf.msg("catenae check level", env.CATENAE_CHECKS)
    conf.msg("catenae battery", env.CATENAE_GTEST or "not built (no --gtest)")


def warnings(env, kind):
    """The flag list for one kind of code, in order (a Clang -Wno- comes after -Weverything)."""
    extra = {"library": env.CATENAE_LIBRARY_ONLY, "test": env.CATENAE_TEST_ONLY, "plain": []}[kind]
    return list(env.CATENAE_WARNINGS) + list(extra) + list(env.CATENAE_WERROR)


def write_config(bld):
    """catenae/config.h for the configured level, from the template CMake fills; its directory."""
    text = bld.path.find_node("tools/config/config.h.in").read()
    text = text.replace("@CATENAE_CHECKS_VALUE@", "CATENAE_CHECKS_" + bld.env.CATENAE_CHECKS.upper())
    text = text.replace("@CATENAE_CONFIG_WRITER@", "waf")
    node = bld.path.find_or_declare("generated/include/catenae/config.h")
    node.parent.mkdir()
    if not os.path.exists(node.abspath()) or node.read() != text:
        node.write(text)
    return node.parent.parent


def build(bld):
    env = bld.env
    include = bld.path.find_dir("include")
    includes = [write_config(bld), include]

    # The compiled core (D4), built with catenae's own contraction policy.
    bld.stlib(
        target="catenae",
        source=bld.path.ant_glob("src/*.cpp"),
        includes=includes,
        export_includes=includes,
        cxxflags=warnings(env, "library") + list(env.CATENAE_CONTRACTION) + list(env.CATENAE_SECTIONS),
    )
    # The default check_failed, which traps (PLAN.md section 3.4): an object, so that an image
    # that defines its own handler never sees this one.
    bld.objects(target="catenae_trap_handler", source=["src/handler/trap.cpp"], includes=includes,
                cxxflags=warnings(env, "library"))

    # A consumer's view: each public header compiled alone, twice, under the whole set.
    checks = []
    for header in sorted(include.ant_glob("catenae/**/*.h"), key=lambda n: n.abspath()):
        rel = header.path_from(include).replace(os.sep, "/")
        node = bld.path.find_or_declare("header_checks/" + rel.replace("/", "_").replace(".", "_") + ".cpp")
        node.parent.mkdir()
        node.write('// Generated by the wscript: %s alone, twice.\n#include "%s"\n#include "%s"\n' % (rel, rel, rel))
        checks.append(node)
    bld.objects(target="catenae_header_checks", source=checks, includes=includes,
                cxxflags=warnings(env, "library"))
    # Each compile-fail case without its defect: the cases are CMake's (tests/compile_fail).
    bld.objects(target="catenae_cf_controls", source=bld.path.ant_glob("tests/compile_fail/*.cpp"),
                includes=includes + ["tests/compile_fail", "tests/fixtures"], defines=["CATENAE_CF_CONTROL"],
                cxxflags=warnings(env, "test"))
    # Each stage's contract instantiated for q31 (D47), as CMake's catenae_q31_traits.
    bld.objects(target="catenae_q31_traits", source=["tests/q31_traits.cpp"], includes=includes,
                cxxflags=warnings(env, "test"))

    if not env.CATENAE_GTEST:
        return

    # GoogleTest, as third-party code: built with no warning set, included as a system header.
    gtest = bld.root.find_dir(env.CATENAE_GTEST).find_dir("googletest")
    system = []
    for directory in (gtest.find_dir("include"),):
        system += list(env.CATENAE_SYSTEM_INCLUDE) + [directory.abspath()]
    bld.stlib(target="catenae_gtest",
              source=[gtest.find_node("src/gtest-all.cc")],
              includes=[gtest.find_dir("include"), gtest],
              export_includes=[gtest.find_dir("include")])
    # main as an object, not inside the library: MSVC's linker infers a program's entry point
    # from the objects it is given and cannot find one in an archive (LNK1561).
    bld.objects(target="catenae_gtest_main", source=[gtest.find_node("src/gtest_main.cc")],
                includes=[gtest.find_dir("include")])

    major, minor, patch = VERSION.split(".")
    bld.program(
        target="catenae_tests",
        features="cxx cxxprogram test",
        source=bld.path.ant_glob("tests/test_*.cpp")
        + ["tests/support/catenae_test/check_trap.cpp", "examples/difference_battery.cpp"],
        includes=includes + ["tests/support", "tests/fixtures", "examples"],
        use=["catenae", "catenae_gtest_main", "catenae_gtest"],
        defines=["CATENAE_TEST_PROJECT_VERSION_MAJOR=" + major, "CATENAE_TEST_PROJECT_VERSION_MINOR=" + minor,
                 "CATENAE_TEST_PROJECT_VERSION_PATCH=" + patch, "CATENAE_TEST_THREADS"],
        # The battery pins the numeric contracts at contraction off, as CMake's does.
        cxxflags=system + warnings(env, "test") + list(env.CATENAE_CONTRACTION),
    )
    # The examples the guide includes, as examples/CMakeLists.txt builds them: each a program
    # that the test step runs, the strawman graph with its nodes' source beside it.
    examples = {
        "print_version": ["examples/print_version.cpp"],
        "stages_and_chains": ["examples/stages_and_chains.cpp"],
        "writing_a_stage": ["examples/writing_a_stage.cpp"],
        "run_time_graph": ["examples/run_time_graph_main.cpp", "examples/run_time_graph.cpp"],
    }
    for name, sources in sorted(examples.items()):
        bld.program(
            target="catenae_example_" + name,
            features="cxx cxxprogram test",
            source=sources,
            includes=includes + ["examples"],
            use=["catenae", "catenae_trap_handler"],
            cxxflags=warnings(env, "test"),
        )
    bld.add_post_fun(waf_unit_test.summary)
    bld.add_post_fun(fail_on_test_failure)


def fail_on_test_failure(bld):
    failed = [entry for entry in getattr(bld, "utest_results", []) if entry[1]]
    if failed:
        for entry in failed:
            Logs.error("catenae: %s failed:\n%s%s" % (entry[0], entry[2].decode("utf-8", "replace"),
                                                       entry[3].decode("utf-8", "replace")))
        bld.fatal("catenae: %d test program(s) failed" % len(failed))
