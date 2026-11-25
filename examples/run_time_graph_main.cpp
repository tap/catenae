/// @file run_time_graph_main.cpp
/// @brief The strawman run: the same strip as strip.h, joined at run time.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstdio>

#include "run_time_graph.h"

int main() {
    using namespace example::run_time;
    // ANCHOR: built
    static gain_node  level{1.0f};
    static delay_node line;
    static halve_node half;
    static graph      strip;
    // Each joint is checked here, as the program runs: a mismatch is a false to handle.
    const bool joined = strip.connect(level) && strip.connect(line) && strip.connect(half);
    // ANCHOR_END: built
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    in.fill(1.0f);
    for (int block = 0; block < 4; ++block) {
        strip.process(in.data(), out.data(), 64);
    }
    const bool ok = joined && out[0] > 0.49f && out[0] < 0.51f;
    std::printf("graph: %.3f\n", static_cast<double>(out[0]));
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
