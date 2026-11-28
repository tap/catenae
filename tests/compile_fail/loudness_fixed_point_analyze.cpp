// Copyright 2026 Timothy Place and the catenae contributors
// A q31 loudness meter analysing: its contract is declared, its K-weighting's recursion arrives
// with its profile at C6 (D47).
// Verifies CAT-LOUDNESS-004.
// Expect: catenae: a fixed-point loudness meter's K-weighting is new design
#include <array>
#include <cstdint>

#include "catenae/loudness_meter.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using meter_t = catenae::loudness_meter<1, 480, 10>;
    using v_t     = float;
#else
    using meter_t = catenae::loudness_meter_q31<1, 480, 10>;
    using v_t     = std::int32_t;
#endif

    void analysed(const meter_t& m, meter_t::state& st, const std::array<v_t, 4>& in) noexcept;
    void analysed(const meter_t& m, meter_t::state& st, const std::array<v_t, 4>& in) noexcept {
        m.analyze(st, catenae::span<const v_t>(in));
    }
} // namespace cf
