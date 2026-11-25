# Copyright 2026 Timothy Place and the catenae contributors
#
# The harness's binaries (CATENAE_BUILD_BENCH; bench/README.md has the policy):
#
#   catenae_icount_<scenario>   the instruction-count ratchet's workloads, one binary per
#                               scenario (bare-metal targets have no argv), counted under QEMU
#                               or callgrind by scripts/icount.py against baselines.json
#   catenae_probe_<set>         the size probes, linked with a map (scripts/ceilings.py)
#   catenae_stack_<set>         the stack probes (scripts/ceilings.py)
#   catenae_gate_<fixture>      the link-stage gate's own fixtures, on the Arm legs
#                               (scripts/link_gate.py)
#
# "Host-class" is decided by CMAKE_SYSTEM_NAME (Generic is bare metal), never by
# CMAKE_CROSSCOMPILING: Hexagon is a cross build of a hosted target.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(_catenae_bare_metal ON)
else()
    set(_catenae_bare_metal OFF)
endif()
# GCC writes each function's frame and the call graph it compiled; the stack probes' static
# worst path reads them (bare-metal GCC only: clang has no -fcallgraph-info, and a hosted GCC's
# compile database feeds clang-tidy, whose clang front end rejects the flag).
if(_catenae_bare_metal AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_catenae_stack_info -fstack-usage -fcallgraph-info=su)
    target_compile_options(catenae PRIVATE ${_catenae_stack_info})
endif()

add_library(catenae_bench_support INTERFACE)
target_include_directories(catenae_bench_support INTERFACE "${PROJECT_SOURCE_DIR}/tools/probe")
target_link_libraries(catenae_bench_support INTERFACE catenae::catenae catenae_test_warnings)

# --- The ratchet's workloads -------------------------------------------------------------
function(catenae_add_icount scenario source)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "DEFINITIONS")
    set(_target catenae_icount_${scenario})
    add_executable(${_target} ${source})
    target_compile_definitions(${_target} PRIVATE ${arg_DEFINITIONS})
    target_link_libraries(${_target} PRIVATE catenae_bench_support)
    # Off the bare-metal legs, CTest runs each workload twice and compares the DONE lines
    # (bench/icount/smoke.cmake); the counts are the ratchet job's.
    if(NOT _catenae_bare_metal)
        add_test(NAME bench.icount.${scenario}
                 COMMAND ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:${_target}>
                         "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" -P ${CMAKE_CURRENT_SOURCE_DIR}/icount/smoke.cmake)
        set_tests_properties(bench.icount.${scenario} PROPERTIES LABELS bench)
    endif()
endfunction()

catenae_add_icount(version icount/version_main.cpp DEFINITIONS CATENAE_ICOUNT_BRACKET=0)
catenae_add_icount(callmax_version icount/version_main.cpp DEFINITIONS CATENAE_ICOUNT_BRACKET=1)

# --- The size and stack probes -----------------------------------------------------------
function(catenae_add_probe set source)
    set(_size catenae_probe_${set})
    add_executable(${_size} ${source})
    target_compile_definitions(${_size} PRIVATE CATENAE_PROBE_STACK=0)
    target_link_libraries(${_size} PRIVATE catenae_bench_support)
    target_link_options(${_size} PRIVATE "LINKER:-Map=$<TARGET_FILE:${_size}>.map")

    set(_stack catenae_stack_${set})
    add_executable(${_stack} ${source})
    target_compile_definitions(${_stack} PRIVATE CATENAE_PROBE_STACK=1)
    target_compile_options(${_stack} PRIVATE ${_catenae_stack_info})
    target_link_libraries(${_stack} PRIVATE catenae_bench_support)
endfunction()

catenae_add_probe(version probe/version_probe.cpp)

# --- The link-stage gate's own test (D36) ------------------------------------------------
# The gate itself is scripts/link_gate.py over a named probe image; the first probe is C1's.
# Here it proves itself on two fixtures on every Arm leg: an image of float arithmetic passes,
# and one with a single product in double is refused where double is software (the M4, the
# M4F, the M33). Where the FPU has double precision (GCC's default for -mcpu=cortex-m55) a
# double product is an instruction and no routine, so the gate cannot see it: that limit is
# pinned as a test of its own rather than hidden.
if(_catenae_bare_metal AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
#if !(defined(__ARM_FP) && (__ARM_FP & 8))
#error double is software here
#endif
int main() { return 0; }" CATENAE_HARDWARE_DOUBLE)
    foreach(_fixture IN ITEMS float_only with_double)
        add_executable(catenae_gate_${_fixture} link_gate/${_fixture}.cpp)
        target_link_libraries(catenae_gate_${_fixture} PRIVATE catenae_test_warnings)
    endforeach()
    add_test(NAME link_gate.FloatImagePasses
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_gate_float_only>)
    if(CATENAE_HARDWARE_DOUBLE)
        add_test(NAME link_gate.HardwareDoubleIsInvisible
                 COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                         $<TARGET_FILE:catenae_gate_with_double>)
    else()
        add_test(NAME link_gate.SoftwareDoubleIsRefused
                 COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py --expect-refusal
                         $<TARGET_FILE:catenae_gate_with_double>)
    endif()
    get_property(_gate_tests DIRECTORY PROPERTY TESTS)
    list(FILTER _gate_tests INCLUDE REGEX "^link_gate[.]")
    set_tests_properties(${_gate_tests} PROPERTIES LABELS link_gate)
endif()
