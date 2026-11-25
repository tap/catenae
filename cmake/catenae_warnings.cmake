# Copyright 2026 Timothy Place and the catenae contributors
#
# The warning sets of PLAN.md section 7.3, read from tools/warnings/ (their one home, shared
# with the wscript), as three interface targets:
#
#   catenae_warnings          every catenae target: the compiler's set, plus -Werror or /WX
#                             when CATENAE_WERROR is on
#   catenae_library_warnings  the compiled core and the header-alone checks: the above plus
#                             the library-only flags (GCC's per-function frame limit)
#   catenae_test_warnings     test, example and bench code: the above less what the test
#                             framework needs off (Clang)

# Reads one flag file: a flag per line, `#` comments and blank lines ignored.
function(catenae_read_flags file out)
    file(STRINGS "${file}" _lines)
    set(_flags)
    foreach(_line IN LISTS _lines)
        string(REGEX REPLACE "#.*$" "" _line "${_line}")
        string(STRIP "${_line}" _line)
        if(NOT _line STREQUAL "")
            list(APPEND _flags "${_line}")
        endif()
    endforeach()
    set(${out} "${_flags}" PARENT_SCOPE)
endfunction()

set(_catenae_warning_dir "${CMAKE_CURRENT_LIST_DIR}/../tools/warnings")
set(_catenae_set "")
set(_catenae_library_set "")
set(_catenae_test_set "")
if(MSVC)
    catenae_read_flags("${_catenae_warning_dir}/msvc.txt" _catenae_set)
    set(_catenae_werror /WX)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    catenae_read_flags("${_catenae_warning_dir}/gcc.txt" _catenae_set)
    catenae_read_flags("${_catenae_warning_dir}/gcc-library.txt" _catenae_library_set)
    set(_catenae_werror -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    catenae_read_flags("${_catenae_warning_dir}/clang.txt" _catenae_set)
    catenae_read_flags("${_catenae_warning_dir}/clang-tests.txt" _catenae_test_set)
    set(_catenae_werror -Werror)
else()
    message(FATAL_ERROR "catenae: no warning set for ${CMAKE_CXX_COMPILER_ID} (tools/warnings/)")
endif()

# Each target carries its whole list in order: with Clang a -Wno- must come after -Weverything.
set(_catenae_werror_flags)
if(CATENAE_WERROR)
    set(_catenae_werror_flags ${_catenae_werror})
endif()
add_library(catenae_warnings INTERFACE)
target_compile_options(catenae_warnings INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:${_catenae_set};${_catenae_werror_flags}>")

add_library(catenae_library_warnings INTERFACE)
target_compile_options(catenae_library_warnings INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:${_catenae_set};${_catenae_library_set};${_catenae_werror_flags}>")

add_library(catenae_test_warnings INTERFACE)
target_compile_options(catenae_test_warnings INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:${_catenae_set};${_catenae_test_set};${_catenae_werror_flags}>")
