/// @file with_double.cpp
/// @brief A link-gate fixture: one product in double, which the gate must refuse.
// Copyright 2026 Timothy Place and the catenae contributors
//
// -Wdouble-promotion cannot see an explicit cast (PLAN.md section 7.3): the gate sees the
// software-double routines the cast pulls into the image. The constant is 0.1, which no float
// holds exactly, so the compiler cannot narrow the product to float (it may, and GCC does,
// narrow a quotient or a product whose operands are all exact in float). scripts/link_gate.py's own test on
// each Arm leg (bench/CMakeLists.txt). Harness code, not a probe of the library.
#include <cstdint>

namespace {
    volatile float g_in  = 1.5f;
    volatile float g_out = 0.0f;
} // namespace

int main() {
    const float x = g_in;
    g_out         = static_cast<float>(static_cast<double>(x) * 0.1);
    return 0;
}
