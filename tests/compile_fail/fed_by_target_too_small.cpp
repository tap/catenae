// Copyright 2026 Timothy Place and the catenae contributors
// A pull side for requests of up to 256 frames behind a handoff fed 64 at a time whose target is
// 128: it needs 256 + 64 - 1 (PLAN.md 9.6), so the pull side does not compile; 320 does.
// Verifies CAT-FED-BY-003.
// Expect: catenae: the junction does not serve this pull side
#include <cstddef>

#include "catenae/fed_by.h"
#include "catenae/gain.h"
#include "catenae/handoff.h"

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t k_target = 320;
#else
constexpr std::size_t k_target = 128;
#endif

constexpr auto k_level = catenae::chain(catenae::gain<1>{0.5f, 0});
using link_t           = catenae::handoff<float, 1, 1024, k_target, 64>;

std::size_t pull_side(const link_t& link, link_t::state& link_state, catenae::fed_by_state<decltype(k_level)>& st,
                      catenae::fed_by_workspace<decltype(k_level), 256>& ws, catenae::span<float> out) noexcept;
std::size_t pull_side(const link_t& link, link_t::state& link_state, catenae::fed_by_state<decltype(k_level)>& st,
                      catenae::fed_by_workspace<decltype(k_level), 256>& ws, catenae::span<float> out) noexcept {
    return catenae::fed_by(link, link_state, k_level, st, ws, out);
}
