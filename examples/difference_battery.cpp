/// @file difference_battery.cpp
/// @brief The battery of the stage written in difference.h (guide: Writing a stage), run with
/// the library's own on every leg.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

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

    // The card's arithmetic: y[n] = x[n] - x[n - 1], from a reset of all zero.
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

    // The card's Limits: a full-scale tone at Nyquist comes out at twice full scale.
    TEST(difference, PeaksAtTwiceItsInputAtNyquist) {
        constexpr example::difference<1>  stage{};
        example::difference<1>::state     st{};
        example::difference<1>::workspace ws{};
        const std::array<float, 4>        in{1.0f, -1.0f, 1.0f, -1.0f};
        std::array<float, 4>              out{};
        stage.run(st, ws, catenae::span<const float>(in), catenae::span<float>(out));
        const std::array<float, 4> expect{1.0f, -2.0f, 2.0f, -2.0f};
        EXPECT_EQ(std::memcmp(out.data(), expect.data(), sizeof out), 0);
    }

    // The card's Limits: a NaN or an infinity spoils its own frame and the next, and no more.
    TEST(difference, NonFiniteInputSpoilsTwoFrames) {
        constexpr example::difference<1>  stage{};
        example::difference<1>::workspace ws{};
        std::array<float, 4>              out{};

        example::difference<1>::state nan_state{};
        const std::array<float, 4>    nan_in{0.5f, std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f};
        stage.run(nan_state, ws, catenae::span<const float>(nan_in), catenae::span<float>(out));
        EXPECT_EQ(out[0], 0.5f);
        EXPECT_TRUE(std::isnan(out[1]));
        EXPECT_TRUE(std::isnan(out[2]));
        EXPECT_EQ(out[3], 0.0f);

        // inf - 0.5 is +inf; 0.5 - inf is -inf; then the frames are finite again.
        example::difference<1>::state inf_state{};
        const std::array<float, 4>    inf_in{0.5f, std::numeric_limits<float>::infinity(), 0.5f, 0.5f};
        stage.run(inf_state, ws, catenae::span<const float>(inf_in), catenae::span<float>(out));
        EXPECT_EQ(out[0], 0.5f);
        EXPECT_TRUE(std::isinf(out[1]) && out[1] > 0.0f);
        EXPECT_TRUE(std::isinf(out[2]) && out[2] < 0.0f);
        EXPECT_EQ(out[3], 0.0f);
    }
    // ANCHOR_END: battery

} // namespace
