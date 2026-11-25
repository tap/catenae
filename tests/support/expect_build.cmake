# Copyright 2026 Timothy Place and the catenae contributors
#
# Builds one target of this build tree and judges the result: the compile-fail battery and the
# link tests (tests/compile_fail, tests/link) run through it from CTest.
#
#   cmake -DBUILD_DIR=<tree> -DTARGET=<target> [-DCONFIG=<config>]
#         (-DEXPECT_SUCCESS=ON | -DEXPECT_REGEX=<regex> | -DEXPECT_FILE=<source>) -P expect_build.cmake
#
# EXPECT_SUCCESS: the target must build. Otherwise it must fail, and its output must match
# EXPECT_REGEX, or the regex on the "// Expect: " line of EXPECT_FILE: a failure for any other
# reason is a failure of the test, with the output printed.
foreach(_required BUILD_DIR TARGET)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "expect_build.cmake: ${_required} is required")
    endif()
endforeach()
if(DEFINED EXPECT_FILE)
    file(STRINGS "${EXPECT_FILE}" _expect REGEX "^// Expect: ")
    list(LENGTH _expect _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR "${EXPECT_FILE} has ${_count} '// Expect: ' lines, not one")
    endif()
    string(REGEX REPLACE "^// Expect: " "" EXPECT_REGEX "${_expect}")
endif()
set(_config)
if(CONFIG)
    set(_config --config "${CONFIG}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}" ${_config}
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
if(EXPECT_SUCCESS)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${TARGET} must build, and did not:\n${_out}")
    endif()
    message(STATUS "${TARGET} built, as it must")
    return()
endif()
if(_rc EQUAL 0)
    message(FATAL_ERROR "${TARGET} built, and must not")
endif()
if(NOT _out MATCHES "${EXPECT_REGEX}")
    message(FATAL_ERROR "${TARGET} failed, but without '${EXPECT_REGEX}':\n${_out}")
endif()
message(STATUS "${TARGET} failed as it must: ${CMAKE_MATCH_0}")
