// Copyright 2026 Timothy Place and the catenae contributors
// A call whose expected is ignored: a warning, so an error under catenae's sets.
// Verifies CAT-EXPECTED-004.
// Expect: ignoring return(ed)? value|discarding return value
#include "catenae/expected.h"

namespace cf {
    catenae::expected<int, catenae::status> make(int v) noexcept;
    void                                    discard(int v) noexcept;
} // namespace cf

catenae::expected<int, catenae::status> cf::make(int v) noexcept {
    return v;
}

void cf::discard(int v) noexcept {
#ifdef CATENAE_CF_CONTROL
    static_cast<void>(make(v));
#else
    make(v);
#endif
}
