#! /usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""catenae's waf build (D26, PLAN.md section 7.4): the same files as CMakeLists.txt.

    waf configure --std=17 --gtest=/path/to/googletest   # --std 17, 20 or 23
    waf build                                             # the library, the checks, the battery
    waf build --alltests                                  # and run every test and example again

It builds the compiled core (libcatenae), compiles each public header alone under the
compiler's warning set (the consumer's view), and, given a GoogleTest source tree, builds and
runs the battery and the examples. The warning sets are read from tools/warnings/, their one
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
    conf.msg("catenae battery", env.CATENAE_GTEST or "not built (no --gtest)")


def warnings(env, kind):
    """The flag list for one kind of code, in order (a Clang -Wno- comes after -Weverything)."""
    extra = {"library": env.CATENAE_LIBRARY_ONLY, "test": env.CATENAE_TEST_ONLY, "plain": []}[kind]
    return list(env.CATENAE_WARNINGS) + list(extra) + list(env.CATENAE_WERROR)


def build(bld):
    env = bld.env
    include = bld.path.find_dir("include")

    # The compiled core (D4), built with catenae's own contraction policy.
    bld.stlib(
        target="catenae",
        source=bld.path.ant_glob("src/*.cpp"),
        includes=["include"],
        export_includes=["include"],
        cxxflags=warnings(env, "library") + list(env.CATENAE_CONTRACTION) + list(env.CATENAE_SECTIONS),
    )

    # A consumer's view: each public header compiled alone, twice, under the whole set.
    checks = []
    for header in sorted(include.ant_glob("catenae/**/*.h"), key=lambda n: n.abspath()):
        rel = header.path_from(include).replace(os.sep, "/")
        node = bld.path.find_or_declare("header_checks/" + rel.replace("/", "_").replace(".", "_") + ".cpp")
        node.parent.mkdir()
        node.write('// Generated by the wscript: %s alone, twice.\n#include "%s"\n#include "%s"\n' % (rel, rel, rel))
        checks.append(node)
    bld.objects(target="catenae_header_checks", source=checks, includes=["include"],
                cxxflags=warnings(env, "library"))

    if not env.CATENAE_GTEST:
        return

    # GoogleTest, as third-party code: built with no warning set, included as a system header.
    gtest = bld.root.find_dir(env.CATENAE_GTEST).find_dir("googletest")
    system = []
    for directory in (gtest.find_dir("include"),):
        system += list(env.CATENAE_SYSTEM_INCLUDE) + [directory.abspath()]
    bld.stlib(target="catenae_gtest",
              source=[gtest.find_node("src/gtest-all.cc"), gtest.find_node("src/gtest_main.cc")],
              includes=[gtest.find_dir("include"), gtest],
              export_includes=[gtest.find_dir("include")])

    major, minor, patch = VERSION.split(".")
    bld.program(
        target="catenae_tests",
        features="cxx cxxprogram test",
        source=bld.path.ant_glob("tests/test_*.cpp"),
        includes=["include"],
        use=["catenae", "catenae_gtest"],
        defines=["CATENAE_TEST_PROJECT_VERSION_MAJOR=" + major, "CATENAE_TEST_PROJECT_VERSION_MINOR=" + minor,
                 "CATENAE_TEST_PROJECT_VERSION_PATCH=" + patch],
        cxxflags=system + warnings(env, "test"),
    )
    for example in bld.path.ant_glob("examples/*.cpp"):
        bld.program(
            target="catenae_example_" + os.path.splitext(example.name)[0],
            features="cxx cxxprogram test",
            source=[example],
            includes=["include"],
            use=["catenae"],
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
