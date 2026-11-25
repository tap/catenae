/// @file test_fwd_alone.cpp
/// @brief A translation unit that sees only catenae/fwd.h (test_fwd.cpp is its other half).
// Copyright 2026 Timothy Place and the catenae contributors
#include "test_fwd_alone.h"

#include "catenae/fwd.h"

namespace catenae_test {

    // Each names a declared type and nothing more: what a consumer's header does with fwd.h.
    const catenae::span<const float>* pass_span(const catenae::span<const float>* s) noexcept {
        return s;
    }
    const catenae::expected<catenae::ratio, catenae::status>*
    pass_expected(const catenae::expected<catenae::ratio, catenae::status>* e) noexcept {
        return e;
    }
    const catenae::port<catenae::fixed<std::int32_t, 31>, 2>*
    pass_port(const catenae::port<catenae::fixed<std::int32_t, 31>, 2>* p) noexcept {
        return p;
    }
    catenae::layout planar() noexcept {
        return catenae::layout::planar;
    }
    catenae::align by_caller() noexcept {
        return catenae::align::by_caller;
    }

} // namespace catenae_test
