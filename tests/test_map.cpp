/// @file test_map.cpp
/// @brief map's contract (include/catenae/map.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "catenae/map.h"
#include "catenae_test/harness.h"

namespace {

    struct times {
        float           factor;
        constexpr float operator()(float x) const noexcept { return x * factor; }
    };
    struct negate_q31 {
        constexpr std::int32_t operator()(std::int32_t x) const noexcept { return x == INT32_MIN ? INT32_MAX : -x; }
    };

    // Verifies CAT-MAP-001.
    TEST(map, AppliesItsFunctionToEveryValue) {
        constexpr catenae::map<2, times> m{times{0.5f}};
        using m_t = catenae::map<2, times>;
        m_t::state           st{};
        m_t::workspace       ws{};
        std::array<float, 6> in{1.0f, -2.0f, 3.0f, -4.0f, 5.0f, 0.25f};
        std::array<float, 6> out{};
        EXPECT_EQ(m.run(st, ws, catenae::span<const float>(in), catenae::span<float>(out)), 3u);
        EXPECT_FLOAT_EQ(out[1], -1.0f);
        EXPECT_FLOAT_EQ(out[5], 0.125f);
        std::array<float, 6> same = in;
        EXPECT_EQ(m.run(st, ws, catenae::span<const float>(same), catenae::span<float>(same)), 3u);
        EXPECT_EQ(std::memcmp(same.data(), out.data(), sizeof out), 0);
        EXPECT_TRUE(catenae_test::reset_ignores_storage(m));
        EXPECT_TRUE(catenae_test::counts_are_exact(m));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(m, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(m));
        constexpr catenae::map_q31<1, negate_q31> q{negate_q31{}};
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(q));
        static_assert(catenae::stage_traits<m_t>::is_in_place && catenae::stage_traits<m_t>::rests_at_zero,
                      "in place, nothing to remember");
    }

} // namespace
