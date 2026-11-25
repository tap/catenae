// Copyright 2026 Timothy Place and the catenae contributors
// A junction written into a chain's type.
// Verifies CAT-CHAIN-001.
// Expect: catenae: a junction is never inside a chain
#include "catenae/gain.h"
#include "cf_junction.h"

namespace catenae {
    template <>
    struct is_junction<cf::junction> : std::true_type {};
} // namespace catenae

#ifdef CATENAE_CF_CONTROL
static_assert(catenae::chain<catenae::gain<1>>::input_group == 1, "a chain of a stage");
#else
static_assert(catenae::chain<catenae::gain<1>, cf::junction>::input_group == 1, "a chain of a junction");
#endif
