/// @file test_fwd.cpp
/// @brief fwd.h's contract (include/catenae/fwd.h).
// Copyright 2026 Timothy Place and the catenae contributors
//
// test_fwd_alone.cpp declares and defines functions over catenae's types having included only
// fwd.h; this file includes the defining headers and calls them. That it links is the proof
// that each forward declaration names the defined entity, inline namespace and all.
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/expected.h"
#include "catenae/fwd.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "test_fwd_alone.h"

namespace {

    // Verifies CAT-FWD-001.
    TEST(fwd, DeclaresTheDefinedTypes) {
        const float                                              values[2] = {1.0f, 2.0f};
        const catenae::span<const float>                         s(values);
        const catenae::expected<catenae::ratio, catenae::status> e = catenae::ratio{1, 2};
        const catenae::port<catenae::q31, 2>                     p{};
        EXPECT_EQ(catenae_test::pass_span(&s), &s);
        EXPECT_EQ(catenae_test::pass_expected(&e), &e);
        EXPECT_EQ(catenae_test::pass_port(&p), &p);
        EXPECT_EQ(catenae_test::planar(), catenae::layout::planar);
        // fwd.h gives the default arguments, the defining headers use them.
        static_assert(std::is_same<catenae::span<float>, catenae::span<float, catenae::dynamic_extent>>::value,
                      "span's default extent");
        static_assert(
            std::is_same<catenae::port<float, 2>, catenae::port<float, 2, catenae::layout::interleaved>>::value,
            "port's default layout");
    }

} // namespace
