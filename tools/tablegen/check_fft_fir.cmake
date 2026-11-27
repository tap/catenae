# Copyright 2026 Timothy Place and the catenae contributors
#
# The FFT FIR's example tables are generated (tools/tablegen/fft_fir_tables.cpp) and committed.
# This runs the generator and fails when the committed file is not what it writes now, so the
# tables cannot drift from the designers.
#
#   cmake -DGENERATOR=exe -DCOMMITTED=examples/fft_fir_tables.h -DWORK=dir -P check_fft_fir.cmake
execute_process(COMMAND "${GENERATOR}" "${WORK}/fft_fir_tables.h" RESULT_VARIABLE ran)
if(NOT ran EQUAL 0)
    message(FATAL_ERROR "the generator failed (${ran})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${WORK}/fft_fir_tables.h" "${COMMITTED}"
                RESULT_VARIABLE differs)
if(NOT differs EQUAL 0)
    message(FATAL_ERROR "${COMMITTED} is not what the generator writes now; regenerate it:\n"
                        "  catenae_tablegen_fft_fir examples/fft_fir_tables.h")
endif()
message(STATUS "the FFT FIR's tables are what the generator writes")
