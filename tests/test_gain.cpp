/// @file test_gain.cpp
/// @brief gain's contract (include/catenae/gain.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/gain.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T>
    class gain_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(gain_formats, formats, ); // the empty name generator: Clang asks for the third argument

    template <typename T>
    std::vector<T> ones(std::size_t values) {
        return std::vector<T>(values, T{1});
    }

    // Verifies CAT-GAIN-001.
    TYPED_TEST(gain_formats, AtRestEveryFrameTimesTheLevel) {
        using g_t = catenae::basic_gain<TypeParam, 3>;
        constexpr g_t           g{static_cast<TypeParam>(0.25), 16};
        typename g_t::state     st;
        typename g_t::workspace ws{};
        std::memset(&st, 0xA5, sizeof st);
        catenae::stage_traits<g_t>::reset(g, st);
        const std::vector<TypeParam> in = {
            TypeParam{1}, TypeParam{-2}, TypeParam{4}, TypeParam{8}, static_cast<TypeParam>(0.5), TypeParam{-1}};
        std::vector<TypeParam> out(in.size());
        EXPECT_EQ(g.run(st, ws, catenae::span<const TypeParam>(in), catenae::span<TypeParam>(out)), 2u);
        for (std::size_t i = 0; i < in.size(); ++i) {
            EXPECT_EQ(out[i], in[i] * static_cast<TypeParam>(0.25)) << i;
        }
        std::vector<TypeParam> same = in;
        EXPECT_EQ(g.run(st, ws, catenae::span<const TypeParam>(same), catenae::span<TypeParam>(same)), 2u);
        EXPECT_EQ(std::memcmp(same.data(), out.data(), same.size() * sizeof(TypeParam)), 0);
    }

    // Verifies CAT-GAIN-002.
    TYPED_TEST(gain_formats, ANewLevelIsReachedOverTheRamp) {
        using g_t = catenae::basic_gain<TypeParam, 1>;
        g_t                     g{TypeParam{1}, 8};
        typename g_t::state     st{};
        typename g_t::workspace ws{};
        catenae::stage_traits<g_t>::reset(g, st);
        g.set_level(static_cast<TypeParam>(0.2));
        const std::vector<TypeParam> in = ones<TypeParam>(12);
        std::vector<TypeParam>       out(12);
        static_cast<void>(g.run(st, ws, catenae::span<const TypeParam>(in), catenae::span<TypeParam>(out)));
        const TypeParam step = (static_cast<TypeParam>(0.2) - TypeParam{1}) / TypeParam{8};
        for (std::uint32_t k = 1; k <= 8; ++k) {
            EXPECT_EQ(out[k - 1], static_cast<TypeParam>(0.2) - step * static_cast<TypeParam>(8 - k)) << k;
        }
        EXPECT_EQ(out[7], static_cast<TypeParam>(0.2)); // the goal, exactly
        for (std::size_t k = 1; k < 8; ++k) {
            EXPECT_LT(out[k], out[k - 1]); // monotonic
        }
        EXPECT_EQ(out[11], static_cast<TypeParam>(0.2));
        // A ramp of no frames jumps.
        g_t jump{TypeParam{1}, 0};
        catenae::stage_traits<g_t>::reset(jump, st);
        jump.set_level(TypeParam{3});
        static_cast<void>(jump.run(st, ws, catenae::span<const TypeParam>(in), catenae::span<TypeParam>(out)));
        EXPECT_EQ(out[0], TypeParam{3});
    }

    // Verifies CAT-GAIN-003.
    TYPED_TEST(gain_formats, ARampIsTheSameInAnyChunking) {
        using g_t = catenae::basic_gain<TypeParam, 2>;
        // The level differs from what reset leaves only if it changes after reset; a changed
        // configuration copied into the harness's driver starts a ramp at its first run.
        g_t g{TypeParam{1}, 37};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(g));
        EXPECT_TRUE(catenae_test::counts_are_exact(g));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(g, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(g));
        // A ramp across calls: one run of 100 against calls of 1 to 9.
        typename g_t::workspace      ws{};
        const std::vector<TypeParam> in = ones<TypeParam>(2 * 100);
        std::vector<TypeParam>       whole(in.size());
        std::vector<TypeParam>       pieces(in.size());
        typename g_t::state          a{};
        typename g_t::state          b{};
        catenae::stage_traits<g_t>::reset(g, a);
        catenae::stage_traits<g_t>::reset(g, b);
        g.set_level(static_cast<TypeParam>(0.5));
        static_cast<void>(g.run(a, ws, catenae::span<const TypeParam>(in), catenae::span<TypeParam>(whole)));
        std::size_t at = 0;
        for (const std::size_t n : catenae_test::chunks(100, 9, 3)) {
            static_cast<void>(g.run(b, ws, catenae::span<const TypeParam>(in.data() + 2 * at, 2 * n),
                                    catenae::span<TypeParam>(pieces.data() + 2 * at, 2 * n)));
            at += n;
        }
        EXPECT_EQ(std::memcmp(whole.data(), pieces.data(), whole.size() * sizeof(TypeParam)), 0);
    }

} // namespace
