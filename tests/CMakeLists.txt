# Copyright 2026 Timothy Place and the catenae contributors
#
# The battery, GoogleTest 1.14 at a pinned commit. Two modes, one registration helper, carried
# from DspTap's tests/CMakeLists.txt (NOTICE.md).
#
# Hosted (default): each test executable links GTest::gtest_main and its tests are registered
# one by one through gtest_discover_tests (under the toolchain's emulator on Hexagon).
#
# CATENAE_BARE_METAL (set by the four cmake/arm-cortex-*.cmake toolchains): one-shot mode for
# Cortex-M under qemu-system-arm. There is no argv on the target and semihosting does not
# reliably propagate exit codes, so each test executable gets tests/bare_metal_main.cpp as its
# main, runs its whole selection in one process, and is registered as ONE CTest test judged on
# the text it prints: PASS on "CATENAE_TESTS_COMPLETE rc=0 ...", FAIL on any gtest
# "[  FAILED  ]" line or on the "CATENAE_TESTS_FAULT" marker platform/cortexm_startup.c
# prints from a fault handler before exiting.
#
#   catenae_add_gtest_executable(<target>
#       SOURCES <src>...
#       [MAIN_FILTER <gtest filter>]
#       [LABELS <label>...])
#
# MAIN_FILTER is a gtest filter applied only in bare-metal mode, default "*": everything
# compiled into the binary runs on the target. It is NEGATIVE by policy ("*-suite.Test"): a
# positive filter would silently drop every suite added later. A skipped test fails the run on
# the target (bare_metal_main.cpp): exclude it by name instead.
include(FetchContent)
include(GoogleTest)

if(NOT CATENAE_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()
if(CATENAE_BARE_METAL)
    # Value-checked feature macros only: GTEST_HAS_DEATH_TEST is tested with #ifdef in gtest
    # 1.14, so it must stay undefined (no GTEST_OS_* is detected on bare metal).
    add_compile_definitions(GTEST_HAS_POSIX_RE=0 GTEST_HAS_STREAM_REDIRECTION=0 GTEST_HAS_FILE_SYSTEM=0)
endif()

# Third-party headers are system headers (PLAN.md section 7.3): SYSTEM keeps the warning sets
# on catenae's code and off GoogleTest's.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    # Commit pin, not the movable tag: DspTap's pin, v1.14.0.
    GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
    SYSTEM)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

function(catenae_add_gtest_executable target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "MAIN_FILTER" "SOURCES;LABELS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "catenae_add_gtest_executable(${target}): unknown arguments '${arg_UNPARSED_ARGUMENTS}'")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "catenae_add_gtest_executable(${target}): SOURCES is required")
    endif()
    add_executable(${target} ${arg_SOURCES})
    # The battery pins the numeric contracts at contraction off (PLAN.md section 4): the
    # compiled core is built so, and the header templates are tested so; a consumer's own
    # flags are its own, and each card says what they can move.
    target_link_libraries(${target} PRIVATE catenae::catenae catenae_test_warnings catenae_contraction)
    # Under an emulator (the QEMU legs, Hexagon) an exhaustive sweep that takes a second natively
    # takes minutes: a test may stride through it there, and says so where it does.
    if(CMAKE_CROSSCOMPILING)
        target_compile_definitions(${target} PRIVATE CATENAE_TEST_EMULATED=1)
    endif()
    if(CATENAE_BARE_METAL)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/bare_metal_main.cpp)
        target_link_libraries(${target} PRIVATE GTest::gtest)
        if(arg_MAIN_FILTER)
            target_compile_definitions(${target} PRIVATE "CATENAE_BARE_METAL_FILTER=\"${arg_MAIN_FILTER}\"")
        endif()
        # CTest prepends CMAKE_CROSSCOMPILING_EMULATOR (the toolchain's qemu line) to a
        # target-name COMMAND. A fault exits in seconds through the marker.
        add_test(NAME ${target}_emulated COMMAND ${target})
        set_tests_properties(${target}_emulated PROPERTIES
            PASS_REGULAR_EXPRESSION "CATENAE_TESTS_COMPLETE rc=0 "
            FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\];CATENAE_TESTS_FAULT"
            TIMEOUT 600)
        if(arg_LABELS)
            set_tests_properties(${target}_emulated PROPERTIES LABELS "${arg_LABELS}")
        endif()
    else()
        target_link_libraries(${target} PRIVATE GTest::gtest_main)
        set(_props TIMEOUT 300)
        if(arg_LABELS)
            list(APPEND _props LABELS "${arg_LABELS}")
        endif()
        gtest_discover_tests(${target} DISCOVERY_TIMEOUT 120 PROPERTIES ${_props})
    endif()
endfunction()

# The battery. Adding a source here puts its suites on every leg, the QEMU legs included; the
# MAIN_FILTER (none yet) is the complete list of what does not run there, each entry a budget
# decision written next to it.
catenae_add_gtest_executable(catenae_tests
    SOURCES
        support/catenae_test/check_trap.cpp
        support/catenae_test/floating_point.cpp
        test_analyzer_traits.cpp
        test_biquad.cpp
        test_biquad_design.cpp
        test_biquad_kernel.cpp
        test_chain.cpp
        test_chain_composition.cpp
        test_check.cpp
        test_compiler.cpp
        test_convert.cpp
        test_correlation.cpp
        test_convert_shift.cpp
        test_delay.cpp
        test_dither.cpp
        test_dynamics.cpp
        test_dynamics_design.cpp
        test_expected.cpp
        test_feeding.cpp
        test_fft.cpp
        test_fft_fir.cpp
        test_fft_fir_design.cpp
        test_fir.cpp
        test_fir_design.cpp
        test_fir_kernel.cpp
        test_fwd.cpp
        test_fwd_alone.cpp
        test_gain.cpp
        test_handoff.cpp
        test_harness.cpp
        test_interleave.cpp
        test_loudness.cpp
        test_junction_traits.cpp
        test_mailbox.cpp
        test_map.cpp
        test_meters.cpp
        test_mix.cpp
        test_oversample.cpp
        test_pack.cpp
        test_parallel.cpp
        test_planar_section.cpp
        test_port.cpp
        test_ppm.cpp
        test_ratio.cpp
        test_resample.cpp
        test_sample_format.cpp
        test_sample_traits.cpp
        test_sanitize.cpp
        test_span.cpp
        test_stage_traits.cpp
        test_steady.cpp
        test_stft.cpp
        test_table_view.cpp
        test_tap.cpp
        test_true_peak.cpp
        test_unpack.cpp
        test_version.cpp
        test_window.cpp
        test_wire.cpp
        # The guide's stage of its own (examples/difference.h): its battery runs with this one.
        ../examples/difference_battery.cpp)
# On a bare-metal leg a timer interrupt is one side of a mailbox and a junction (PLAN.md 7.3).
if(CATENAE_BARE_METAL)
    target_sources(catenae_tests PRIVATE bare_metal/interrupt.cpp)
endif()
# The FFT battery sweeps to 2^20 hosted. Lengths above 65536 are the hosted legs' only: under
# Hexagon's emulator it stops at 65536 (and its clang 19 crashes compiling the long-length test),
# and a bare-metal leg's RAM holds no 65536-point double frame, nor its flash every table, so it
# stops at 4096.
if(CATENAE_BARE_METAL)
    target_compile_definitions(catenae_tests PRIVATE CATENAE_TEST_MAX_FFT_N=4096)
elseif(CMAKE_CROSSCOMPILING)
    target_compile_definitions(catenae_tests PRIVATE CATENAE_TEST_MAX_FFT_N=65536)
endif()
# The battery's support (its check_failed, the generic harness) and the fixture stages.
target_include_directories(catenae_tests PRIVATE support fixtures ../examples)
# The message-passing test needs a second thread: hosted builds that have one.
if(Threads_FOUND)
    target_compile_definitions(catenae_tests PRIVATE CATENAE_TEST_THREADS)
    target_link_libraries(catenae_tests PRIVATE Threads::Threads)
endif()
# test_version.cpp pins include/catenae/version.h to the build's project() version.
target_compile_definitions(catenae_tests PRIVATE
    CATENAE_TEST_PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    CATENAE_TEST_PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    CATENAE_TEST_PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Every stage a template on its sample format, its contract (not its arithmetic) instantiated
# for q31 (D47): an object built on every leg.
add_library(catenae_q31_traits OBJECT q31_traits.cpp)
target_link_libraries(catenae_q31_traits PRIVATE catenae::catenae catenae_test_warnings)

# The table generator (tools/tablegen, PLAN.md 3.3, 12): a host tool, built and checked where its
# designs were printed, on a Linux host with glibc, at contraction off as the battery is: the
# committed tables must be what it writes. Another libm may round the sine otherwise.
if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(catenae_tablegen "${PROJECT_SOURCE_DIR}/tools/tablegen/nyquist_tables.cpp")
    target_link_libraries(catenae_tablegen PRIVATE catenae_test_warnings catenae_contraction)
    add_test(NAME tablegen.TablesAreTheGeneratorsOutput
             COMMAND "${CMAKE_COMMAND}" -DGENERATOR=$<TARGET_FILE:catenae_tablegen>
                     "-DLIBRARY=${PROJECT_SOURCE_DIR}/include/catenae/nyquist_tables.h"
                     "-DTESTS=${CMAKE_CURRENT_SOURCE_DIR}/support/catenae_test/nyquist_test_tables.h"
                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}" -P "${PROJECT_SOURCE_DIR}/tools/tablegen/check.cmake")
    # The FFT FIR's example tables: the library's own make(), so the generator links the core.
    add_executable(catenae_tablegen_fft_fir "${PROJECT_SOURCE_DIR}/tools/tablegen/fft_fir_tables.cpp")
    target_link_libraries(catenae_tablegen_fft_fir PRIVATE catenae::catenae catenae::trap_handler
                                                           catenae_test_warnings catenae_contraction)
    add_test(NAME tablegen.FftFirTablesAreTheGeneratorsOutput
             COMMAND "${CMAKE_COMMAND}" -DGENERATOR=$<TARGET_FILE:catenae_tablegen_fft_fir>
                     "-DCOMMITTED=${PROJECT_SOURCE_DIR}/examples/fft_fir_tables.h"
                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}" -P "${PROJECT_SOURCE_DIR}/tools/tablegen/check_fft_fir.cmake")
endif()

# What must not compile (tests/compile_fail) and what must and must not link (tests/link).
add_subdirectory(compile_fail)
add_subdirectory(link)

# The README's quick start is compiled (PLAN.md section 11.2, rule 7): its first ```cpp block,
# under the test warning set, on every leg.
file(READ "${PROJECT_SOURCE_DIR}/README.md" _catenae_readme)
string(REGEX MATCH "```cpp\n([^`]*)```" _catenae_quick_start "${_catenae_readme}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "README.md has no ```cpp quick start")
endif()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/readme_quick_start.cpp"
     CONTENT "// Generated from README.md's quick start by tests/CMakeLists.txt.\n${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/README.md")
add_library(catenae_readme_quick_start OBJECT "${CMAKE_CURRENT_BINARY_DIR}/readme_quick_start.cpp")
target_link_libraries(catenae_readme_quick_start PRIVATE catenae::catenae catenae_test_warnings)
