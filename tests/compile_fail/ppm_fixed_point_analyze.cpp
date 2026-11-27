// Copyright 2026 Timothy Place and the catenae contributors
// A q31 peak programme meter analysing: its contract is declared, its recursion arrives with its
// profile at C6 (D47).
// Verifies CAT-PPM-003.
// Expect: catenae: a fixed-point peak programme meter is new design
#include <array>
#include <cstdint>

#include "catenae/ppm_meter.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using meter_t = catenae::ppm_meter<1, 4>;
    using v_t     = float;
#else
    using meter_t = catenae::ppm_meter_q31<1, 4>;
    using v_t     = std::int32_t;
#endif

    void analysed(const meter_t& m, meter_t::state& st, const std::array<v_t, 4>& in) noexcept;
    void analysed(const meter_t& m, meter_t::state& st, const std::array<v_t, 4>& in) noexcept {
        m.analyze(st, catenae::span<const v_t>(in));
    }
} // namespace cf
