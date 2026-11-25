// Copyright 2026 Timothy Place and the catenae contributors
// A stereo chain feeding a mono junction: the chain's output port is not the sink port.
// Verifies CAT-FEEDING-001.
// Expect: catenae: one stage's output and the next stage's input have different channel counts
#include <cstddef>

#include "catenae/feeding.h"
#include "catenae/gain.h"
#include "catenae_fixtures/edges.h"

#ifdef CATENAE_CF_CONTROL
using ring_t = catenae_fixtures::ring_junction<2, 256>;
#else
using ring_t = catenae_fixtures::ring_junction<1, 256>;
#endif

constexpr auto k_stereo = catenae::chain(catenae::gain<2>{0.5f, 0});

std::size_t into_ring(const ring_t& ring, ring_t::state& ring_state, catenae::feeding_state<decltype(k_stereo)>& st,
                      catenae::feeding_workspace<decltype(k_stereo)>& ws, catenae::span<const float> in) noexcept;
std::size_t into_ring(const ring_t& ring, ring_t::state& ring_state, catenae::feeding_state<decltype(k_stereo)>& st,
                      catenae::feeding_workspace<decltype(k_stereo)>& ws, catenae::span<const float> in) noexcept {
    return catenae::feeding(ring, ring_state, k_stereo, st, ws, in);
}
