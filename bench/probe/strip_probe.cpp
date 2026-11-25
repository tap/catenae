/// @file strip_probe.cpp
/// @brief The example chain's memory, start-up and stack: the strip of examples/strip.h in an
/// image of its own (PLAN.md section 10, C1; bench/README.md).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Built twice (bench/CMakeLists.txt; scripts/ceilings.py measures both):
//
//   catenae_probe_strip  CATENAE_PROBE_STACK=0, the size probe, linked with a map: the strip's
//                        state and workspace are this unit's .bss, and it adds no .data and no
//                        start-up function (D34)
//   catenae_stack_strip  CATENAE_PROBE_STACK=1, the stack probe: the painted high-water of the
//                        two calls an application makes, reset once and process every callback
//
// Each call is rooted at a noinline probe_<call>, whose frame and everything below it are the
// call's: the static worst path, the code it reaches and the painted figure all start there.
// The blocks live in main's frame, above the roots, so neither call's figure includes them.
#include <array>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "strip.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    /// A callback's block: 64 stereo frames.
    using block = std::array<float, 64 * 2>;

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept;
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    // ANCHOR: memory
    namespace {
        // The strip's one instance: its state and its workspace, plain storage, in .bss.
        CATENAE_CONSTINIT catenae::memory_of<example::strip_t> s_memory{};
    } // namespace

    // Once, before the first callback: the configuration sets its state.
    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        example::strip.reset(s_memory.state);
    }

    // Every callback.
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(example::strip.process(s_memory, in, out));
    }
    // ANCHOR_END: memory

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
    // The memory's two parts, as this target lays them out: what the chain remembers and what
    // a call needs.
    const std::size_t state     = sizeof(catenae::state_of<example::strip_t>);
    const std::size_t workspace = sizeof(catenae::workspace_of<example::strip_t>);
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
