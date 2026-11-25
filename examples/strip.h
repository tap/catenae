/// @file strip.h
/// @brief The examples' stereo strip: a gain that ramps, a fixed delay, and a halving map.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_EXAMPLES_STRIP_H
#define CATENAE_EXAMPLES_STRIP_H

#include <type_traits>

#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/map.h"

namespace example {

    // ANCHOR: strip
    /// Half of what comes in: a fixed gain needs no class, only a map.
    struct halve {
        constexpr float operator()(float x) const noexcept { return x * 0.5f; }
    };

    // The configuration: a constant. Its type is the topology.
    inline constexpr auto strip =
        catenae::gain<2>{1.0f, 96} | catenae::delay<2, 32>{} | catenae::map<2, halve>{halve{}};
    using strip_t = std::remove_cv_t<decltype(strip)>;
    // ANCHOR_END: strip

    // ANCHOR: proven
    // Proven when the type is formed: every joint is a port<float, 2> meeting a port<float, 2>.
    static_assert(strip_t::is_frame_synchronous, "n frames in, n out, every call");
    static_assert(strip_t::is_in_place, "every stage may write over its input, so the chain may");
    static_assert(strip_t::exact_latency.up == 32 && strip_t::exact_latency.down == 1, "32 frames, exactly");
    // ANCHOR_END: proven

} // namespace example

#endif // CATENAE_EXAMPLES_STRIP_H
