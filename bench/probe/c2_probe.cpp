/// @file c2_probe.cpp
/// @brief The link-stage gate's probe image for C2's family (D36, PLAN.md section 7.3): the
/// edges in one audio path, bytes in and bytes out, float between.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gate (scripts/link_gate.py) refuses an image of the float profile that links a
// software-double or double-libm routine. C2's family is the edges, so this path is 24-bit
// packed stereo off the wire, unpacked to q31, converted to float for a gain, and back to q31
// and the wire: the compiled loops of src/wire.cpp with the header stages around them. The
// image is measured as every probe set is (scripts/ceilings.py), and on the Arm keys the gate
// runs on its size image (link_gate.C2ImageHasNoSoftwareDouble).
#include <array>
#include <cstddef>
#include <type_traits>

#include "call_marker.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/convert.h"
#include "catenae/gain.h"
#include "catenae/pack.h"
#include "catenae/unpack.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    namespace wire = catenae::wire;

    // The path: 24-bit packed little-endian stereo in and out, the gain in float.
    constexpr auto c2_path = catenae::chain(catenae::unpack<wire::s24le_packed, 2>{})
                             | catenae::convert<catenae::q31, float, 2>{} | catenae::gain<2>{0.5f, 32}
                             | catenae::convert<float, catenae::q31, 2>{} | catenae::pack<wire::s24le_packed, 2>{};
    using c2_path_t = std::remove_cv_t<decltype(c2_path)>;

    /// A callback's block: 64 stereo frames of three-byte samples.
    using block = std::array<std::byte, 64 * 2 * 3>;

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept;
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    namespace {
        CATENAE_CONSTINIT catenae::memory_of<c2_path_t> s_memory{};
    } // namespace

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        c2_path.reset(s_memory.state);
    }

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(c2_path.process(s_memory, in, out));
    }

} // namespace catenae_probe

int main() {
    catenae_probe::block in{};
    catenae_probe::block out{};
    in.fill(std::byte{0x40});
#if CATENAE_PROBE_STACK
    const std::size_t reset   = catenae_probe::high_water([] { catenae_probe::probe_reset(); });
    const std::size_t process = catenae_probe::high_water([&in, &out] { catenae_probe::probe_process(in, out); });
    std::printf("CATENAE_STACK reset %lu\n", static_cast<unsigned long>(reset));
    std::printf("CATENAE_STACK process %lu\n", static_cast<unsigned long>(process));
    // The memory's two parts, as this target lays them out: what the chain remembers and what
    // a call needs.
    const std::size_t state     = sizeof(catenae::state_of<catenae_probe::c2_path_t>);
    const std::size_t workspace = sizeof(catenae::workspace_of<catenae_probe::c2_path_t>);
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
