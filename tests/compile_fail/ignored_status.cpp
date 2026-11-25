// Copyright 2026 Timothy Place and the catenae contributors
// A call whose status is ignored: a warning, so an error under catenae's sets.
// Verifies CAT-EXPECTED-004.
// Expect: ignoring return(ed)? value|discarding return value
#include "catenae/expected.h"

namespace cf {
    catenae::status apply(int v) noexcept;
    void            discard(int v) noexcept;
} // namespace cf

catenae::status cf::apply(int v) noexcept {
    return v < 0 ? catenae::status::invalid_argument : catenae::status::out_of_range;
}

void cf::discard(int v) noexcept {
#ifdef CATENAE_CF_CONTROL
    static_cast<void>(apply(v));
#else
    apply(v);
#endif
}
