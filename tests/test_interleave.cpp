/// @file test_interleave.cpp
/// @brief The layout converters' contract (include/catenae/interleave.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/interleave.h"
#include "catenae/sample_format.h"
#include "catenae_test/harness.h"

namespace {

    template <typename S>
    class layout_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double, catenae::q15, catenae::q31, catenae::q4_27>;
    TYPED_TEST_SUITE(layout_formats, formats, ); // the empty name generator: Clang asks for the third argument

    // Verifies CAT-LAYOUT-001.
    TYPED_TEST(layout_formats, ValueFTimesChPlusCIsChannelCsValueF) {
        using value_type = catenae::storage_t<TypeParam>;
        using split_t    = catenae::basic_deinterleave<TypeParam, 4>;
        using join_t     = catenae::basic_interleave<TypeParam, 4>;
        constexpr split_t                      split{};
        constexpr join_t                       join{};
        const auto                             in = catenae_test::input_for<split_t>(50, 7);
        std::array<std::vector<value_type>, 4> planes;
        for (auto& p : planes) {
            p.resize(50);
        }
        std::array<catenae::span<value_type>, 4> out{planes[0], planes[1], planes[2], planes[3]};
        typename split_t::state                  st{};
        typename split_t::workspace              ws{};
        EXPECT_EQ(split.run(st, ws, catenae::span<const value_type>(in), out), 50u);
        for (std::size_t f = 0; f < 50; ++f) {
            for (std::size_t c = 0; c < 4; ++c) {
                ASSERT_EQ(std::memcmp(&planes[c][f], &in[f * 4 + c], sizeof(value_type)), 0) << f << " " << c;
            }
        }
        std::array<catenae::span<const value_type>, 4> back{planes[0], planes[1], planes[2], planes[3]};
        std::vector<value_type>                        rejoined(in.size());
        typename join_t::state                         jst{};
        EXPECT_EQ(join.run(jst, ws, back, catenae::span<value_type>(rejoined)), 50u);
        EXPECT_EQ(std::memcmp(rejoined.data(), in.data(), in.size() * sizeof(value_type)), 0);
        static_assert(!catenae::stage_traits<split_t>::is_in_place
                          && catenae::stage_traits<split_t>::is_frame_synchronous,
                      "1:1 frame by frame, never in place");
    }

} // namespace
