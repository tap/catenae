# Copyright 2026 Timothy Place and the catenae contributors
#
# The examples the guide includes (PLAN.md section 11.2, rule 5): every listing on a guide page
# comes from a file here, which the build compiles and CTest runs. An example passes when it
# exits 0 and prints its CATENAE_EXAMPLE_DONE line (the bare-metal legs judge the text, since
# semihosting does not reliably carry an exit code).
function(catenae_add_example name)
    add_executable(catenae_example_${name} ${name}.cpp)
    target_link_libraries(catenae_example_${name} PRIVATE catenae::catenae catenae::trap_handler catenae_test_warnings)
    add_test(NAME example.${name} COMMAND catenae_example_${name})
    set_tests_properties(example.${name} PROPERTIES PASS_REGULAR_EXPRESSION "CATENAE_EXAMPLE_DONE ok=1" TIMEOUT 120)
endfunction()

catenae_add_example(print_version)
catenae_add_example(stages_and_chains)
catenae_add_example(writing_a_stage)
target_include_directories(catenae_example_writing_a_stage PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catenae_add_example(bursts_and_buffering)
target_include_directories(catenae_example_bursts_and_buffering PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catenae_add_example(edges)
catenae_add_example(audio_callback)
target_include_directories(catenae_example_audio_callback PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catenae_add_example(arithmetic_costs)
catenae_add_example(overdrive)
target_include_directories(catenae_example_overdrive PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catenae_add_example(dynamics)
target_include_directories(catenae_example_dynamics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catenae_add_example(planar)
catenae_add_example(long_filter)
target_include_directories(catenae_example_long_filter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(catenae_example_arithmetic_costs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Its figure (guide/src/figures/steady.svg) is drawn from its output: on a host with Python, the
# figure is drawn again and must be the committed one.
if(NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME example.bursts_and_buffering_figure
                 COMMAND "${CMAKE_COMMAND}" "-DEXAMPLE=$<TARGET_FILE:catenae_example_bursts_and_buffering>"
                         "-DPYTHON=${Python3_EXECUTABLE}" "-DSCRIPT=${PROJECT_SOURCE_DIR}/scripts/figure_steady.py"
                         "-DFIGURE=${PROJECT_SOURCE_DIR}/guide/src/figures/steady.svg"
                         "-DWORK=${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/figure_check.cmake")
    endif()
endif()

# Threads and clocks: a mailbox and a junction across threads, on a host that has them.
if(NOT CATENAE_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(Threads_FOUND)
    catenae_add_example(threads_and_clocks)
    target_link_libraries(catenae_example_threads_and_clocks PRIVATE Threads::Threads)
    # What one would naturally write instead (guide: Threads and clocks), built only under the
    # thread sanitizer, where it passes when the sanitizer reports its data race: it never runs
    # in the battery, and the race is the point.
    if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
        add_executable(catenae_example_racy_level racy_level.cpp)
        target_link_libraries(catenae_example_racy_level PRIVATE catenae_test_warnings Threads::Threads)
        add_test(NAME example.racy_level_is_a_data_race COMMAND catenae_example_racy_level)
        set_tests_properties(example.racy_level_is_a_data_race PROPERTIES
                             PASS_REGULAR_EXPRESSION "WARNING: ThreadSanitizer: data race" TIMEOUT 120)
    endif()
endif()

# The guide's strawman: what one would naturally write instead of a chain. Its nodes are a
# library of their own so that the ratchet's workload counts the same code.
add_library(catenae_example_graph STATIC run_time_graph.cpp)
target_include_directories(catenae_example_graph PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(catenae_example_graph PRIVATE catenae_test_warnings catenae_no_exceptions)
add_executable(catenae_example_run_time_graph run_time_graph_main.cpp)
target_link_libraries(catenae_example_run_time_graph PRIVATE catenae_example_graph catenae_test_warnings)
add_test(NAME example.run_time_graph COMMAND catenae_example_run_time_graph)
set_tests_properties(example.run_time_graph PROPERTIES PASS_REGULAR_EXPRESSION "CATENAE_EXAMPLE_DONE ok=1" TIMEOUT 120)
