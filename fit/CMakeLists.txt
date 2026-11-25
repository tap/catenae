# Copyright 2026 Timothy Place and the catenae contributors
#
# The fit milestone (PLAN.md section 10, CF): throwaway skeletons of SampleRateTap's three
# engines written against C0b's and C1's contracts, built only with CATENAE_BUILD_FIT. They
# are not library code: nothing under include/ or src/ includes them, and they are deleted
# when C7 and C8 bring the engines themselves. fit/README.md says what each one shows.
#
# Each engine's directory holds its headers and its tests (test_*.cpp); the glob takes them
# all, so a skeleton adds files without editing this one.
# GoogleTest's discovery reads a variable its module sets in the including directory's scope.
include(GoogleTest)
file(GLOB _catenae_fit_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/test_*.cpp)
if(NOT _catenae_fit_tests)
    message(FATAL_ERROR "fit/: no test_*.cpp under any skeleton's directory")
endif()
# The battery's check handler and generic harness, and its fixture stages, as the battery has
# them (tests/CMakeLists.txt).
catenae_add_gtest_executable(catenae_fit_tests
    SOURCES ${_catenae_fit_tests} ${PROJECT_SOURCE_DIR}/tests/support/catenae_test/check_trap.cpp LABELS fit)
target_include_directories(catenae_fit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests/support
                                                      ${PROJECT_SOURCE_DIR}/tests/fixtures)
