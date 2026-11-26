// Copyright 2026 Timothy Place and the catenae contributors
// A q31 biquad run: its contract is declared, its arithmetic arrives with its section scaling
// at C6 (D47).
// Verifies CAT-BIQUAD-006.
// Expect: catenae: a fixed-point biquad needs its section scaling
#include <array>
#include <cstdint>

#include "catenae/biquad.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage_t = catenae::biquad<2>;
    using v_t     = float;
    constexpr stage_t k_stage{
        stage_t::design_type::from_table({{{0.25f, 0.5f, 0.25f, -0.5f, 0.25f}}}, {1.0f, 0.5f, 100, 50, 0.0f})};
#else
    using stage_t                = catenae::biquad_q31<2>;
    using v_t                    = std::int32_t;
    constexpr std::int32_t k_one = std::int32_t{1} << 30;
    constexpr stage_t      k_stage{stage_t::design_type::from_table(
        {{{k_one / 4, k_one / 2, k_one / 4, -k_one / 2, k_one / 4}}}, {1.0f, 0.5f, 100, 50, 0.0f})};
#endif

    std::size_t ran(stage_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept;
    std::size_t ran(stage_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept {
        stage_t::workspace ws{};
        return k_stage.run(st, ws, catenae::span<const v_t>(in), catenae::span<v_t>(out));
    }
} // namespace cf
