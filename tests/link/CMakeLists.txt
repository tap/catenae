# Copyright 2026 Timothy Place and the catenae contributors
#
# The link tests of PLAN.md section 3.4. The compiled core and the trapping handler are built
# again here at each check level, beside three probes compiled at each level:
#
#   calls_core         calls catenae::version(): the compiled core
#   runtime_check      holds a check of a run-time value: references check_failed
#   compile_time_only  every check it has is evaluated while compiling: references nothing
#
# Each probe is linked against the core and handler of each level, and the compile-time probe
# against nothing at all. A probe that references the core or the handler links only at its own
# level, and fails at the other naming the missing symbol, its level in its name; the
# compile-time probe links everywhere. On the bare-metal legs this is the build's -Os and
# --gc-sections (cmake/arm-cortex-*.cmake).
file(GLOB _catenae_link_core_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
set(_catenae_link_levels boundary audit)

foreach(_level IN LISTS _catenae_link_levels)
    string(TOUPPER "CATENAE_CHECKS_${_level}" CATENAE_CHECKS_VALUE)
    set(CATENAE_CONFIG_WRITER "CMake (tests/link)")
    configure_file("${PROJECT_SOURCE_DIR}/tools/config/config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${_level}/include/catenae/config.h" @ONLY)
    set(_include "${CMAKE_CURRENT_BINARY_DIR}/${_level}/include" "${PROJECT_SOURCE_DIR}/include")

    add_library(catenae_link_core_${_level} STATIC EXCLUDE_FROM_ALL ${_catenae_link_core_sources})
    target_include_directories(catenae_link_core_${_level} PRIVATE ${_include})
    target_link_libraries(catenae_link_core_${_level} PRIVATE catenae_library_warnings catenae_no_exceptions)
    add_library(catenae_link_handler_${_level} OBJECT EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/src/handler/trap.cpp")
    target_include_directories(catenae_link_handler_${_level} PRIVATE ${_include})
    target_link_libraries(catenae_link_handler_${_level} PRIVATE catenae_library_warnings catenae_no_exceptions)
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
                              "catenae::${_level}::version|version@${_level}@catenae")
            catenae_link_case(runtime_check ${_level} ${_against}
                              "catenae::${_level}::check_failed|check_failed@${_level}@catenae")
        endif()
        catenae_link_case(compile_time_only ${_level} ${_against} "")
    endforeach()
    catenae_link_case(compile_time_only ${_level} none "")
endforeach()
