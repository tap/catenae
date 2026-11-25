# Copyright 2026 Timothy Place and the catenae contributors
#
# Off the bare-metal legs: two workloads of one computation must print the same checksum, the
# same output bit for bit (bench/CMakeLists.txt names the pairs).
#   cmake -DFIRST=<path> -DSECOND=<path> [-DEMULATOR=<command>] -P agree.cmake
foreach(_required FIRST SECOND)
    if(NOT ${_required})
        message(FATAL_ERROR "agree.cmake: -D${_required}=<workload> is required")
    endif()
endforeach()
set(_sums)
foreach(_binary IN ITEMS "${FIRST}" "${SECOND}")
    execute_process(COMMAND ${EMULATOR} "${_binary}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _out)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${_binary} exited ${_rc}:\n${_out}")
    endif()
    string(REGEX MATCH "CATENAE_ICOUNT_DONE ok=1 checksum=([0-9a-f]+)" _line "${_out}")
    if(NOT _line)
        message(FATAL_ERROR "${_binary} did not report ok=1:\n${_out}")
    endif()
    list(APPEND _sums "${CMAKE_MATCH_1}")
endforeach()
list(GET _sums 0 _first)
list(GET _sums 1 _second)
if(NOT _first STREQUAL _second)
    message(FATAL_ERROR "the two differ: ${_first} against ${_second}")
endif()
message(STATUS "the same output: ${_first}")
