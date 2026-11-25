/// @file stages_and_chains.cpp
/// @brief A chain written as a type, run from a fixed-block callback (guide: Stages and chains).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdio>

#include "catenae/compiler.h"
#include "strip.h"

namespace {

    // ANCHOR: callback
    // One instance: its state and the thread's workspace, plain storage in .bss.
    CATENAE_CONSTINIT catenae::memory_of<example::strip_t> s_strip_memory{};

    // Both blocks are arrays, so every size is checked by the compiler and none at run time.
    void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
        static_cast<void>(example::strip.process(s_strip_memory, in, out));
    }
    // ANCHOR_END: callback

} // namespace

int main() {
    example::strip.reset(s_strip_memory.state); // once, before the first callback
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    in.fill(1.0f);
    for (int block = 0; block < 4; ++block) {
        audio_callback(in, out);
    }
    // After the 32-frame delay, a constant 1.0 comes out at half: 0.5 on both channels.
    const bool ok = out[0] > 0.49f && out[0] < 0.51f && out[127] > 0.49f && out[127] < 0.51f;
    std::printf("strip: %.3f %.3f\n", static_cast<double>(out[0]), static_cast<double>(out[127]));
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
