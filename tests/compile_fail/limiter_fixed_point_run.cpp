// Copyright 2026 Timothy Place and the catenae contributors
// A q31 limiter run: its contract is declared, its arithmetic arrives with its profile at C6
// (D47).
// Verifies CAT-LIMITER-005.
// Expect: catenae: a fixed-point limiter is new design
#include <array>
#include <cstdint>

#include "catenae/limiter.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage_t = catenae::limiter<2, 8>;
    using v_t     = float;
#else
    using stage_t = catenae::limiter_q31<2, 8>;
    using v_t     = std::int32_t;
#endif
    constexpr stage_t k_stage{stage_t::design_type::from_table({-0.1f, 0.9f, 0.99f, 0.01f})};

    std::size_t ran(stage_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept;
    std::size_t ran(stage_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept {
        stage_t::workspace ws{};
        return k_stage.run(st, ws, catenae::span<const v_t>(in), catenae::span<v_t>(out));
    }
} // namespace cf
