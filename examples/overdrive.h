/// @file overdrive.h
/// @brief PLAN.md 9.4's chain as a constant: a tanh soft clip at four times the rate, between
/// half-bands, its latency made whole (guide: Rate and latency).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_EXAMPLES_OVERDRIVE_H
#define CATENAE_EXAMPLES_OVERDRIVE_H

#include <cmath>
#include <type_traits>

#include "catenae/map.h"
#include "catenae/oversample.h"
#include "catenae/ratio.h"

namespace example {

    // ANCHOR: overdrive
    /// The nonlinearity: tanh of the sample, driven.
    struct soft_clip {
        float drive;
        float operator()(float x) const noexcept { return std::tanh(drive * x); }
    };

    // The configuration: a constant. The soft clip runs at four times the rate, between
    // half-bands, and oversample adds the delay that makes its latency whole.
    inline constexpr auto overdrive =
        catenae::oversample<4, catenae::map<2, soft_clip>>{catenae::map<2, soft_clip>{soft_clip{4.0f}}};
    using overdrive_t = std::remove_cv_t<decltype(overdrive)>;

    static_assert(overdrive_t::rate_ratio == (catenae::ratio{1, 1}));
    static_assert(overdrive_t::is_frame_synchronous); // up before down: n frames come back as n
    static_assert(overdrive_t::exact_latency == (catenae::ratio{24, 1}));
    // ANCHOR_END: overdrive

} // namespace example

#endif // CATENAE_EXAMPLES_OVERDRIVE_H
