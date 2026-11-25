# Copyright 2026 Timothy Place and the catenae contributors
#
# Off the bare-metal legs: run one ratchet workload twice (under EMULATOR when there is one);
# both runs must exit 0, report ok=1 and print byte-identical CATENAE_ICOUNT_DONE lines (the
# checksum is a bit-exact fingerprint). Carried from DspTap's bench/icount/smoke.cmake
# (NOTICE.md).
#   cmake -DBINARY=<path> [-DEMULATOR=<command>] -P smoke.cmake
if(NOT BINARY)
    message(FATAL_ERROR "smoke.cmake: -DBINARY=<workload> is required")
endif()
set(_lines)
foreach(_run 1 2)
    execute_process(COMMAND ${EMULATOR} "${BINARY}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _out)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${BINARY}: run ${_run} exited ${_rc}:\n${_out}")
    endif()
    string(REGEX MATCH "CATENAE_ICOUNT_DONE ok=1[^\n]*" _line "${_out}")
    if(NOT _line)
        message(FATAL_ERROR "${BINARY}: run ${_run} did not report ok=1:\n${_out}")
    endif()
    list(APPEND _lines "${_line}")
endforeach()
list(GET _lines 0 _first)
list(GET _lines 1 _second)
if(NOT _first STREQUAL _second)
    message(FATAL_ERROR "${BINARY}: two runs differ:\n  ${_first}\n  ${_second}")
endif()
message(STATUS "${_first}")
