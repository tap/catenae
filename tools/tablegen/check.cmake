# Copyright 2026 Timothy Place and the catenae contributors
#
# The Nyquist tables are generated (tools/tablegen/nyquist_tables.cpp) and committed. This runs
# the generator and fails when either committed file is not what it writes now, so the tables
# cannot drift from the designer.
#
#   cmake -DGENERATOR=exe -DLIBRARY=include/catenae/nyquist_tables.h
#         -DTESTS=tests/support/catenae_test/nyquist_test_tables.h -DWORK=dir -P check.cmake
execute_process(COMMAND "${GENERATOR}" "${WORK}/nyquist_tables.h" "${WORK}/nyquist_test_tables.h"
                RESULT_VARIABLE ran)
if(NOT ran EQUAL 0)
    message(FATAL_ERROR "the generator failed (${ran})")
endif()
foreach(_pair IN ITEMS "nyquist_tables.h|${LIBRARY}" "nyquist_test_tables.h|${TESTS}")
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _made)
    list(GET _parts 1 _committed)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${WORK}/${_made}" "${_committed}"
                    RESULT_VARIABLE differs)
    if(NOT differs EQUAL 0)
        message(FATAL_ERROR "${_committed} is not what the generator writes now; regenerate it:\n"
                            "  catenae_tablegen include/catenae/nyquist_tables.h "
                            "tests/support/catenae_test/nyquist_test_tables.h")
    endif()
endforeach()
message(STATUS "the tables are what the generator writes")
