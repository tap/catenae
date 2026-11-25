/// @file test_delay.cpp
/// @brief delay's contract (include/catenae/delay.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/delay.h"
#include "catenae/sample_format.h"
#include "catenae_test/harness.h"

namespace {

    template <typename S>
    class delay_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double, catenae::q15, catenae::q31, catenae::q4_27>;
    TYPED_TEST_SUITE(delay_formats, formats, ); // the empty name generator: Clang asks for the third argument

    // Verifies CAT-DELAY-001.
    TYPED_TEST(delay_formats, OutputIsTheInputFramesLater) {
        using d_t        = catenae::basic_delay<TypeParam, 3, 7>;
        using value_type = catenae::storage_t<TypeParam>;
        constexpr d_t           d{};
        const auto              in = catenae_test::input_for<d_t>(200, 9);
        std::vector<value_type> out(in.size());
        typename d_t::state     st{};
        typename d_t::workspace ws{};
        catenae::stage_traits<d_t>::reset(d, st);
        std::size_t at = 0;
        for (const std::size_t n : catenae_test::chunks(200, 13, 4)) {
            EXPECT_EQ(d.run(st, ws, catenae::span<const value_type>(in.data() + at * 3, n * 3),
                            catenae::span<value_type>(out.data() + at * 3, n * 3)),
                      n);
            at += n;
        }
        const value_type zero{};
        for (std::size_t i = 0; i < 7 * 3; ++i) {
            ASSERT_EQ(std::memcmp(&out[i], &zero, sizeof zero), 0) << i;
        }
        EXPECT_EQ(std::memcmp(out.data() + 7 * 3, in.data(), (200 - 7) * 3 * sizeof(value_type)), 0);
        // In place: the same.
        std::vector<value_type> same = in;
        catenae::stage_traits<d_t>::reset(d, st);
        EXPECT_EQ(d.run(st, ws, catenae::span<const value_type>(same), catenae::span<value_type>(same)), 200u);
        EXPECT_EQ(std::memcmp(same.data(), out.data(), same.size() * sizeof(value_type)), 0);
        EXPECT_TRUE(catenae_test::reset_ignores_storage(d));
        EXPECT_TRUE(catenae_test::counts_are_exact(d));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(d, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(d));
    }

    // Verifies CAT-DELAY-002.
    TEST(delay, LatencyAndTailAreItsFrames) {
        using d_t = catenae::delay<2, 48>;
        static_assert(catenae::stage_traits<d_t>::has_exact_latency
                          && catenae::stage_traits<d_t>::exact_latency.up == 48
                          && catenae::stage_traits<d_t>::exact_latency.down == 1,
                      "exactly 48 frames, a constant of the type");
        constexpr d_t d{};
        EXPECT_FLOAT_EQ(d.latency().nominal, 48.0f);
        EXPECT_FLOAT_EQ(d.latency().deviation, 0.0f);
        EXPECT_EQ(d.tail_frames(), 48u);
        EXPECT_EQ(d.practical_tail_frames(), 48u);
        EXPECT_FLOAT_EQ(d.flush_residue(), 0.0f);
        d_t::state         st{};
        d_t::workspace     ws{};
        std::vector<float> data(2 * 30, 1.0f);
        std::vector<float> out(data.size());
        static_cast<void>(d.run(st, ws, catenae::span<const float>(data), catenae::span<float>(out)));
        const std::vector<float> zeros(2 * 48, 0.0f);
        std::vector<float>       drained(zeros.size());
        static_cast<void>(d.run(st, ws, catenae::span<const float>(zeros), catenae::span<float>(drained)));
        std::vector<float> after(2 * 8);
        static_cast<void>(d.run(st, ws, catenae::span<const float>(zeros.data(), 16), catenae::span<float>(after)));
        for (const float v : after) {
            EXPECT_EQ(std::memcmp(&v, &zeros[0], sizeof v), 0);
        }
    }

} // namespace
