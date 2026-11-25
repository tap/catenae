/// @file compile_time_only.cpp
/// @brief A link-test probe whose checks are all evaluated while it compiles (tests/link).
// Copyright 2026 Timothy Place and the catenae contributors
// Verifies CAT-ABI-001: nothing in it differs by level, so it links anywhere.
#include "catenae/ratio.h"
#include "catenae/span.h"

namespace {
    constexpr int            k_values[] = {1, 2, 3};
    constexpr catenae::ratio k_sum      = catenae::ratio{1, 2} + catenae::ratio{1, 3};
} // namespace

static_assert(catenae::span<const int>(k_values).subspan(1).size() == 2, "a checked subview, at compile time");

int main() {
    return static_cast<int>(k_sum.up) - 5;
}
