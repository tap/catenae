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

#include "catenae/chain.h"
#include "catenae/convert.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/interleave.h"
#include "catenae/map.h"
#include "catenae/stage_traits.h"

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

} // namespace
