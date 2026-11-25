# Copyright 2026 Timothy Place and the catenae contributors
#
# The flags every catenae build has (D21, D2, PLAN.md section 7.1):
#
#   catenae_no_exceptions  -fno-exceptions -fno-rtti (MSVC: /EHs-c- /GR- and _HAS_EXCEPTIONS=0).
#                          There is no second configuration. Where catenae is the project being
#                          built, every target in its tree gets them, the test framework
#                          included; where it is consumed, only catenae's own targets do.
#   catenae_contraction    the compiled core's contraction policy: off, so its bit pins hold
#                          whatever the consumer passes (-ffp-contract=off; MSVC's default
#                          /fp:precise does not contract).

add_library(catenae_no_exceptions INTERFACE)
add_library(catenae_contraction INTERFACE)
if(MSVC)
    target_compile_options(catenae_no_exceptions INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/EHs-c-;/GR->")
    target_compile_definitions(catenae_no_exceptions INTERFACE _HAS_EXCEPTIONS=0)
    target_compile_options(catenae_contraction INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/fp:precise>")
else()
    target_compile_options(catenae_no_exceptions INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
    target_compile_options(catenae_contraction INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>")
endif()

if(PROJECT_IS_TOP_LEVEL)
    if(MSVC)
        # CMake's default MSVC flags turn exceptions on (/EHsc); there is no policy for it.
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHs-c-;/GR->")
        add_compile_definitions(_HAS_EXCEPTIONS=0)
    else()
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
    endif()
endif()
