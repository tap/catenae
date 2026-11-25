/// @file test_fwd_alone.h
/// @brief What test_fwd_alone.cpp defines, declared with catenae/fwd.h alone.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TESTS_TEST_FWD_ALONE_H
#define CATENAE_TESTS_TEST_FWD_ALONE_H

#include <cstdint>

#include "catenae/fwd.h"

namespace catenae_test {

    const catenae::span<const float>* pass_span(const catenae::span<const float>* s) noexcept;
    const catenae::expected<catenae::ratio, catenae::status>*
    pass_expected(const catenae::expected<catenae::ratio, catenae::status>* e) noexcept;
    const catenae::port<catenae::fixed<std::int32_t, 31>, 2>*
                    pass_port(const catenae::port<catenae::fixed<std::int32_t, 31>, 2>* p) noexcept;
    catenae::layout planar() noexcept;
    catenae::align  by_caller() noexcept;

} // namespace catenae_test

#endif // CATENAE_TESTS_TEST_FWD_ALONE_H
