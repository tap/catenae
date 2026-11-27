/// @file fir_tables.h
/// @brief Designs that are tables in ROM: the 63-tap low-pass at 0.1 of the sample rate, 60 dB
/// down, in float and in Q15, as `fir_design<S, 63>::make(fir_lowpass{0.1, 60.0})` gives them.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The taps were printed, the float ones in hexadecimal so that every bit is written, by make()
// on the hosted Linux leg's build (GCC 13, glibc's sin, sqrt and pow); the battery checks them
// against make() on every leg, the float taps within a float ulp and the Q15 taps within an LSB
// where another libm rounds the double design differently (test_fir.cpp,
// ATableDesignIsWhatMakeGives). Nothing here runs a designer: an image whose designs are all
// tables links no software double and no libm. The near-zero float taps are the design's zeros
// of sinc, where sin(pi n) in double is not exactly 0; in Q15 they round to 0.
#ifndef CATENAE_EXAMPLES_FIR_TABLES_H
#define CATENAE_EXAMPLES_FIR_TABLES_H

#include <array>
#include <cstdint>

#include "catenae/fir_design.h"

namespace example {

    // ANCHOR: float_taps
    /// The low-pass in float: 63 taps, symmetric, a delay of exactly 31 frames.
    inline constexpr std::array<float, 63> fir_lowpass_taps = {
        0x1.022fep-13f,   -0x1.27bff2p-62f, -0x1.3a116ep-12f, -0x1.679f8ep-11f, -0x1.e9c1fep-11f, -0x1.906b8ap-11f,
        0x1.2f9fbep-60f,  0x1.488a3cp-10f,  0x1.4c28c6p-9f,   0x1.99b4d8p-9f,   0x1.34dc36p-9f,   -0x1.5e1eeap-59f,
        -0x1.bf10dp-9f,   -0x1.ae44b2p-8f,  -0x1.fcc22ap-8f,  -0x1.71e71cp-8f,  0x1.30fb8ap-58f,  0x1.fa245ep-8f,
        0x1.dd3ab6p-7f,   0x1.15d41p-6f,    0x1.8ffdb4p-7f,   -0x1.b5fe14p-58f, -0x1.116bd8p-6f,  -0x1.04cfd8p-5f,
        -0x1.3692dap-5f,  -0x1.cfff1cp-6f,  0x1.0d1dfp-57f,   0x1.6f4d26p-5f,   0x1.93b69ap-4f,   0x1.32dc8ap-3f,
        0x1.7e5a0ep-3f,   0x1.99cf64p-3f,   0x1.7e5a0ep-3f,   0x1.32dc8ap-3f,   0x1.93b69ap-4f,   0x1.6f4d26p-5f,
        0x1.0d1dfp-57f,   -0x1.cfff1cp-6f,  -0x1.3692dap-5f,  -0x1.04cfd8p-5f,  -0x1.116bd8p-6f,  -0x1.b5fe14p-58f,
        0x1.8ffdb4p-7f,   0x1.15d41p-6f,    0x1.dd3ab6p-7f,   0x1.fa245ep-8f,   0x1.30fb8ap-58f,  -0x1.71e71cp-8f,
        -0x1.fcc22ap-8f,  -0x1.ae44b2p-8f,  -0x1.bf10dp-9f,   -0x1.5e1eeap-59f, 0x1.34dc36p-9f,   0x1.99b4d8p-9f,
        0x1.4c28c6p-9f,   0x1.488a3cp-10f,  0x1.2f9fbep-60f,  -0x1.906b8ap-11f, -0x1.e9c1fep-11f, -0x1.679f8ep-11f,
        -0x1.3a116ep-12f, -0x1.27bff2p-62f, 0x1.022fep-13f};
    inline constexpr auto fir_lowpass = catenae::fir_design<float, 63>::from_table(fir_lowpass_taps);
    // ANCHOR_END: float_taps

    // ANCHOR: q15_taps
    /// The same low-pass in Q15: Q1.14 taps (16384 is 1.0) that sum to exactly 16384, unity at DC.
    inline constexpr std::array<std::int16_t, 63> fir_lowpass_q15_taps = {
        2,    0,    -5,   -11, -15, -13,  0,    20,   41,   51,   39,  0,   -56,  -108, -127, -93,
        0,    127,  239,  278, 200, 0,    -273, -522, -621, -464, 0,   735, 1615, 2455, 3059, 3278,
        3059, 2455, 1615, 735, 0,   -464, -621, -522, -273, 0,    200, 278, 239,  127,  0,    -93,
        -127, -108, -56,  0,   39,  51,   41,   20,   0,    -13,  -15, -11, -5,   0,    2};
    inline constexpr auto fir_lowpass_q15 = catenae::fir_design<catenae::q15, 63>::from_table(fir_lowpass_q15_taps);
    // ANCHOR_END: q15_taps

} // namespace example

#endif // CATENAE_EXAMPLES_FIR_TABLES_H
