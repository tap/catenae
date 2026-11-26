/// @file test_biquad.cpp
/// @brief biquad's contract (include/catenae/biquad.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/biquad.h"
#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/gain.h"
#include "catenae/parallel.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/floating_point.h"
#include "catenae_test/harness.h"
#include "lowpass_table.h"

namespace {

    template <typename T>
    class biquad_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(biquad_formats, formats, );

    using catenae::biquad_response;

    template <typename S, std::size_t Order>
    catenae::biquad_design<S, Order / 2> butterworth_at(double fc) {
        const auto d = catenae::butterworth_design<S, Order>::make(fc);
        EXPECT_TRUE(d.has_value()) << Order << " at " << fc;
        return *d;
    }

    template <typename S>
    catenae::biquad_design<S> cookbook(biquad_response r, double f0, double q, double g = 0.0) {
        const auto d = catenae::biquad_design<S>::make({r, f0, q, g});
        EXPECT_TRUE(d.has_value()) << static_cast<int>(r) << " at " << f0;
        return *d;
    }

    template <typename S>
    std::vector<S> noise(std::size_t values, std::uint32_t seed, double level = 1.0) {
        catenae_test::signal s(seed);
        std::vector<S>       v(values);
        for (S& x : v) {
            x = static_cast<S>(level * static_cast<double>(s.next<S>()));
        }
        return v;
    }

    /// Whether a and b are within one ulp of each other.
    bool within_an_ulp(float a, float b) {
        const float up   = std::nextafter(a, std::numeric_limits<float>::infinity());
        const float down = std::nextafter(a, -std::numeric_limits<float>::infinity());
        return b >= down && b <= up;
    }

    // Verifies CAT-BIQUAD-003.
    TEST(biquad, ATableDesignIsWhatMakeGives) {
        // examples/lowpass_table.h, a constant: from make() on the hosted Linux leg, bit for bit;
        // within a float ulp of make() wherever another libm rounds the double design otherwise.
        const auto made  = butterworth_at<float, 4>(0.1);
        const auto table = example::lowpass_at_0_1;
        for (std::size_t s = 0; s < 2; ++s) {
            for (std::size_t j = 0; j < 5; ++j) {
                EXPECT_TRUE(within_an_ulp(table.table()[s][j], made.table()[s][j]))
                    << "section " << s << " coefficient " << j;
            }
        }
        EXPECT_TRUE(within_an_ulp(table.latency().nominal, made.latency().nominal));
        EXPECT_TRUE(within_an_ulp(table.latency().deviation, made.latency().deviation));
        EXPECT_NEAR(static_cast<double>(table.tail_frames()), static_cast<double>(made.tail_frames()), 1.0);
        EXPECT_NEAR(static_cast<double>(table.practical_tail_frames()),
                    static_cast<double>(made.practical_tail_frames()), 1.0);
        EXPECT_EQ(table.flush_residue(), made.flush_residue());
    }

    // Verifies CAT-BIQUAD-001.
    TYPED_TEST(biquad_formats, EachChannelIsItsSectionsInOrder) {
        constexpr std::size_t                         ch     = 3;
        constexpr std::size_t                         frames = 500;
        const auto                                    design = butterworth_at<TypeParam, 4>(0.1);
        const catenae::basic_biquad<TypeParam, ch, 2> stage(design);
        typename decltype(stage)::state               st{};
        catenae::no_workspace                         ws{};
        const std::vector<TypeParam>                  in = noise<TypeParam>(frames * ch, 5);
        std::vector<TypeParam>                        out(in.size());
        ASSERT_EQ(stage.run(st, ws, catenae::span<const TypeParam>(in), catenae::span<TypeParam>(out)), frames);
        // Each channel through the per-frame kernel on a history of its own.
        for (std::size_t c = 0; c < ch; ++c) {
            std::array<TypeParam, 4> z{};
            for (std::size_t f = 0; f < frames; ++f) {
                const TypeParam want =
                    catenae::detail::biquad_cascade_frame<TypeParam, 2>(design.table(), z.data(), in[f * ch + c]);
                ASSERT_EQ(std::memcmp(&want, &out[f * ch + c], sizeof(TypeParam)), 0)
                    << "channel " << c << " frame " << f;
            }
        }
        // In place, the same bits.
        typename decltype(stage)::state again{};
        std::vector<TypeParam>          buffer = in;
        static_cast<void>(
            stage.run(again, ws, catenae::span<const TypeParam>(buffer), catenae::span<TypeParam>(buffer)));
        EXPECT_EQ(std::memcmp(buffer.data(), out.data(), out.size() * sizeof(TypeParam)), 0);
        // The generic tests: reset over any storage, the counts, the arrival numbers, any chunking.
        const catenae::basic_biquad<TypeParam, 2, 1> one(cookbook<TypeParam>(biquad_response::peaking, 0.05, 2.0, 6.0));
        const catenae::basic_butterworth<TypeParam, 3, 8> eighth(butterworth_at<TypeParam, 8>(0.2));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(one));
        EXPECT_TRUE(catenae_test::counts_are_exact(one));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(one, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(one));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(eighth));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(eighth));
    }

    // Verifies CAT-BIQUAD-002.
    TYPED_TEST(biquad_formats, ANewDesignAppliesToTheStateAsItStands) {
        constexpr std::size_t               frames = 300;
        const auto                          first  = cookbook<TypeParam>(biquad_response::lowpass, 0.05, 0.7);
        const auto                          second = cookbook<TypeParam>(biquad_response::highpass, 0.2, 2.0);
        catenae::basic_biquad<TypeParam, 2> stage(first);
        typename catenae::basic_biquad<TypeParam, 2>::state st{};
        catenae::no_workspace                               ws{};
        const std::vector<TypeParam>                        in = noise<TypeParam>(frames * 2, 9);
        std::vector<TypeParam>                              out(in.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data(), frames),
                                    catenae::span<TypeParam>(out.data(), frames)));
        auto held = st; // the history the new design inherits
        stage.set_design(second);
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data() + frames, frames),
                                    catenae::span<TypeParam>(out.data() + frames, frames)));
        const catenae::basic_biquad<TypeParam, 2> fresh(second);
        std::vector<TypeParam>                    want(frames);
        static_cast<void>(fresh.run(held, ws, catenae::span<const TypeParam>(in.data() + frames, frames),
                                    catenae::span<TypeParam>(want)));
        EXPECT_EQ(std::memcmp(want.data(), out.data() + frames, frames * sizeof(TypeParam)), 0);
        EXPECT_EQ(std::memcmp(&stage.design(), &second, sizeof(second)), 0);
    }

    // Verifies CAT-BIQUAD-003.
    TYPED_TEST(biquad_formats, ItReportsItsDesignsNumbers) {
        const auto                                        d = butterworth_at<TypeParam, 4>(0.1);
        const catenae::basic_butterworth<TypeParam, 2, 4> stage(d);
        using traits = catenae::stage_traits<catenae::basic_butterworth<TypeParam, 2, 4>>;
        static_assert(!traits::has_exact_latency, "a recursion has no exact latency");
        static_assert(traits::rests_at_zero && traits::is_in_place && traits::is_frame_synchronous, "the contract");
        EXPECT_EQ(stage.latency().nominal, d.latency().nominal);
        EXPECT_EQ(stage.latency().deviation, d.latency().deviation);
        EXPECT_EQ(stage.tail_frames(), d.tail_frames());
        EXPECT_EQ(stage.practical_tail_frames(), d.practical_tail_frames());
        EXPECT_EQ(stage.flush_residue(), d.flush_residue());
        // Beside a gain in parallel it is aligned by its caller (D43), and a chain composes it.
        using arms = catenae::parallel<catenae::basic_butterworth<TypeParam, 1, 4>, catenae::basic_gain<TypeParam, 1>,
                                       catenae::align::by_caller>;
        static_assert(catenae::stage_traits<arms>::opted_in, "a recursive arm, aligned by the caller");
        const auto strip = catenae::chain(stage) | catenae::basic_gain<TypeParam, 2>{TypeParam(0.5), 0};
        EXPECT_EQ(strip.latency().nominal, d.latency().nominal);
    }

    // Verifies CAT-BIQUAD-004.
    TEST(biquad, AtAuditRunChecksTheFlushToZeroSetting) {
        const catenae::biquad<1>  stage(cookbook<float>(biquad_response::lowpass, 0.1, 0.7));
        catenae::biquad<1>::state st{};
        catenae::no_workspace     ws{};
        std::array<float, 8>      buffer{};
        const auto                run = [&] {
            static_cast<void>(stage.run(st, ws, catenae::span<const float>(buffer), catenae::span<float>(buffer)));
        };
        run(); // the battery's setting is the build's
        if constexpr (catenae::detail::k_subnormal_control<float>) {
            const catenae_test::flush_to_zero_scope other(CATENAE_FLUSH_TO_ZERO == 0);
            if constexpr (catenae::detail::k_audit_level) {
                CATENAE_EXPECT_CHECK(catenae::check::flush_to_zero, run());
            }
            else {
                run(); // at boundary nothing is read
            }
        }
        EXPECT_TRUE(catenae::detail::flush_to_zero_as_stated<float>());
    }

    // Verifies CAT-BIQUAD-005.
    TEST(biquad, FloatFollowsTheDoubleRecursion) {
#if defined(CATENAE_TEST_EMULATED)
        constexpr std::size_t frames = 4800;
#else
        constexpr std::size_t frames = 48000;
#endif
        const std::vector<float>              in_f = noise<float>(frames, 21, 0.9);
        const std::vector<double>             in_d(in_f.begin(), in_f.end());
        const catenae::butterworth<1, 4>      f(butterworth_at<float, 4>(0.07));
        const catenae::butterworth_f64<1, 4>  d(butterworth_at<double, 4>(0.07));
        catenae::butterworth<1, 4>::state     sf{};
        catenae::butterworth_f64<1, 4>::state sd{};
        catenae::no_workspace                 ws{};
        std::vector<float>                    out_f(frames);
        std::vector<double>                   out_d(frames);
        static_cast<void>(f.run(sf, ws, catenae::span<const float>(in_f), catenae::span<float>(out_f)));
        static_cast<void>(d.run(sd, ws, catenae::span<const double>(in_d), catenae::span<double>(out_d)));
        double worst = 0.0;
        for (std::size_t n = 0; n < frames; ++n) {
            const double e = std::fabs(static_cast<double>(out_f[n]) - out_d[n]);
            worst          = e > worst ? e : worst;
        }
        EXPECT_LT(worst, 2e-6); // DspTap's bound at 0.07, order 4
    }

    template <std::size_t K>
    void residue_holds(const catenae::biquad_design<float, K>& design, const char* what) {
        const catenae::basic_biquad<float, 1, K>           stage(design);
        typename catenae::basic_biquad<float, 1, K>::state st{};
        catenae::no_workspace                              ws{};
        // A full-scale square wave of period 64, then the tail of zeros.
        std::vector<float> in(4096);
        for (std::size_t n = 0; n < in.size(); ++n) {
            in[n] = (n / 32) % 2 == 0 ? 1.0f : -1.0f;
        }
        std::vector<float> out(in.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const float>(in), catenae::span<float>(out)));
        std::vector<float> zeros(stage.tail_frames(), 0.0f);
        std::vector<float> tail(zeros.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const float>(zeros), catenae::span<float>(tail)));
        std::array<float, 256> after{};
        std::array<float, 256> silent{};
        static_cast<void>(stage.run(st, ws, catenae::span<const float>(silent), catenae::span<float>(after)));
        for (const float v : after) {
            ASSERT_LE(std::fabs(v), stage.flush_residue()) << what;
        }
    }

    // Verifies CAT-BIQUAD-005.
    TEST(biquad, AfterItsTailTheOutputIsWithinTheResidue) {
#if defined(CATENAE_TEST_EMULATED)
        const std::array<double, 2> cutoffs = {0.05, 0.45};
#else
        const std::array<double, 4> cutoffs = {0.005, 0.05, 0.25, 0.45};
#endif
        for (const double fc : cutoffs) {
            residue_holds(butterworth_at<float, 4>(fc), "order 4");
            residue_holds(butterworth_at<float, 8>(fc), "order 8");
        }
        residue_holds(cookbook<float>(biquad_response::peaking, 0.05, 3.0, 9.0), "peaking");
        residue_holds(cookbook<float>(biquad_response::allpass, 0.3, 0.7), "all-pass");
        residue_holds(cookbook<float>(biquad_response::notch, 0.15, 8.0), "notch");
        residue_holds(cookbook<float>(biquad_response::high_shelf, 0.1, 0.7, -6.0), "high shelf");
    }

} // namespace
