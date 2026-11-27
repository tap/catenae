/// @file meters.h
/// @brief A stereo bus's meters, each a tap in one chain: peak, RMS, the EBU's peak programme
/// meter and the correlation of the two channels, at 48 kHz (guide: Meters).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The PPM's numbers were printed in hexadecimal by make(ebu_ppm(48000.0)) on the hosted Linux
// leg's build (GCC 13, glibc), and the battery checks them against make() on every leg, within a
// float ulp where another libm rounds the double design otherwise (test_ppm.cpp,
// ExampleTableIsTheEbuDesign). Nothing here runs a designer: an image of this chain links no
// software double.
#ifndef CATENAE_EXAMPLES_METERS_H
#define CATENAE_EXAMPLES_METERS_H

#include <type_traits>

#include "catenae/chain.h"
#include "catenae/cross_correlation.h"
#include "catenae/peak_meter.h"
#include "catenae/ppm_design.h"
#include "catenae/ppm_meter.h"
#include "catenae/rms_meter.h"
#include "catenae/tap.h"

namespace example {

    // ANCHOR: ppm
    /// make(ebu_ppm(48000.0)): the EBU's Type IIb meter at 48 kHz, its two numbers in ROM.
    inline constexpr auto ebu_ppm_48k = catenae::ppm_design<float>::from_table({0x1.ec524p-8f, 0x1.fffd4ep-1f});
    // ANCHOR_END: ppm

    // ANCHOR: meters
    /// A result every 10 ms (480 frames); peak, RMS and correlation over the last 100 ms.
    using peak_t        = catenae::peak_meter<2, 480, 4800>;
    using rms_t         = catenae::rms_meter<2, 480, 4800>;
    using ppm_t         = catenae::ppm_meter<2, 480>;
    using correlation_t = catenae::cross_correlation<2, 0, 1, 16, 480, 4800>; // left with right, +-16 frames

    /// Each meter a tap: the audio passes through the four unchanged.
    inline constexpr auto bus_meters = catenae::tap<2, peak_t>{} | catenae::tap<2, rms_t>{}
                                       | catenae::tap<2, ppm_t>{ppm_t{ebu_ppm_48k}} | catenae::tap<2, correlation_t>{};
    using bus_meters_t = std::remove_cv_t<decltype(bus_meters)>;

    static_assert(bus_meters_t::is_frame_synchronous && bus_meters_t::exact_latency == (catenae::ratio{0, 1}));
    // One hop for all four, so a new result on one is a new result on each.
    static_assert(peak_t::hop_frames == rms_t::hop_frames && ppm_t::hop_frames == rms_t::hop_frames
                  && correlation_t::hop_frames == rms_t::hop_frames);
    // ANCHOR_END: meters

} // namespace example

#endif // CATENAE_EXAMPLES_METERS_H
