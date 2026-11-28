/// @file loudness.h
/// @brief A stereo programme's loudness and true peak, each a tap in one chain, at 48 kHz, as
/// ITU-R BS.1770-4 and EBU Tech 3341 measure them (guide: Loudness and true peak).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The K-weighting is BS.1770-4's Tables 1 and 2 in float, with the numbers make(48000.0) stores
// beside them (printed by it on the hosted Linux leg's build, GCC 13, glibc); the battery checks
// the table against make() on every leg (test_loudness.cpp, ExampleTableIsTheStandards). Nothing
// here runs a designer: an image of this chain links no software double.
#ifndef CATENAE_EXAMPLES_LOUDNESS_H
#define CATENAE_EXAMPLES_LOUDNESS_H

#include <type_traits>

#include "catenae/biquad_design.h"
#include "catenae/chain.h"
#include "catenae/loudness_meter.h"
#include "catenae/tap.h"
#include "catenae/true_peak_meter.h"

namespace example {

    // ANCHOR: k_weighting
    /// k_weighting_design<float>::make(48000.0): the standard's own two sections, in ROM.
    inline constexpr auto k_weighting_48k = catenae::biquad_design<float, 2>::from_table(
        {{{1.53512485958697f, -2.69169618940638f, 1.19839281085285f, -1.69065929318241f, 0.73248077421585f},
          {1.0f, -2.0f, 1.0f, -1.99004745483398f, 0.99007225036621f}}},
        {-0x1.4e85b2p+0f, 0x1.259ddcp+1f, 36967, 4578, 0x1p-118f});
    // ANCHOR_END: k_weighting

    // ANCHOR: meters
    /// A result every 20 ms (960 frames, five to BS.1770-4's 100 ms step): loudness momentary,
    /// short-term and integrated, left and right weighed 1; true peak over the last second.
    using loudness_t  = catenae::loudness_meter<2, 960, 5>;
    using true_peak_t = catenae::true_peak_meter<2, 960, 48000>;

    /// Each meter a tap: the audio passes through both unchanged.
    inline constexpr auto programme_meters =
        catenae::tap<2, loudness_t>{loudness_t::from_table(k_weighting_48k, {1.0f, 1.0f})}
        | catenae::tap<2, true_peak_t>{};
    using programme_meters_t = std::remove_cv_t<decltype(programme_meters)>;

    static_assert(programme_meters_t::is_frame_synchronous
                  && programme_meters_t::exact_latency == (catenae::ratio{0, 1}));
    static_assert(loudness_t::hop_frames == true_peak_t::hop_frames, "one hop for both");
    // ANCHOR_END: meters

} // namespace example

#endif // CATENAE_EXAMPLES_LOUDNESS_H
