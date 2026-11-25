# Copyright 2026 Timothy Place and the catenae contributors
#
# catenae's reference build (D26, PLAN.md section 7.4). `add_subdirectory` gives one target,
# catenae::catenae: the headers of include/ and the compiled core of src/, built from source
# with the consumer's target flags. The wscript is the waf build of the same files.
cmake_minimum_required(VERSION 3.25)

# The version, in one place per build: here and in the wscript's VERSION, both kept equal to
# include/catenae/version.h by the battery (CAT-VERSION-001).
project(catenae VERSION 0.0.0 LANGUAGES CXX)

# Warnings are errors where catenae is the project being built and off where it is consumed
# (PLAN.md section 7.3).
option(CATENAE_WERROR "Treat warnings as errors in catenae's own targets" ${PROJECT_IS_TOP_LEVEL})
option(CATENAE_BUILD_TESTS "Build catenae's battery" ${PROJECT_IS_TOP_LEVEL})
option(CATENAE_BUILD_EXAMPLES "Build the examples the guide includes" ${PROJECT_IS_TOP_LEVEL})
option(CATENAE_BUILD_BENCH "Build the ratchet's workloads, the size probes and the link-gate fixtures" OFF)
option(CATENAE_COVERAGE "Instrument every target for Clang's source-based coverage" OFF)
option(CATENAE_AMALGAMATION "Build the compiled core from the amalgamated catenae.cpp (scripts/amalgamate.py)" OFF)

# The standard of catenae's own build: 17, 20 or 23 (D2: the same files at each). A consumer's
# own setting wins; the library itself asks for C++17 and nothing later.
set(CATENAE_CXX_STANDARD 17 CACHE STRING "C++ standard of catenae's own build: 17, 20 or 23")
set_property(CACHE CATENAE_CXX_STANDARD PROPERTY STRINGS 17 20 23)
if(PROJECT_IS_TOP_LEVEL)
    if(NOT CATENAE_CXX_STANDARD MATCHES "^(17|20|23)$")
        message(FATAL_ERROR "CATENAE_CXX_STANDARD is 17, 20 or 23 (got '${CATENAE_CXX_STANDARD}')")
    endif()
    set(CMAKE_CXX_STANDARD ${CATENAE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(cmake/catenae_flags.cmake)
include(cmake/catenae_warnings.cmake)

# The check level (D27, PLAN.md section 3.4): boundary, the default, or audit, independent of the
# build type. It is written into catenae/config.h, which catenae/detail/abi.h spells into the
# inline namespace, so code built at one level does not link against the other.
set(CATENAE_CHECKS boundary CACHE STRING "catenae's check level: boundary or audit")
set_property(CACHE CATENAE_CHECKS PROPERTY STRINGS boundary audit)
if(NOT CATENAE_CHECKS MATCHES "^(boundary|audit)$")
    message(FATAL_ERROR "CATENAE_CHECKS is boundary or audit (got '${CATENAE_CHECKS}')")
endif()
string(TOUPPER "CATENAE_CHECKS_${CATENAE_CHECKS}" CATENAE_CHECKS_VALUE)
set(CATENAE_CONFIG_WRITER "CMake")

if(CATENAE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CATENAE_COVERAGE needs Clang (source-based coverage)")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

# The compiled core (D4). From the amalgamation it is the one generated file; otherwise every
# source of src/.
if(CATENAE_AMALGAMATION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_catenae_amalgamation "${PROJECT_BINARY_DIR}/amalgamation")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/amalgamate.py" --out "${_catenae_amalgamation}"
                --checks "${CATENAE_CHECKS}"
        COMMAND_ERROR_IS_FATAL ANY)
    set(_catenae_sources "${_catenae_amalgamation}/catenae.cpp")
    set(_catenae_include "${_catenae_amalgamation}/include")
else()
    file(GLOB _catenae_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
    # catenae/config.h is the build's, written beside the headers into the build tree.
    configure_file("${PROJECT_SOURCE_DIR}/tools/config/config.h.in"
                   "${PROJECT_BINARY_DIR}/generated/include/catenae/config.h" @ONLY)
    set(_catenae_include "${PROJECT_BINARY_DIR}/generated/include" "${PROJECT_SOURCE_DIR}/include")
endif()

add_library(catenae STATIC ${_catenae_sources})
add_library(catenae::catenae ALIAS catenae)
# Never a system include: catenae's headers are held to the warning sets in every consumer's
# view (PLAN.md section 7.3).
target_include_directories(catenae PUBLIC "$<BUILD_INTERFACE:${_catenae_include}>")
target_compile_features(catenae PUBLIC cxx_std_17)
target_link_libraries(catenae PRIVATE catenae_library_warnings catenae_no_exceptions catenae_contraction)
# One copy of the compiled code, dropped when unused (PLAN.md section 7.1).
if(NOT MSVC)
    target_compile_options(catenae PRIVATE -ffunction-sections -fdata-sections)
endif()

# The default handler, which traps (PLAN.md section 3.4): not part of the core, which defines no
# handler. An integrator links it, or defines check_failed where their system has a safe state.
add_library(catenae_trap_handler OBJECT "${PROJECT_SOURCE_DIR}/src/handler/trap.cpp")
add_library(catenae::trap_handler ALIAS catenae_trap_handler)
target_link_libraries(catenae_trap_handler PRIVATE catenae catenae_library_warnings catenae_no_exceptions)

if(PROJECT_IS_TOP_LEVEL)
    include(cmake/catenae_header_checks.cmake)
endif()

if(CATENAE_BUILD_TESTS OR CATENAE_BUILD_EXAMPLES OR CATENAE_BUILD_BENCH)
    enable_testing()
endif()
if(CATENAE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CATENAE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(CATENAE_BUILD_BENCH)
    add_subdirectory(bench)
endif()
