/// @file denoise_probe.cpp
/// @brief The link-stage gate's probe image for the transform domain (D36, PLAN.md 7.3): PLAN.md
/// 9.5's region of examples/spectral_gate.h inside steady, in float.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gate (scripts/link_gate.py) refuses an image of the float profile that links a
// software-double or double-libm routine. The windows are the core's float constants, the gate's
// design a constant, the transforms the core's float plan, and the inverse's divisors are
// computed in float by its reset.
#include <array>
#include <cstddef>

#include "call_marker.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "spectral_gate.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    /// A callback's block: 64 mono frames.
    using block = std::array<float, 64>;

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept;
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    namespace {
        CATENAE_CONSTINIT catenae::memory_of<example::denoise_t> s_memory{};
    } // namespace

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        example::denoise.reset(s_memory.state);
    }

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(example::denoise.process(s_memory, in, out));
    }

} // namespace catenae_probe

int main() {
    catenae_probe::block in{};
    catenae_probe::block out{};
    in.fill(0.25f);
#if CATENAE_PROBE_STACK
    const std::size_t reset = catenae_probe::high_water([] { catenae_probe::probe_reset(); });
    // Two callbacks: the second completes a hop, so it runs the transforms.
    std::size_t process = 0;
    for (int call = 0; call < 2; ++call) {
        const std::size_t high = catenae_probe::high_water([&in, &out] { catenae_probe::probe_process(in, out); });
        process                = high > process ? high : process;
    }
    std::printf("CATENAE_STACK reset %lu\n", static_cast<unsigned long>(reset));
    std::printf("CATENAE_STACK process %lu\n", static_cast<unsigned long>(process));
    const std::size_t state     = sizeof(catenae::state_of<example::denoise_t>);
    const std::size_t workspace = sizeof(catenae::workspace_of<example::denoise_t>);
    std::printf("CATENAE_SIZEOF state %lu\n", static_cast<unsigned long>(state));
    std::printf("CATENAE_SIZEOF workspace %lu\n", static_cast<unsigned long>(workspace));
    const bool ok = reset < catenae_probe::paint_bytes && process < catenae_probe::paint_bytes;
    std::printf("CATENAE_PROBE_DONE ok=%d\n", ok ? 1 : 0);
#else
    catenae_probe::probe_reset();
    catenae_probe::probe_process(in, out);
    catenae_probe::probe_process(in, out);
#endif
    return 0;
}
