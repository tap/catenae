/// @file float_only.cpp
/// @brief A link-gate fixture: an image whose arithmetic is all float, which the gate passes.
// Copyright 2026 Timothy Place and the catenae contributors
//
// scripts/link_gate.py's own test on each Arm leg (bench/CMakeLists.txt): this image must pass
// and with_double.cpp's must be refused. Harness code, not a probe of the library.
#include <cstdint>

namespace {
    volatile float g_in  = 1.5f;
    volatile float g_out = 0.0f;
} // namespace

int main() {
    const float x = g_in;
    g_out         = x * 0.5f + x / 3.0f;
    return 0;
}
