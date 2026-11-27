/// @file dynamics_tables.h
/// @brief Dynamics designs that are tables in ROM, and the strip they make: an expander, a
/// compressor and a look-ahead limiter, stereo, one chain (guide: Dynamics).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The numbers were printed in hexadecimal, so that every bit is written, by make() on the hosted
// Linux leg's build (GCC 13, glibc's exp and expm1), from the specs beside them; the battery
// checks them against make() on every leg, each within a float ulp where another libm rounds
// the double design otherwise (test_dynamics_design.cpp, ATableDesignIsWhatMakeGives). Nothing
// here runs a designer: an image of these tables links no software double. The times are in
// frames: at 48 kHz, 48 frames is 1 ms.
#ifndef CATENAE_EXAMPLES_DYNAMICS_TABLES_H
#define CATENAE_EXAMPLES_DYNAMICS_TABLES_H

#include <type_traits>

#include "catenae/chain.h"
#include "catenae/compressor.h"
#include "catenae/dynamics_design.h"
#include "catenae/expander.h"
#include "catenae/limiter.h"
#include "catenae/ratio.h"

namespace example {

    // ANCHOR: designs
    /// make({-50 dB, ratio 4, a 6 dB knee, a 40 dB range, attack 48, hold 2400, release 4800}):
    /// below -50 dB the level is pushed down four times as fast, by at most 40 dB.
    inline constexpr auto gate_design = catenae::expander_design<float>::from_table(
        {-0x1.7069e2p+2f, 0x1.61ad54p-2f, 0x1.8p+1f, 0x1.15f2cep+1f, 0x1.26bb1cp+2f, 0x1.f57194p-1f, 0x1.51cd66p-6f,
         0x1.ffe4b2p-1f, 0x1.b4dc74p-13f, 2400u});
    /// make({-18 dB, ratio 4, a 6 dB knee, attack 240, release 4800, make-up 6 dB}).
    inline constexpr auto bus_design = catenae::compressor_design<float>::from_table(
        {-0x1.0942p+1f, 0x1.61ad54p-2f, 0x1.8p-1f, 0x1.15f2cep-1f, 0x1.61ad54p-1f, 0x1.fddfp-1f, 0x1.107fa2p-8f,
         0x1.ffe4b2p-1f, 0x1.b4dc74p-13f});
    /// make({-1 dB, release 2400}): no sample leaves above 0.891 of full scale.
    inline constexpr auto ceiling_design =
        catenae::limiter_design<float>::from_table({-0x1.d791c6p-4f, 0x1.c8520ap-1f, 0x1.ffc966p-1f, 0x1.b4d0cep-12f});
    // ANCHOR_END: designs

    // ANCHOR: strip
    /// The strip: the expander, then the compressor, then the limiter 48 frames ahead.
    inline constexpr auto dynamics_strip = catenae::expander<2>{gate_design} | catenae::compressor<2>{bus_design}
                                           | catenae::limiter<2, 48>{ceiling_design};
    using dynamics_strip_t = std::remove_cv_t<decltype(dynamics_strip)>;

    static_assert(dynamics_strip_t::rate_ratio == (catenae::ratio{1, 1}));
    static_assert(dynamics_strip_t::exact_latency == (catenae::ratio{48, 1})); // the look-ahead, exactly
    // ANCHOR_END: strip

} // namespace example

#endif // CATENAE_EXAMPLES_DYNAMICS_TABLES_H
