/// @file overdrive_probe.cpp
/// @brief PLAN.md 9.4's memory, start-up and stack: the four-times overdrive of
/// examples/overdrive.h, two voices and one workspace; the rate family's image for the
/// link-stage gate (D36).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Built twice (bench/CMakeLists.txt; scripts/ceilings.py measures both): the size probe, whose
// .rodata holds the configuration and whose .bss the two voices and the workspace, and the
// stack probe, the painted high-water of reset (both voices) and of one callback. The designs
// are generated constants and the soft clip is tanhf, so the gate finds no software double.
#include <array>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "overdrive.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    /// A callback's block: 64 stereo frames.
    using block = std::array<float, 64 * 2>;

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept;
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    namespace {
        // Two voices, each its state, and the thread's workspace, which both use: plain
        // storage, in .bss.
        CATENAE_CONSTINIT catenae::state_of<example::overdrive_t> s_voice_a{};
        CATENAE_CONSTINIT catenae::state_of<example::overdrive_t> s_voice_b{};
        CATENAE_CONSTINIT catenae::workspace_of<example::overdrive_t> s_work{};
    } // namespace

    // Once, before the first callback.
    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        example::overdrive.reset(s_voice_a);
        example::overdrive.reset(s_voice_b);
    }

    // Every callback, one voice.
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(example::overdrive.process(s_voice_a, s_work, in, out));
    }

} // namespace catenae_probe

int main() {
    catenae_probe::block in{};
    catenae_probe::block out{};
    in.fill(0.25f);
#if CATENAE_PROBE_STACK
    const std::size_t reset   = catenae_probe::high_water([] { catenae_probe::probe_reset(); });
    const std::size_t process = catenae_probe::high_water([&in, &out] { catenae_probe::probe_process(in, out); });
    std::printf("CATENAE_STACK reset %lu\n", static_cast<unsigned long>(reset));
    std::printf("CATENAE_STACK process %lu\n", static_cast<unsigned long>(process));
    const std::size_t state     = sizeof(catenae::state_of<example::overdrive_t>);
    const std::size_t workspace = sizeof(catenae::workspace_of<example::overdrive_t>);
    std::printf("CATENAE_SIZEOF state %lu\n", static_cast<unsigned long>(state));
    std::printf("CATENAE_SIZEOF workspace %lu\n", static_cast<unsigned long>(workspace));
    const bool ok = reset < catenae_probe::paint_bytes && process < catenae_probe::paint_bytes;
    std::printf("CATENAE_PROBE_DONE ok=%d\n", ok ? 1 : 0);
#else
    catenae_probe::probe_reset();
    catenae_probe::probe_process(in, out);
#endif
    return 0;
}
