# Copyright 2026 Timothy Place and the catenae contributors
#
# The compile-fail battery (PLAN.md section 10, C0b): each case is one source that must not
# compile, and must fail with the message on its "// Expect: " line (a regex). Each case also
# compiles as its own control, with CATENAE_CF_CONTROL defined, in the ordinary build: the
# control is the case with its one defect removed, so a case fails for the reason it names and
# no other. A "// Compilers: " line lists the CMAKE_CXX_COMPILER_IDs a case is for; without
# one it is for all.
#
# Each case is a target outside the default build that CTest asks the build tree to build,
# through tests/support/expect_build.cmake; one lock keeps two builds of the tree apart.
file(GLOB _catenae_cf_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(SORT _catenae_cf_cases)

set(_catenae_cf_controls)
foreach(_case IN LISTS _catenae_cf_cases)
    get_filename_component(_name "${_case}" NAME_WE)
    file(STRINGS "${_case}" _compilers REGEX "^// Compilers: ")
    if(_compilers)
        string(REGEX REPLACE "^// Compilers: " "" _compilers "${_compilers}")
        separate_arguments(_compilers)
        if(NOT CMAKE_CXX_COMPILER_ID IN_LIST _compilers)
            continue()
        endif()
    endif()
    list(APPEND _catenae_cf_controls "${_case}")
    add_library(catenae_cf_${_name} OBJECT EXCLUDE_FROM_ALL "${_case}")
    target_include_directories(catenae_cf_${_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(catenae_cf_${_name} PRIVATE catenae::catenae catenae_test_warnings)
    # A case does not compile, so it stays out of the compile database the tidy gate and the
    # naming and shift scripts read; its control is there in its place.
    set_target_properties(catenae_cf_${_name} PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
    add_test(NAME compile_fail.${_name}
             COMMAND "${CMAKE_COMMAND}" -DBUILD_DIR=${PROJECT_BINARY_DIR} -DTARGET=catenae_cf_${_name}
                     -DCONFIG=$<CONFIG> -DEXPECT_FILE=${_case} -P "${PROJECT_SOURCE_DIR}/tests/support/expect_build.cmake")
    set_tests_properties(compile_fail.${_name} PROPERTIES
        LABELS compile_fail RESOURCE_LOCK catenae_build_tree TIMEOUT 300)
endforeach()

add_library(catenae_cf_controls OBJECT ${_catenae_cf_controls})
target_include_directories(catenae_cf_controls PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(catenae_cf_controls PRIVATE CATENAE_CF_CONTROL)
target_link_libraries(catenae_cf_controls PRIVATE catenae::catenae catenae_test_warnings)
