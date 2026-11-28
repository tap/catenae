/// @file q31_traits.cpp
/// @brief Each stage's contract instantiated for q31 (D47): compiled on every leg, run on none.
// Copyright 2026 Timothy Place and the catenae contributors
//
// From C1 every stage is a template on its sample format, and CI instantiates its contract,
// not its arithmetic, for q31: stage_traits reads every member and asserts it. A stage whose
// fixed-point arithmetic is new design (gain, D47) is here with the rest; its run() is not
// instantiated until its profile arrives at C6.
// Verifies CAT-STAGE-001.
#include <cstdint>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/autocorrelation.h"
#include "catenae/biquad.h"
#include "catenae/chain.h"
#include "catenae/compressor.h"
#include "catenae/convert.h"
#include "catenae/cross_correlation.h"
#include "catenae/delay.h"
#include "catenae/dither.h"
#include "catenae/expander.h"
#include "catenae/fft_fir.h"
#include "catenae/fir.h"
#include "catenae/gain.h"
#include "catenae/handoff.h"
#include "catenae/interleave.h"
#include "catenae/junction_traits.h"
#include "catenae/limiter.h"
#include "catenae/loudness_meter.h"
#include "catenae/map.h"
#include "catenae/mix.h"
#include "catenae/pack.h"
#include "catenae/parallel.h"
#include "catenae/peak_meter.h"
#include "catenae/planar_section.h"
#include "catenae/ppm_meter.h"
#include "catenae/resample.h"
#include "catenae/rms_meter.h"
#include "catenae/stage_traits.h"
#include "catenae/steady.h"
#include "catenae/stft.h"
#include "catenae/tap.h"
#include "catenae/true_peak_meter.h"
#include "catenae/unpack.h"

namespace {

    struct negate {
        constexpr std::int32_t operator()(std::int32_t x) const noexcept { return x == INT32_MIN ? INT32_MAX : -x; }
    };

    static_assert(negate{}(5) == -5, "the map's function, over Q31 storage");

    template <typename Stage>
    constexpr bool contract() noexcept {
        return catenae::stage_traits<Stage>::opted_in;
    }

    static_assert(contract<catenae::map_q31<2, negate>>(), "map in q31");
    static_assert(contract<catenae::gain_q31<2>>(), "gain in q31: its contract");
    static_assert(contract<catenae::delay_q31<2, 16>>(), "delay in q31");
    static_assert(contract<catenae::deinterleave_q31<2>>(), "deinterleave in q31");
    static_assert(contract<catenae::interleave_q31<2>>(), "interleave in q31");
    static_assert(contract<catenae::convert<catenae::q31, float, 2>>(), "convert from q31");
    static_assert(contract<catenae::convert<float, catenae::q31, 2>>(), "convert to q31");
    static_assert(contract<catenae::chain<catenae::gain_q31<2>, catenae::delay_q31<2, 4>>>(), "a q31 chain");
    static_assert(contract<catenae::steady<catenae::delay_q31<2, 4>>>(), "steady around q31");
    static_assert(contract<catenae::parallel<catenae::delay_q31<1, 4>, catenae::gain_q31<2>>>(), "parallel in q31");
    static_assert(contract<catenae::parallel<catenae::deinterleave_q31<2>, catenae::deinterleave_q31<1>,
                                             catenae::align::by_caller>>(),
                  "parallel in q31, planar out, aligned by the caller");
    static_assert(contract<catenae::biquad_q31<2>>(), "biquad in q31: its contract");
    static_assert(contract<catenae::biquad_q31<1, 4>>(), "a cascade of four in q31: its contract");
    static_assert(contract<catenae::dither_q31<2, 24>>(), "dither in q31");
    static_assert(contract<catenae::fir_q31<2, 63>>(), "fir in q31, its taps by value");
    static_assert(contract<catenae::fir_q31<1, 255>>(), "fir in q31, its taps viewed");
    static_assert(contract<catenae::fir_any_q31<2, 16>>(), "any FIR in q31");
    static_assert(contract<catenae::fir_q4_27<2, 63>>(), "fir in Q4.27");
    static_assert(contract<catenae::up2_q31<2>>(), "up2 in q31");
    static_assert(contract<catenae::down2_q31<2, 2>>(), "down2 in q31, relaxed");
    static_assert(contract<catenae::up2_q4_27<2>>(), "up2 in Q4.27");
    static_assert(contract<catenae::chain<catenae::up2_q31<2>, catenae::down2_q31<2>>>(), "a q31 round trip");
    static_assert(contract<catenae::chain<catenae::fir_q31<2, 31>, catenae::delay_q31<2, 4>>>(), "fir in a q31 chain");
    static_assert(contract<catenae::mix_q31<2, 1>>(), "mix in q31: its contract, Q1.30 coefficients");
    static_assert(contract<catenae::compressor_q31<2>>(), "compressor in q31: its contract");
    static_assert(contract<catenae::expander_q31<2>>(), "expander in q31: its contract");
    static_assert(contract<catenae::limiter_q31<2, 32>>(), "limiter in q31: its contract");
    static_assert(contract<catenae::fft_fir_q31<2, 255>>(), "the FFT FIR in q31: its contract");
    static_assert(contract<catenae::steady<catenae::fft_fir_q31<1, 31>>>(), "the FFT FIR in q31, steady");
    static_assert(contract<catenae::planar_section<2, catenae::gain_q31<1>, catenae::delay_q31<1, 4>>>(),
                  "planar_section in q31: its contract");
    static_assert(contract<catenae::chain<catenae::compressor_q31<2>, catenae::limiter_q31<2, 8>>>(),
                  "dynamics in a q31 chain");
    static_assert(contract<catenae::unpack<catenae::wire::s24le_packed, 2>>(), "unpack to q31");
    static_assert(contract<catenae::pack<catenae::wire::s32be, 2>>(), "pack from q31");
    static_assert(contract<catenae::chain<catenae::unpack<catenae::wire::s24le_in32_msb, 2>, catenae::gain_q31<2>,
                                          catenae::pack<catenae::wire::s24be_packed, 2>>>(),
                  "wire to wire through q31");
    static_assert(catenae::junction_traits<catenae::handoff<catenae::q31, 2, 256, 64, 64>>::opted_in,
                  "a handoff of q31 frames: the junction's contract");
    // The analyzers: meters and correlation run in q31 at C5 (D47); the peak programme meter's
    // recursion arrives at C6, its contract here.
    static_assert(contract<catenae::tap<2, catenae::peak_meter_q31<2, 64>>>(), "a q31 peak meter's tap");
    static_assert(contract<catenae::tap<2, catenae::rms_meter_q31<2, 64, 256>>>(), "a q31 RMS meter's tap");
    static_assert(contract<catenae::tap<2, catenae::ppm_meter_q31<2, 64>>>(), "a q31 PPM's tap: its contract");
    static_assert(contract<catenae::tap<2, catenae::autocorrelation_q31<2, 8, 64>>>(), "q31 autocorrelation");
    static_assert(contract<catenae::tap<2, catenae::cross_correlation_q31<2, 0, 1, 8, 64>>>(), "q31 cross-correlation");
    static_assert(catenae::analyzer_traits<catenae::ppm_meter_q31<1, 32>>::opted_in, "the q31 PPM, an analyzer");
    // True peak runs in q31 at C5, its sums exact; loudness's K-weighting arrives at C6, its
    // contract here (a Q2.30 table).
    static_assert(contract<catenae::tap<2, catenae::true_peak_meter_q31<2, 64>>>(), "a q31 true-peak meter's tap");
    static_assert(contract<catenae::tap<2, catenae::loudness_meter_q31<2, 960, 5>>>(),
                  "a q31 loudness meter's tap: its contract");
    static_assert(catenae::analyzer_traits<catenae::loudness_meter_q31<1, 96, 1>>::opted_in,
                  "the q31 loudness meter, an analyzer");
    static_assert(
        std::is_same<catenae::loudness_meter_q31<1, 96, 1>::design_type::coefficient_type, std::int32_t>::value,
        "a Q2.30 K-weighting table");
    // The STFT's contract in q31; its windows and spectra arrive at C6 (D38, D47).
    static_assert(contract<catenae::stft_q31<512, 128, 2>>(), "the STFT in q31: its contract");
    static_assert(contract<catenae::istft_q31<512, 128, 2>>(), "the inverse STFT in q31: its contract");
    static_assert(contract<catenae::chain<catenae::stft_q31<64, 16, 1>, catenae::istft_q31<64, 16, 1>>>(),
                  "the STFT pair in a q31 chain");
    static_assert(std::is_same<catenae::window_design<catenae::q31, 512>::value_type, std::int32_t>::value,
                  "a q31 window of raw integers");

} // namespace
