/// @file calls_core.cpp
/// @brief A link-test probe that calls the compiled core (tests/link/CMakeLists.txt).
// Copyright 2026 Timothy Place and the catenae contributors
// Verifies CAT-ABI-001, CAT-ABI-002.
#include "catenae/version.h"

int main() {
    return catenae::version() == CATENAE_VERSION ? 0 : 1;
}
