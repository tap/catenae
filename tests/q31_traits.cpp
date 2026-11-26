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
#include "catenae/dither.h"
#include "catenae/gain.h"
#include "catenae/handoff.h"
#include "catenae/interleave.h"
#include "catenae/junction_traits.h"
#include "catenae/map.h"
#include "catenae/mix.h"
#include "catenae/pack.h"
#include "catenae/parallel.h"
#include "catenae/stage_traits.h"
#include "catenae/steady.h"
#include "catenae/unpack.h"

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
    static_assert(contract<catenae::steady<catenae::delay_q31<2, 4>>>(), "steady around q31");
    static_assert(contract<catenae::parallel<catenae::delay_q31<1, 4>, catenae::gain_q31<2>>>(), "parallel in q31");
    static_assert(contract<catenae::parallel<catenae::deinterleave_q31<2>, catenae::deinterleave_q31<1>,
                                             catenae::align::by_caller>>(),
                  "parallel in q31, planar out, aligned by the caller");
    static_assert(contract<catenae::dither_q31<2, 24>>(), "dither in q31");
    static_assert(contract<catenae::mix_q31<2, 1>>(), "mix in q31: its contract, Q1.30 coefficients");
    static_assert(contract<catenae::unpack<catenae::wire::s24le_packed, 2>>(), "unpack to q31");
    static_assert(contract<catenae::pack<catenae::wire::s32be, 2>>(), "pack from q31");
    static_assert(contract<catenae::chain<catenae::unpack<catenae::wire::s24le_in32_msb, 2>, catenae::gain_q31<2>,
                                          catenae::pack<catenae::wire::s24be_packed, 2>>>(),
                  "wire to wire through q31");
    static_assert(catenae::junction_traits<catenae::handoff<catenae::q31, 2, 256, 64, 64>>::opted_in,
                  "a handoff of q31 frames: the junction's contract");

} // namespace
