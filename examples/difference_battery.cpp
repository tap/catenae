/// @file difference_battery.cpp
/// @brief The battery of the stage written in difference.h (guide: Writing a stage), run with
/// the library's own on every leg.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstring>

#include <gtest/gtest.h>

#include "catenae_test/harness.h"
#include "difference.h"

namespace {

    // ANCHOR: battery
    // Verifies CAT-STAGE-009, CAT-STAGE-010, CAT-STAGE-011 and CAT-STAGE-012.
    TEST(difference, PassesTheGenericTests) {
        constexpr example::difference<2> stage{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
    }

    // Verifies CAT-STAGE-001: the contract the traits read is the one the card states.
    TEST(difference, IsTheFirstDifference) {
        constexpr example::difference<1>  stage{};
        example::difference<1>::state     st{};
        example::difference<1>::workspace ws{};
        const std::array<float, 4>        in{1.0f, 3.0f, 6.0f, 6.0f};
        std::array<float, 4>              out{};
        EXPECT_EQ(stage.run(st, ws, catenae::span<const float>(in), catenae::span<float>(out)), 4u);
        const std::array<float, 4> expect{1.0f, 2.0f, 3.0f, 0.0f};
        EXPECT_EQ(std::memcmp(out.data(), expect.data(), sizeof out), 0);
    }
    // ANCHOR_END: battery

} // namespace
