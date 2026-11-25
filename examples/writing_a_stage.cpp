/// @file writing_a_stage.cpp
/// @brief A stage of one's own, in a chain beside the library's (guide: Writing a stage).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstdio>

#include "catenae/compiler.h"
#include "catenae/gain.h"
#include "difference.h"

namespace {

    // ANCHOR: chain
    // A stage of another namespace joins a chain through chain(): its | is catenae's.
    constexpr auto edges = catenae::chain(example::difference<2>{}) | catenae::gain<2>{0.5f, 0};
    using edges_t        = std::remove_cv_t<decltype(edges)>;
    static_assert(edges_t::exact_latency.up == 1 && edges_t::exact_latency.down == 2, "the stage's half frame");
    // ANCHOR_END: chain

    CATENAE_CONSTINIT catenae::memory_of<edges_t> s_edges{};

} // namespace

int main() {
    edges.reset(s_edges.state);
    std::array<float, 8 * 2> in{};
    std::array<float, 8 * 2> out{};
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(i / 2); // a ramp, the same on both channels
    }
    static_cast<void>(edges.process(s_edges, in, out));
    // The ramp's difference is 1 a frame (0 to the first), halved.
    const bool ok = out[0] < 0.01f && out[2] > 0.49f && out[2] < 0.51f && out[15] > 0.49f && out[15] < 0.51f;
    std::printf("difference: %.2f %.2f %.2f\n", static_cast<double>(out[0]), static_cast<double>(out[2]),
                static_cast<double>(out[15]));
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
