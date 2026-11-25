/// @file c1_probe.cpp
/// @brief The link-stage gate's first probe image (D36, PLAN.md section 7.3): every C1 stage in
/// one float audio path.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gate (scripts/link_gate.py) refuses an image of the float profile that links a
// software-double or double-libm routine. Its probe images are one per float stage family,
// each an audio path built from table designs only; C1's family is the chain's first stages and
// adapters, so this path holds each of them once, in float: gain, delay, map, parallel
// (aligned by type), steady, convert to Q15 and back, and deinterleave then interleave. The
// image is measured as every probe set is (scripts/ceilings.py: sizes, start-up, each call's
// stack and code), and on the Arm keys the gate runs on its size image
// (link_gate.C1ImageHasNoSoftwareDouble).
#include <array>
#include <type_traits>

#include "call_marker.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/convert.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/interleave.h"
#include "catenae/map.h"
#include "catenae/parallel.h"
#include "catenae/steady.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    /// A soft clip at one: x / (1 + |x|), in float.
    struct soft_clip {
        constexpr float operator()(float x) const noexcept { return x / (1.0f + (x < 0.0f ? -x : x)); }
    };

    // The path: stereo float in and out, each C1 stage once.
    constexpr auto c1_path =
        catenae::chain(catenae::gain<2>{0.5f, 32}) | catenae::delay<2, 8>{} | catenae::map<2, soft_clip>{soft_clip{}}
        | catenae::parallel{catenae::gain<1>{2.0f, 0}, catenae::delay<1, 4>{}} | catenae::steady{catenae::delay<2, 2>{}}
        | catenae::convert<float, catenae::q15, 2>{} | catenae::convert<catenae::q15, float, 2>{}
        | catenae::deinterleave<2>{} | catenae::interleave<2>{};
    using c1_path_t = std::remove_cv_t<decltype(c1_path)>;

    /// A callback's block: 64 stereo frames.
    using block = std::array<float, 64 * 2>;

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept;
    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    namespace {
        CATENAE_CONSTINIT catenae::memory_of<c1_path_t> s_memory{};
    } // namespace

    CATENAE_PROBE_NOINLINE void probe_reset() noexcept {
        c1_path.reset(s_memory.state);
    }

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        static_cast<void>(c1_path.process(s_memory, in, out));
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
    // The memory's two parts, as this target lays them out: what the chain remembers and what
    // a call needs.
    std::printf("CATENAE_SIZEOF state %lu\n",
                static_cast<unsigned long>(sizeof(catenae::state_of<catenae_probe::c1_path_t>)));
    std::printf("CATENAE_SIZEOF workspace %lu\n",
                static_cast<unsigned long>(sizeof(catenae::workspace_of<catenae_probe::c1_path_t>)));
    const bool ok = reset < catenae_probe::paint_bytes && process < catenae_probe::paint_bytes;
    std::printf("CATENAE_PROBE_DONE ok=%d\n", ok ? 1 : 0);
#else
    catenae_probe::probe_reset();
    catenae_probe::probe_process(in, out);
#endif
    return 0;
}
