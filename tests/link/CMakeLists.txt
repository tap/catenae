# Copyright 2026 Timothy Place and the catenae contributors
#
# The link tests of PLAN.md section 3.4. The compiled core and the trapping handler are built
# again here in three configurations: each check level with the default FFT back ends, and
# boundary with every FFT left out (`fftnone`). Three probes are compiled in each:
#
#   calls_core         calls catenae::version(): the compiled core
#   runtime_check      holds a check of a run-time value: references check_failed
#   compile_time_only  every check it has is evaluated while compiling: references nothing
#
# Each probe is linked against the core and handler of each check level, and the compile-time
# probe against nothing at all. A probe that references the core or the handler links only at
# its own level, and fails at the other naming the missing symbol, its level in its name; the
# compile-time probe links everywhere. The core-calling probe is also linked across the FFT
# choices, and links only where they are the same (CAT-ABI-002). On the bare-metal legs this is
# the build's -Os and --gc-sections (cmake/arm-cortex-*.cmake).
file(GLOB _catenae_link_core_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
set(_catenae_link_levels boundary audit)
set(_catenae_link_configs boundary audit fftnone)

foreach(_config IN LISTS _catenae_link_configs)
    set(_level ${_config})
    set(_fft_f32 ${CATENAE_FFT_F32_VALUE})
    set(_fft_f64 ${CATENAE_FFT_F64_VALUE})
    set(_fft_q15 ${CATENAE_FFT_Q15_VALUE})
    set(_fft_q31 ${CATENAE_FFT_Q31_VALUE})
    if(_config STREQUAL "fftnone")
        set(_level boundary)
        foreach(_type IN ITEMS F32 F64 Q15 Q31)
            set(CATENAE_FFT_${_type}_VALUE CATENAE_FFT_NONE)
        endforeach()
    endif()
    string(TOUPPER "CATENAE_CHECKS_${_level}" CATENAE_CHECKS_VALUE)
    set(CATENAE_CONFIG_WRITER "CMake (tests/link)")
    configure_file("${PROJECT_SOURCE_DIR}/tools/config/config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${_config}/include/catenae/config.h" @ONLY)
    set(CATENAE_FFT_F32_VALUE ${_fft_f32})
    set(CATENAE_FFT_F64_VALUE ${_fft_f64})
    set(CATENAE_FFT_Q15_VALUE ${_fft_q15})
    set(CATENAE_FFT_Q31_VALUE ${_fft_q31})
    set(_include "${CMAKE_CURRENT_BINARY_DIR}/${_config}/include" "${PROJECT_SOURCE_DIR}/include")

    add_library(catenae_link_core_${_config} STATIC EXCLUDE_FROM_ALL ${_catenae_link_core_sources})
    target_include_directories(catenae_link_core_${_config} PRIVATE ${_include})
    target_link_libraries(catenae_link_core_${_config} PRIVATE catenae_library_warnings catenae_no_exceptions
                                                          catenae_constexpr)
    add_library(catenae_link_handler_${_config} OBJECT EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/src/handler/trap.cpp")
    target_include_directories(catenae_link_handler_${_config} PRIVATE ${_include})
    target_link_libraries(catenae_link_handler_${_config} PRIVATE catenae_library_warnings catenae_no_exceptions)
endforeach()

# catenae_link_case(<probe> <probe level> <against: a level, or none> <regex, or empty to link>)
function(catenae_link_case probe level against regex)
    set(_target catenae_link_${probe}_${level}_${against})
    add_executable(${_target} EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/${probe}.cpp")
    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${level}/include"
                                                  "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(${_target} PRIVATE catenae_test_warnings catenae_no_exceptions)
    if(NOT against STREQUAL "none")
        target_link_libraries(${_target} PRIVATE catenae_link_core_${against} catenae_link_handler_${against})
    endif()
    if(regex)
        set(_expect "-DEXPECT_REGEX=${regex}")
    else()
        set(_expect -DEXPECT_SUCCESS=ON)
    endif()
    add_test(NAME link.${probe}.${level}_against_${against}
             COMMAND "${CMAKE_COMMAND}" -DBUILD_DIR=${PROJECT_BINARY_DIR} -DTARGET=${_target} -DCONFIG=$<CONFIG>
                     ${_expect} -P "${PROJECT_SOURCE_DIR}/tests/support/expect_build.cmake")
    set_tests_properties(link.${probe}.${level}_against_${against} PROPERTIES
        LABELS link RESOURCE_LOCK catenae_build_tree TIMEOUT 300)
endfunction()

foreach(_level IN LISTS _catenae_link_levels)
    foreach(_against IN LISTS _catenae_link_levels)
        if(_level STREQUAL _against)
            catenae_link_case(calls_core ${_level} ${_against} "")
            catenae_link_case(runtime_check ${_level} ${_against} "")
        else()
            # GNU ld, lld and ld64 print the demangled name, MSVC's linker the decorated one too.
            catenae_link_case(calls_core ${_level} ${_against}
                              "catenae::${_level}_[a-z_]+::version|version@${_level}_[a-z_]+@catenae")
            catenae_link_case(runtime_check ${_level} ${_against}
                              "catenae::${_level}_[a-z_]+::check_failed|check_failed@${_level}_[a-z_]+@catenae")
        endif()
        catenae_link_case(compile_time_only ${_level} ${_against} "")
    endforeach()
    catenae_link_case(compile_time_only ${_level} none "")
endforeach()

# Across the FFT choices: the core-calling probe links only against a core with the same ones.
catenae_link_case(calls_core fftnone fftnone "")
catenae_link_case(calls_core boundary fftnone "catenae::boundary_own_own_own_own::version|version@boundary_own_own_own_own@catenae")
catenae_link_case(calls_core fftnone boundary "catenae::boundary_none_none_none_none::version|version@boundary_none_none_none_none@catenae")
