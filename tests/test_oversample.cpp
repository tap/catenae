/// @file test_oversample.cpp
/// @brief oversample's contract (include/catenae/oversample.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/biquad.h"
#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/map.h"
#include "catenae/oversample.h"
#include "catenae/parallel.h"
#include "catenae/resample.h"
#include "catenae/sample_format.h"
#include "catenae_test/harness.h"

namespace {

    /// The identity, a stage of no latency.
    struct identity {
        template <typename V>
        constexpr V operator()(V x) const noexcept {
            return x;
        }
    };

    template <typename Chain, typename V>
    std::vector<V> run_chain(const Chain& c, const std::vector<V>& in) {
        static catenae::memory_of<Chain> mem;
        c.reset(mem.state);
        std::vector<V> out(in.size());
        static_cast<void>(c.process(mem, in, out));
        return out;
    }

    // Verifies CAT-OVERSAMPLE-001.
    TEST(oversample, ItsLatencyIsWhole) {
        using by2 = catenae::oversample<2, catenae::basic_map<double, 1, identity>>;
        using by4 = catenae::oversample<4, catenae::basic_map<double, 1, identity>>;
        static_assert(by2::delay_frames == 0 && by2::bare_latency == (catenae::ratio{19, 1}), "whole at 2 times");
        static_assert(catenae::stage_traits<by2>::exact_latency == (catenae::ratio{19, 1}), "19");
        static_assert(by4::bare_latency == (catenae::ratio{47, 2}), "the bare chain: 19/2 + 9/4 + 9/4 + 19/2");
        static_assert(by4::delay_frames == 1 && by4::delay_rate == 2, "one frame at the doubled rate");
        static_assert(catenae::stage_traits<by4>::exact_latency == (catenae::ratio{24, 1}), "24");
        static_assert(catenae::stage_traits<by4>::rate_ratio == (catenae::ratio{1, 1})
                          && catenae::stage_traits<by4>::is_frame_synchronous,
                      "1:1, frame-synchronous");
        // A stage of a quarter base frame at 4 times: the delay goes at the full inner rate.
        using q = catenae::oversample<4, catenae::basic_delay<double, 1, 1>>;
        static_assert(q::bare_latency == (catenae::ratio{95, 4}) && q::delay_frames == 1 && q::delay_rate == 4, "1/4");
        static_assert(catenae::stage_traits<q>::exact_latency == (catenae::ratio{24, 1}), "24");
        // One of half a base frame needs none; one of a frame at 2 times, one more there.
        static_assert(catenae::oversample<4, catenae::basic_delay<double, 1, 2>>::delay_frames == 0, "47/2 + 1/2");
        using odd = catenae::oversample<2, catenae::basic_delay<double, 1, 1>>;
        static_assert(odd::delay_frames == 1 && odd::delay_rate == 2
                          && catenae::stage_traits<odd>::exact_latency == (catenae::ratio{20, 1}),
                      "19 + 1/2 + 1/2");
        // A stage with no exact latency: the rate changers are made whole, the latency nominal.
        static_assert(!catenae::stage_traits<catenae::oversample<4, catenae::biquad<2>>>::has_exact_latency,
                      "a recursion: no exact latency");
        static_assert(catenae::oversample<4, catenae::biquad<2>>::delay_frames == 1, "the rate changers made whole");
    }

    // Verifies CAT-OVERSAMPLE-002.
    TEST(oversample, AnImpulseIsSymmetricAboutTheLatency) {
        const catenae::oversample<4, catenae::basic_map<double, 1, identity>> by4{
            catenae::basic_map<double, 1, identity>{identity{}}};
        std::vector<double> x(128, 0.0);
        x[0]                        = 1.0;
        const std::vector<double> y = run_chain(by4, x);
        EXPECT_EQ(std::max_element(y.begin(), y.end()) - y.begin(), 24);
        for (std::size_t k = 1; k <= 24; ++k) {
            EXPECT_NEAR(y[24 - k], y[24 + k], 1e-15) << k;
        }
        // The bare chain is half a frame short of it: its two largest samples are 23 and 24.
        const auto bare = catenae::basic_up2<double, 1>{} | catenae::basic_up2<double, 1, 2>{}
                          | catenae::basic_map<double, 1, identity>{identity{}} | catenae::basic_down2<double, 1, 2>{}
                          | catenae::basic_down2<double, 1>{};
        const std::vector<double> b = run_chain(bare, x);
        EXPECT_NEAR(b[23], b[24], 1e-15);
        EXPECT_GT(b[23], 0.5);
    }

    // Verifies CAT-OVERSAMPLE-002.
    TEST(oversample, ADryArmLinesUpByType) {
        using wet  = catenae::oversample<4, catenae::map<1, identity>>;
        using arms = catenae::parallel<wet, catenae::gain<1>>;
        static_assert(catenae::stage_traits<arms>::opted_in, "aligned by type: the latency is whole");
        static_assert(catenae::stage_traits<arms>::exact_latency == (catenae::ratio{24, 1}), "24");
        const arms         both(wet{catenae::map<1, identity>{identity{}}}, catenae::gain<1>{1.0f, 0});
        static arms::state st;
        both.reset(st);
        arms::workspace    ws{};
        std::vector<float> in(2 * 96, 0.0f);
        in[0] = 1.0f;
        in[1] = 1.0f;
        std::vector<float> out(2 * 96);
        ASSERT_EQ(both.run(st, ws, catenae::span<const float>(in), catenae::span<float>(out)), 96u);
        std::size_t wet_peak = 0;
        for (std::size_t f = 0; f < 96; ++f) {
            wet_peak = out[2 * f] > out[2 * wet_peak] ? f : wet_peak;
        }
        EXPECT_EQ(wet_peak, 24u);
        EXPECT_EQ(out[2 * 24 + 1], 1.0f); // the dry arm's impulse, delayed to meet it
    }

    // Verifies CAT-OVERSAMPLE-001.
    TEST(oversample, ItKeepsTheStageContract) {
        const catenae::oversample<4, catenae::map<2, identity>> f{catenae::map<2, identity>{identity{}}};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(f));
        EXPECT_TRUE(catenae_test::counts_are_exact(f));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(f, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(f));
        const catenae::oversample<2, catenae::basic_delay<catenae::q15, 1, 3>> q{
            catenae::basic_delay<catenae::q15, 1, 3>{}};
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(q));
        EXPECT_TRUE(catenae_test::counts_are_exact(q));
        // Its inner stage is reachable, and a | b keeps it one stage.
        static_assert(std::is_same<std::remove_cv_t<std::remove_reference_t<decltype(f.inner())>>,
                                   catenae::map<2, identity>>::value,
                      "inner()");
        const auto strip = f | catenae::gain<2>{0.5f, 0};
        static_assert(
            std::is_same<std::remove_const_t<decltype(strip)>,
                         catenae::chain<catenae::oversample<4, catenae::map<2, identity>>, catenae::gain<2>>>::value,
            "one stage of a chain");
    }

} // namespace
