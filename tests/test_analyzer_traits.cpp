/// @file test_analyzer_traits.cpp
/// @brief analyzer_traits' contract (include/catenae/analyzer_traits.h), on the toy meter.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/analyzer_traits.h"
#include "catenae_fixtures/edges.h"

namespace {

    using meter  = catenae_fixtures::peak_meter<2, 4>;
    using traits = catenae::analyzer_traits<meter>;

    // Verifies CAT-ANALYZER-001.
    TEST(analyzer_traits, ReadsTheDeclaredMembers) {
        static_assert(std::is_same<traits::input_port, catenae::port<float, 2>>::value, "what it reads");
        static_assert(std::is_same<traits::result_type, float>::value, "what a result is");
        static_assert(meter::hop_frames == 4 && meter::window_frames == 4, "a result each hop");
        static_assert(traits::opted_in, "the toy opts in");
        SUCCEED();
    }

    // Verifies CAT-ANALYZER-002 and CAT-ANALYZER-003.
    TEST(analyzer_traits, ResultsCountAsTheyArrive) {
        static_assert(traits::rests_at_zero && catenae::detail::k_is_plain_storage<traits::state>, "rests at zero");
        const meter          m{};
        traits::state        st{};
        std::array<float, 8> hop{0.1f, -0.2f, 0.3f, -0.9f, 0.0f, 0.4f, 0.2f, 0.1f};
        m.analyze(st, catenae::span<const float>(hop.data(), 6));
        EXPECT_EQ(m.results_ready(st), 0u);
        m.analyze(st, catenae::span<const float>(hop.data() + 6, 2));
        EXPECT_EQ(m.results_ready(st), 1u);
        EXPECT_FLOAT_EQ(m.result(st), 0.9f);
        static_assert(std::is_same<decltype(m.results_ready(st)), std::uint32_t>::value, "a 32-bit count");
    }

} // namespace
