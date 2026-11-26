// Copyright 2026 Timothy Place and the catenae contributors
// A q31 mix run: its contract is declared with Q1.30 coefficients, its ramped arithmetic
// arrives with its profile at C6 (D47).
// Verifies CAT-MIX-004.
// Expect: catenae: a fixed-point mix is ramped, new design that arrives with its profile at C6
#include <array>
#include <cstdint>

#include "catenae/chain.h"
#include "catenae/mix.h"

#ifdef CATENAE_CF_CONTROL
using m_t = catenae::mix<2, 1>;
using v_t = float;
constexpr m_t k_mix{{0.5f, 0.5f}, 0};
#else
using m_t = catenae::mix_q31<2, 1>;
using v_t = std::int32_t;
constexpr m_t k_mix{{1 << 29, 1 << 29}, 0}; // one half each, in Q1.30
#endif

std::size_t ran(m_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 2>& out) noexcept;
std::size_t ran(m_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 2>& out) noexcept {
    m_t::workspace ws{};
    return k_mix.run(st, ws, catenae::span<const v_t>(in), catenae::span<v_t>(out));
}
