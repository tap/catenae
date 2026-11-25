# Copyright 2026 Timothy Place and the catenae contributors
#
# The guide's steady<> figure is drawn by scripts/figure_steady.py from what the example prints
# (PLAN.md section 11.1), and both are committed. This runs the example, draws the figure again
# and fails when it is not the committed one, so the figure cannot drift from the code.
#
#   cmake -DEXAMPLE=exe -DPYTHON=python -DSCRIPT=figure_steady.py -DFIGURE=steady.svg -DWORK=dir
#         -P figure_check.cmake
execute_process(COMMAND "${EXAMPLE}" OUTPUT_VARIABLE trace RESULT_VARIABLE ran)
if(NOT ran EQUAL 0)
    message(FATAL_ERROR "the example failed (${ran}):\n${trace}")
endif()
file(WRITE "${WORK}/steady_trace.txt" "${trace}")
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${WORK}/steady_trace.txt" --out "${WORK}/steady.svg"
                RESULT_VARIABLE drew)
if(NOT drew EQUAL 0)
    message(FATAL_ERROR "the script failed (${drew})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${WORK}/steady.svg" "${FIGURE}"
                RESULT_VARIABLE differs)
if(NOT differs EQUAL 0)
    message(FATAL_ERROR "${FIGURE} is not what the example and the script draw now; regenerate it:\n"
                        "  catenae_example_bursts_and_buffering | python3 scripts/figure_steady.py "
                        "--out guide/src/figures/steady.svg")
endif()
message(STATUS "the figure is what the example draws")
