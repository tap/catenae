/// @file lowpass_table.h
/// @brief A design that is a table in ROM: the order-4 Butterworth low-pass at 0.1 of the
/// sample rate in float, as `butterworth_design<float, 4>::make(0.1)` gives it (PLAN.md 9.2).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The rows and numbers were printed, in hexadecimal so that every bit is written, by make() on
// the hosted Linux leg's build (GCC 13, glibc's tan and sin); the battery checks them against
// make() on every leg, bit for bit there and within a float ulp where another libm rounds the
// double design differently (test_biquad.cpp, ATableDesignIsWhatMakeGives). Nothing here runs a
// designer: an image whose designs are all tables links no software double and no libm.
#ifndef CATENAE_EXAMPLES_LOWPASS_TABLE_H
#define CATENAE_EXAMPLES_LOWPASS_TABLE_H

#include "catenae/biquad_design.h"

namespace example {

    // ANCHOR: table
    /// The order-4 Butterworth at 0.1 in float: its two sections, then its numbers (a nominal
    /// latency of 4.02 frames and 2.51 of deviation, tails of 780 and 69 frames).
    inline constexpr auto lowpass_at_0_1 = catenae::biquad_design<float, 2>::from_table(
        {{{0x1.faf6aap-5f, 0x1.faf6aap-4f, 0x1.faf6aap-5f, -0x1.0c7106p+0f, 0x1.2f3f6ap-2f},
          {0x1.3f4f26p-4f, 0x1.3f4f26p-3f, 0x1.3f4f26p-4f, -0x1.522762p+0f, 0x1.43f656p-1f}}},
        {0x1.015b22p+2f, 0x1.40e36cp+1f, 780, 69, 0x1p-118f});
    // ANCHOR_END: table

} // namespace example

#endif // CATENAE_EXAMPLES_LOWPASS_TABLE_H
