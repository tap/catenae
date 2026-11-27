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
#   catenae_layout_<scenario>   the layout benchmark's workloads, counted but never gated
#                               (scripts/icount.py --prefix catenae_layout_ --report-only)
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

# The guide's "Stages and chains": the strip of examples/strip.h as a chain, and as virtual
# nodes joined at run time (examples/run_time_graph.h), the strawman built here from its source.
catenae_add_icount(strip_chain icount/strip_main.cpp DEFINITIONS CATENAE_STRIP_GRAPH=0)
catenae_add_icount(strip_graph icount/strip_main.cpp DEFINITIONS CATENAE_STRIP_GRAPH=1)
foreach(_strip IN ITEMS strip_chain strip_graph)
    target_sources(catenae_icount_${_strip} PRIVATE "${PROJECT_SOURCE_DIR}/examples/run_time_graph.cpp")
    target_include_directories(catenae_icount_${_strip} PRIVATE "${PROJECT_SOURCE_DIR}/examples")
endforeach()
if(NOT _catenae_bare_metal)
    # The same output, bit for bit: the two print one checksum.
    add_test(NAME bench.strip_chain_and_graph_agree
             COMMAND ${CMAKE_COMMAND} -DFIRST=$<TARGET_FILE:catenae_icount_strip_chain>
                     -DSECOND=$<TARGET_FILE:catenae_icount_strip_graph> "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/icount/agree.cmake)
    set_tests_properties(bench.strip_chain_and_graph_agree PROPERTIES LABELS bench)
endif()

# C2's edges: 24-bit packed stereo off the wire and back, the c2 probe's path.
catenae_add_icount(edges icount/edges_main.cpp)
# The floating FFT (C4): a forward and an inverse over 2^20 samples, DspTap's rfft scenarios on
# catenae's plan; and the worst single transform.
catenae_add_icount(fft_f32_512 icount/fft_main.cpp DEFINITIONS CATENAE_FFT_T=float CATENAE_FFT_N=512)
catenae_add_icount(fft_f32_2048 icount/fft_main.cpp DEFINITIONS CATENAE_FFT_T=float CATENAE_FFT_N=2048)
catenae_add_icount(fft_f64_512 icount/fft_main.cpp DEFINITIONS CATENAE_FFT_T=double CATENAE_FFT_N=512)
catenae_add_icount(callmax_fft_f32_512 icount/fft_main.cpp
                   DEFINITIONS CATENAE_FFT_T=float CATENAE_FFT_N=512 CATENAE_ICOUNT_BRACKET=1)
catenae_add_icount(recursive icount/recursive_main.cpp)
target_include_directories(catenae_icount_recursive PRIVATE "${PROJECT_SOURCE_DIR}/examples")
# What one recursive make() costs, the worst single call (QEMU keys only): its card's numbers.
catenae_add_icount(callmax_design_cookbook icount/design_main.cpp DEFINITIONS CATENAE_DESIGN_SECTIONS=0)
foreach(_order IN ITEMS 2 4 8 16)
    math(EXPR _sections "${_order} / 2")
    catenae_add_icount(callmax_design_butterworth_${_order} icount/design_main.cpp
                       DEFINITIONS CATENAE_DESIGN_SECTIONS=${_sections})
endforeach()

# The FIR family (C3): the 63-tap low-pass of examples/fir_tables.h in float, double, Q15 and
# Q31 (the guide's "What arithmetic costs"); and what one FIR make() costs, the worst single
# call (QEMU keys only): its card's numbers.
foreach(_format IN ITEMS f32:0 f64:1 q15:2 q31:3)
    string(REPLACE ":" ";" _parts "${_format}")
    list(GET _parts 0 _fname)
    list(GET _parts 1 _fid)
    catenae_add_icount(fir_${_fname} icount/fir_main.cpp DEFINITIONS CATENAE_FIR_FORMAT=${_fid})
    target_include_directories(catenae_icount_fir_${_fname} PRIVATE "${PROJECT_SOURCE_DIR}/examples")
endforeach()
foreach(_design IN ITEMS f32_63:0 q15_255:1 f32_1023:2)
    string(REPLACE ":" ";" _parts "${_design}")
    list(GET _parts 0 _dname)
    list(GET _parts 1 _did)
    catenae_add_icount(callmax_design_fir_${_dname} icount/fir_design_main.cpp DEFINITIONS CATENAE_FIR_DESIGN=${_did})
endforeach()

# The rate family (C3): PLAN.md 9.4's four-times overdrive (examples/overdrive.h), and up2 and
# down2 alone in float, Q15 and Q31.
foreach(_kind IN ITEMS overdrive:0 up2_f32:1 down2_f32:2 up2_q15:3 down2_q15:4 up2_q31:5 down2_q31:6)
    string(REPLACE ":" ";" _parts "${_kind}")
    list(GET _parts 0 _kname)
    list(GET _parts 1 _kid)
    catenae_add_icount(${_kname} icount/resample_main.cpp DEFINITIONS CATENAE_RESAMPLE_KIND=${_kid})
    target_include_directories(catenae_icount_${_kname} PRIVATE "${PROJECT_SOURCE_DIR}/examples")
endforeach()

# --- The layout benchmark: interleaved against planar, informational (bench/layout/) --------
add_subdirectory(layout)

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
# C1's: the example chain (examples/strip.h), whose memory, start-up and stack are pinned; the
# same strip as a desktop program writes it, one object on the heap, which the guide's "Where
# memory lives" measures against it; and every C1 stage in one float path, the link-stage
# gate's first probe image.
catenae_add_probe(strip probe/strip_probe.cpp)
catenae_add_probe(strip_object probe/strip_object_probe.cpp)
catenae_add_probe(c1 probe/c1_probe.cpp)
# C2's: the edges in one path, bytes in and out (the link-stage gate's image for the family).
catenae_add_probe(c2 probe/c2_probe.cpp)
# C4's: a float transform of 512 both ways from the constant tables, and a plan made at run
# time; the FFT family's image for the link-stage gate.
catenae_add_probe(fft probe/fft_probe.cpp)
catenae_add_probe(recursive probe/recursive_probe.cpp)
catenae_add_probe(fir probe/fir_probe.cpp)
catenae_add_probe(overdrive probe/overdrive_probe.cpp)
catenae_add_probe(designers probe/designers_probe.cpp)
foreach(_probe IN ITEMS catenae_probe_strip catenae_stack_strip catenae_probe_recursive catenae_stack_recursive
                        catenae_probe_fir catenae_stack_fir catenae_probe_overdrive catenae_stack_overdrive)
    target_include_directories(${_probe} PRIVATE "${PROJECT_SOURCE_DIR}/examples")
endforeach()

# --- The link-stage gate (D36) -----------------------------------------------------------
# The gate is scripts/link_gate.py over a named probe image, one per float stage family: C1's,
# catenae_probe_c1, and C2's, catenae_probe_c2, each of which must pass on every Arm leg. The gate also proves
# itself on two fixtures on every Arm leg: an image of float arithmetic passes,
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
    add_test(NAME link_gate.C1ImageHasNoSoftwareDouble
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_probe_c1>)
    add_test(NAME link_gate.C2ImageHasNoSoftwareDouble
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_probe_c2>)
    add_test(NAME link_gate.FftImageHasNoSoftwareDouble
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_probe_fft>)
    add_test(NAME link_gate.RecursiveImageHasNoSoftwareDouble
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_probe_recursive>)
    add_test(NAME link_gate.FirImageHasNoSoftwareDouble
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_probe_fir>)
    add_test(NAME link_gate.OverdriveImageHasNoSoftwareDouble
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/link_gate.py
                     $<TARGET_FILE:catenae_probe_overdrive>)
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
