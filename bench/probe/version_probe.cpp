/// @file version_probe.cpp
/// @brief The first size and stack probe: catenae::version() in an image of its own.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Built twice (bench/CMakeLists.txt; scripts/ceilings.py measures both):
//
//   catenae_probe_version  CATENAE_PROBE_STACK=0, the size probe: no stdio, linked with a map,
//                          whose input sections give the library's and this unit's bytes apart
//                          from the runtime's
//   catenae_stack_version  CATENAE_PROBE_STACK=1, the stack probe: prints each call's painted
//                          high-water; on the bare-metal GCC legs also compiled with
//                          -fstack-usage -fcallgraph-info=su, whose frames and call graph give
//                          each call's static worst path from its root, probe_<call>
#include <cstdint>

#include "call_marker.h"
#include "catenae/version.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    CATENAE_PROBE_NOINLINE void probe_version() noexcept;

    namespace {
        volatile std::uint32_t g_sink = 0;
    } // namespace

    // A call's root: noinline, so its frame and everything below it are the call's.
    CATENAE_PROBE_NOINLINE void probe_version() noexcept {
        g_sink = catenae::version();
    }

} // namespace catenae_probe

int main() {
#if CATENAE_PROBE_STACK
    const std::size_t used = catenae_probe::high_water([] { catenae_probe::probe_version(); });
    std::printf("CATENAE_STACK version %lu\n", static_cast<unsigned long>(used));
    std::printf("CATENAE_PROBE_DONE ok=%d\n", used < catenae_probe::paint_bytes ? 1 : 0);
#else
    catenae_probe::probe_version();
#endif
    return 0;
}
