/// @file fft_fir_probe.cpp
/// @brief The link-stage gate's probe image for the FFT FIR (D36, PLAN.md 7.3): the 255-tap
/// low-pass of examples/long_filter.h by fast convolution in steady<>, from its spectrum in ROM,
/// in float.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gate (scripts/link_gate.py) refuses an image of the float profile that links a
// software-double or double-libm routine. This image's design is a table, so no designer is
// reachable: the audio path is the float transforms and the product of the compiled core, and
// the stage's and steady<>'s copies. The spectrum's make() is the designers probe's
// (designers_probe.cpp).
#include <array>
#include <cstddef>

#include "call_marker.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "long_filter.h"

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
        CATENAE_CONSTINIT catenae::memory_of<example::long_filter_t> s_memory{};
    } // namespace

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        example::long_filter.reset(s_memory.state);
    }

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(example::long_filter.process(s_memory, in, out));
    }

} // namespace catenae_probe

int main() {
    catenae_probe::block in{};
    catenae_probe::block out{};
    in.fill(0.25f);
#if CATENAE_PROBE_STACK
    const std::size_t reset = catenae_probe::high_water([] { catenae_probe::probe_reset(); });
    // Four callbacks: the fourth completes a block, the deepest path.
    std::size_t process = 0;
    for (int call = 0; call < 4; ++call) {
        const std::size_t one = catenae_probe::high_water([&in, &out] { catenae_probe::probe_process(in, out); });
        process               = one > process ? one : process;
    }
    std::printf("CATENAE_STACK reset %lu\n", static_cast<unsigned long>(reset));
    std::printf("CATENAE_STACK process %lu\n", static_cast<unsigned long>(process));
    const std::size_t state     = sizeof(catenae::state_of<example::long_filter_t>);
    const std::size_t workspace = sizeof(catenae::workspace_of<example::long_filter_t>);
    std::printf("CATENAE_SIZEOF state %lu\n", static_cast<unsigned long>(state));
    std::printf("CATENAE_SIZEOF workspace %lu\n", static_cast<unsigned long>(workspace));
    const bool ok = reset < catenae_probe::paint_bytes && process < catenae_probe::paint_bytes;
    std::printf("CATENAE_PROBE_DONE ok=%d\n", ok ? 1 : 0);
#else
    catenae_probe::probe_reset();
    for (int call = 0; call < 4; ++call) {
        catenae_probe::probe_process(in, out);
    }
#endif
    return 0;
}
