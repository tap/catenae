/// @file runtime_check.cpp
/// @brief A link-test probe that holds a check of a run-time value (tests/link/CMakeLists.txt).
// Copyright 2026 Timothy Place and the catenae contributors
// Verifies CAT-CHECK-004.
#include "catenae/check.h"

int main(int argc, char**) {
    catenae::detail::check_always(argc >= 0, catenae::check::invariant);
    return 0;
}
