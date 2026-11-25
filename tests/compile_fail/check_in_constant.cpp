// Copyright 2026 Timothy Place and the catenae contributors
// A check that fails while a constant is evaluated: check_failed is not constexpr.
// Verifies CAT-CHECK-003.
// Expect: check_failed
#include "catenae/check.h"

namespace cf {
    constexpr int checked(int v) noexcept {
        catenae::detail::check_always(v >= 0, catenae::check::invariant);
        return v;
    }
} // namespace cf

#ifdef CATENAE_CF_CONTROL
static_assert(cf::checked(1) == 1, "a check that holds");
#else
static_assert(cf::checked(-1) == -1, "a check that fails");
#endif
