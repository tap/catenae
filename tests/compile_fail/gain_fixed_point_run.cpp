// Copyright 2026 Timothy Place and the catenae contributors
// A q31 gain run: its contract is declared, its arithmetic arrives with its profile at C6 (D47).
// Verifies CAT-GAIN-001.
// Expect: catenae: a fixed-point gain is new design and arrives with its profile at C6
#include <array>
#include <cstdint>

#include "catenae/chain.h"
#include "catenae/gain.h"

#ifdef CATENAE_CF_CONTROL
using g_t = catenae::gain<2>;
using v_t = float;
constexpr g_t k_gain{0.5f, 0};
#else
using g_t = catenae::gain_q31<2>;
using v_t = std::int32_t;
constexpr g_t k_gain{0.5f, 0};
#endif

std::size_t ran(g_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept;
std::size_t ran(g_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept {
    g_t::workspace ws{};
    return k_gain.run(st, ws, catenae::span<const v_t>(in), catenae::span<v_t>(out));
}
