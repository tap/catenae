/// @file bare_metal_main.cpp
/// @brief The one-shot GoogleTest main of the bare-metal QEMU legs.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Carried from DspTap's tests/bare_metal_main.cpp (NOTICE.md). catenae_add_gtest_executable()
// in tests/CMakeLists.txt links this into every test executable when CATENAE_BARE_METAL is
// set (the four cmake/arm-cortex-*.cmake toolchains). There is no argv on the target, so the
// selection is baked in at build time:
//
//   CATENAE_BARE_METAL_FILTER  the gtest filter (the helper's MAIN_FILTER); unset means "*".
//
// The filter is negative by policy: a suite compiled into a test executable runs on the
// target unless it is excluded by name, each exclusion a written budget decision.
//
// Completion contract, read by CTest (the PASS and FAIL expressions in tests/CMakeLists.txt):
//   CATENAE_TESTS_COMPLETE rc=<n> selected=<n> skipped=<n>
// printed only if the run reached the end, so a crash after gtest's own summary cannot pass.
// rc is 1 when a test failed, when nothing was selected (a filter typo must not pass), and
// when a test was skipped: a skipped gate is not a passed gate. A fault before this line
// prints CATENAE_TESTS_FAULT from platform/cortexm_startup.c and exits.
#include <cstdio>

#include <gtest/gtest.h>

#ifndef CATENAE_BARE_METAL_FILTER
#define CATENAE_BARE_METAL_FILTER "*"
#endif

int main() {
    ::testing::GTEST_FLAG(filter) = CATENAE_BARE_METAL_FILTER;
    ::testing::InitGoogleTest();
    int rc = RUN_ALL_TESTS();
    // gtest applies the filter inside RUN_ALL_TESTS, so the counts are read afterwards.
    const ::testing::UnitTest* const unit     = ::testing::UnitTest::GetInstance();
    const int                        selected = unit->test_to_run_count();
    const int                        skipped  = unit->skipped_test_count();
    if (selected == 0) {
        std::printf("no tests selected: filter \"%s\" is broken\n", CATENAE_BARE_METAL_FILTER);
        rc = 1;
    }
    if (skipped > 0) {
        std::printf("%d test(s) skipped: a skipped gate is a failed gate on the target; "
                    "exclude it in MAIN_FILTER or compile it out\n",
                    skipped);
        rc = 1;
    }
    std::printf("CATENAE_TESTS_COMPLETE rc=%d selected=%d skipped=%d\n", rc, selected, skipped);
    return rc;
}
