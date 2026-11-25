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
