/// @file recursive_probe.cpp
/// @brief The link-stage gate's probe image for the recursive family (D36, PLAN.md 7.3, 9.2):
/// a biquad cascade from a table in ROM and a gain, as 9.2's strip has them.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gate (scripts/link_gate.py) refuses an image of the float profile that links a
// software-double or double-libm routine. This image's design is a table
// (examples/lowpass_table.h), so no designer is reachable: the audio path is the header
// kernel alone. The designers' cost is the designers probe's (designers_probe.cpp).
#include <array>
#include <cstddef>
#include <type_traits>

#include "call_marker.h"
#include "catenae/biquad.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/gain.h"
#include "lowpass_table.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    // The strip: the order-4 Butterworth at 0.1, from its table, then a gain that ramps.
    constexpr auto recursive_path =
        catenae::chain(catenae::butterworth<2, 4>{example::lowpass_at_0_1}) | catenae::gain<2>{1.0f, 96};
    using recursive_path_t = std::remove_cv_t<decltype(recursive_path)>;

    /// A callback's block: 64 stereo frames.
    using block = std::array<float, 64 * 2>;

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept;
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    namespace {
        CATENAE_CONSTINIT catenae::memory_of<recursive_path_t> s_memory{};
    } // namespace

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        recursive_path.reset(s_memory.state);
    }

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(recursive_path.process(s_memory, in, out));
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
    const std::size_t state     = sizeof(catenae::state_of<catenae_probe::recursive_path_t>);
    const std::size_t workspace = sizeof(catenae::workspace_of<catenae_probe::recursive_path_t>);
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
