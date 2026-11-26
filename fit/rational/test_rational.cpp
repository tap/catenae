/// @file test_rational.cpp
/// @brief The rational skeleton's stages, designs and named chains against catenae's contracts (CF).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"
#include "rational/chain.h"
#include "rational/design.h"
#include "rational/ratio.h"
#include "rational/stage.h"

namespace {

    namespace rat = catenae_fit::rational;
    using catenae::ratio;
    using rat::nyquist_design;
    using rat::stage;

    constexpr bool same_terms(ratio a, ratio b) noexcept {
        return a.up == b.up && a.down == b.down;
    }

    // Each machine, at both directions and at both lengths of each band.
    using up2_d1        = stage<float, 2, catenae::ratio_t<2, 1>, nyquist_design<2, 2>>;
    using up2_relaxed   = stage<float, 2, catenae::ratio_t<2, 1>, nyquist_design<2, 1>>;
    using down2_d1      = stage<float, 2, catenae::ratio_t<1, 2>, nyquist_design<2, 2>>;
    using up3_d1        = stage<float, 2, catenae::ratio_t<3, 1>, nyquist_design<3, 2>>;
    using down3_relaxed = stage<float, 2, catenae::ratio_t<1, 3>, nyquist_design<3, 1>>;
    using down3_d1      = stage<float, 2, catenae::ratio_t<1, 3>, nyquist_design<3, 2>>;
    using r32_d1        = stage<float, 2, catenae::ratio_t<3, 2>, nyquist_design<3, 2>>;
    using r32_relaxed   = stage<float, 2, catenae::ratio_t<3, 2>, nyquist_design<3, 1>>;
    using r23_d1        = stage<float, 2, catenae::ratio_t<2, 3>, nyquist_design<3, 2>>;

    // The chains: SampleRateTap's two names, and the converter by 3/2.
    using chain31 = rat::up2_ratio3over2<float, 2>;
    using chain16 = rat::down3_down2<float, 2>;
    using conv32  = rat::converter<float, 2, catenae::ratio_t<3, 2>>;

    // A static chain's memory is plain storage, constant-initialised (D34, D50, CAT-CHAIN-011).
    CATENAE_CONSTINIT catenae::memory_of<chain31> s_memory31{};

    /// The input, interleaved stereo.
    std::vector<float> input(std::size_t frames, std::uint32_t seed) {
        catenae_test::signal s(seed);
        std::vector<float>   in(frames * 2);
        for (float& v : in) {
            v = s.next<float>();
        }
        return in;
    }

    /// The stage's taps as scipy's upfirdn takes them: the design scaled as the stage's table is
    /// (1/M decimating; each L-phase at unit sum for a mixed ratio going down; else the design).
    template <typename Stage>
    std::vector<double> effective_taps() {
        constexpr std::size_t l = Stage::rate_ratio.up;
        constexpr std::size_t m = Stage::rate_ratio.down;
        const auto&           h = Stage::design::prototype;
        std::vector<double>   eff(h.begin(), h.end());
        if (l == 1) {
            for (double& v : eff) {
                v /= static_cast<double>(m);
            }
        }
        else if (l < m) {
            for (std::size_t p = 0; p < l; ++p) {
                double sum = 0.0;
                for (std::size_t i = p; i < eff.size(); i += l) {
                    sum += h[i];
                }
                for (std::size_t i = p; i < eff.size(); i += l) {
                    eff[i] = h[i] / sum;
                }
            }
        }
        return eff;
    }

    /// scipy's upfirdn(h, x, up = L, down = M) from x[< 0] = 0, in double, per channel: output n
    /// is sum_i h[i] u[n M - i], u the input with L - 1 zeros after each frame. `outputs` frames.
    template <typename Stage>
    std::vector<double> upfirdn(const std::vector<float>& x, std::size_t outputs) {
        constexpr std::size_t     l      = Stage::rate_ratio.up;
        constexpr std::size_t     m      = Stage::rate_ratio.down;
        const std::vector<double> h      = effective_taps<Stage>();
        const std::size_t         frames = x.size() / 2;
        std::vector<double>       y(outputs * 2, 0.0);
        for (std::size_t n = 0; n < outputs; ++n) {
            for (std::size_t i = 0; i < h.size() && i <= n * m; ++i) {
                const std::size_t j = n * m - i;
                if (j % l != 0 || j / l >= frames) {
                    continue;
                }
                for (std::size_t c = 0; c < 2; ++c) {
                    y[n * 2 + c] += h[i] * static_cast<double>(x[j / l * 2 + c]);
                }
            }
        }
        return y;
    }

    /// The stage run over `in` in calls of the sizes given, from reset.
    template <typename Stage>
    std::vector<float> run_in(const std::vector<float>& in, const std::vector<std::size_t>& sizes) {
        constexpr Stage       s{};
        typename Stage::state st{};
        catenae::no_workspace ws{};
        std::vector<float>    out;
        std::vector<float>    part;
        std::size_t           at = 0;
        catenae::stage_traits<Stage>::reset(s, st);
        for (const std::size_t n : sizes) {
            part.assign(Stage::max_outputs_for(n) * 2, 0.0f);
            const std::size_t made =
                s.run(st, ws, catenae::span<const float>(in.data() + at * 2, n * 2), catenae::span<float>(part));
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(made * 2));
            at += n;
        }
        return out;
    }

    template <typename Stage>
    class rational_stage : public ::testing::Test {};
    using stages =
        ::testing::Types<up2_d1, up2_relaxed, down2_d1, up3_d1, down3_relaxed, down3_d1, r32_d1, r32_relaxed, r23_d1>;
    TYPED_TEST_SUITE(rational_stage, stages, );

    // A stage's output is upfirdn of its input, at every chunking, and the chunkings agree bit
    // for bit.
    TYPED_TEST(rational_stage, OutputIsUpfirdnFromRestAtAnyChunking) {
        constexpr std::size_t     frames = 300;
        const std::vector<float>  in     = input(frames, 21);
        const std::size_t         owed   = TypeParam::max_outputs_for(frames);
        const std::vector<double> ref    = upfirdn<TypeParam>(in, owed);
        const std::vector<float>  whole  = run_in<TypeParam>(in, {frames});
        ASSERT_EQ(whole.size(), owed * 2) << "n frames from reset write ceil(n L / M)";
        double worst = 0.0;
        for (std::size_t i = 0; i < whole.size(); ++i) {
            worst = std::max(worst, std::fabs(static_cast<double>(whole[i]) - ref[i]));
        }
        EXPECT_LT(worst, 4e-6) << "float against upfirdn in double";
        for (const std::size_t largest : {1u, 2u, 5u, 7u, 64u}) {
            const std::vector<float> pieces = run_in<TypeParam>(in, catenae_test::chunks(frames, largest, 3));
            ASSERT_EQ(pieces.size(), whole.size()) << largest;
            EXPECT_EQ(std::memcmp(pieces.data(), whole.data(), whole.size() * sizeof(float)), 0) << largest;
        }
    }

    // The generic tests every stage runs (tests/support/catenae_test/harness.h): reset over any
    // bytes, the counts, the arrival numbers counted (exactly: frame by frame they are reached),
    // and chunking.
    TYPED_TEST(rational_stage, PassesTheGenericHarness) {
        constexpr TypeParam s{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(s));
        EXPECT_TRUE(catenae_test::counts_are_exact(s));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(s, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(s));
    }

    // From rest, N frames have written ceil(N L / M), frame by frame: the group's output is
    // written when its first frame arrives (PLAN.md 12), one frame in giving one out.
    TYPED_TEST(rational_stage, WritesAGroupOnItsFirstFrame) {
        constexpr std::size_t     l      = TypeParam::rate_ratio.up;
        constexpr std::size_t     m      = TypeParam::rate_ratio.down;
        constexpr std::size_t     frames = 5 * l * m + 7;
        const std::vector<float>  in     = input(frames, 4);
        constexpr TypeParam       s{};
        typename TypeParam::state st{};
        catenae::no_workspace     ws{};
        std::vector<float>        out(TypeParam::max_outputs_for(1) * 2);
        catenae::stage_traits<TypeParam>::reset(s, st);
        std::size_t total = 0;
        for (std::size_t n = 1; n <= frames; ++n) {
            total += s.run(st, ws, catenae::span<const float>(in.data() + (n - 1) * 2, 2), catenae::span<float>(out));
            ASSERT_EQ(total, (n * l + m - 1) / m) << "after " << n << " frames";
        }
    }

    // outputs_for and frames_needed from every position of two superblocks, against the closed
    // form ceil(N L / M): exhaustive where the harness samples.
    TYPED_TEST(rational_stage, CountsAreExactFromEveryPosition) {
        constexpr std::size_t     l  = TypeParam::rate_ratio.up;
        constexpr std::size_t     m  = TypeParam::rate_ratio.down;
        const auto                o  = [](std::size_t n) { return (n * l + m - 1) / m; };
        const std::vector<float>  in = input(2 * l * m + 4 * m, 5);
        constexpr TypeParam       s{};
        typename TypeParam::state st{};
        catenae::no_workspace     ws{};
        std::vector<float>        out(TypeParam::max_outputs_for(1) * 2);
        catenae::stage_traits<TypeParam>::reset(s, st);
        for (std::size_t at = 0; at <= 2 * l * m; ++at) {
            for (std::size_t n = 0; n <= 4 * m; ++n) {
                ASSERT_EQ(s.outputs_for(st, n), o(at + n) - o(at)) << at << " " << n;
                ASSERT_LE(s.outputs_for(st, n), TypeParam::max_outputs_for(n));
            }
            for (std::size_t k = 0; k <= 3 * l; ++k) {
                std::size_t smallest = 0;
                while (o(at + smallest) - o(at) < k) {
                    ++smallest;
                }
                ASSERT_EQ(s.frames_needed(st, k), smallest) << at << " " << k;
            }
            static_cast<void>(
                s.run(st, ws, catenae::span<const float>(in.data() + at * 2, 2), catenae::span<float>(out)));
        }
    }

    /// Whether, after `length` frames of input and then `zeros` zero frames, 64 more zero frames
    /// write only silence.
    template <typename Stage>
    bool silent_after(std::size_t length, std::size_t zeros) {
        std::vector<float> in = input(length, 6);
        in.resize((length + zeros + 64) * 2, 0.0f);
        const std::vector<float> out  = run_in<Stage>(in, {length + zeros, 64});
        const std::size_t        from = Stage::max_outputs_for(length + zeros) * 2;
        for (std::size_t i = from; i < out.size(); ++i) {
            if (out[i] < 0.0f || out[i] > 0.0f) {
                return false;
            }
        }
        return true;
    }

    // The tail is SampleRateTap's window (CF finding 4): after it the output is silence from any
    // input length, and it is the least such number for a decimator, N - 1, and one more than
    // the least for the L-phase machine, whose rows read T frames and need T - 1.
    TYPED_TEST(rational_stage, AfterItsTailTheOutputIsSilence) {
        constexpr std::size_t l = TypeParam::rate_ratio.up;
        constexpr std::size_t m = TypeParam::rate_ratio.down;
        constexpr TypeParam   s{};
        const std::size_t     tail  = s.tail_frames();
        std::size_t           least = 0;
        for (std::size_t length = 1; length <= 2 * l * m + 3; ++length) {
            ASSERT_TRUE(silent_after<TypeParam>(length, tail)) << length;
            std::size_t z = 0;
            while (!silent_after<TypeParam>(length, z)) {
                ++z;
            }
            least = std::max(least, z);
        }
        EXPECT_EQ(least, l == 1 ? tail : tail - 1);
    }

    // What reset sets: each line at the design's row length of silence, the schedule at its start,
    // one pre-advance pending (PLAN.md 12, "Reset values"); storage that was garbage is no matter.
    TYPED_TEST(rational_stage, ResetStartsAtTheRowLengthWithOnePreAdvancePending) {
        constexpr TypeParam s{};
        auto                st = std::make_unique<typename TypeParam::state>();
        std::memset(st.get(), 0x5A, sizeof(typename TypeParam::state));
        s.reset(*st);
        for (const std::uint32_t end : st->end) {
            EXPECT_EQ(end, TypeParam::row_length());
        }
        EXPECT_EQ(st->pos, 0u);
        EXPECT_EQ(st->pending, 1u);
        EXPECT_EQ(st->dphase, 0u);
        for (const float v : st->hist) {
            ASSERT_FALSE(v < 0.0f || v > 0.0f);
        }
        EXPECT_GE(s.outputs_for(*st, 1), 1u) << "one frame in from rest writes";
    }

    // The published numbers: the stage_traits read, and what they are for an L/M stage.
    TYPED_TEST(rational_stage, PublishesItsArrivalLatencyAndTail) {
        using tr                  = catenae::stage_traits<TypeParam>;
        constexpr std::uint32_t l = TypeParam::rate_ratio.up;
        constexpr std::uint32_t m = TypeParam::rate_ratio.down;
        constexpr std::uint32_t n = static_cast<std::uint32_t>(TypeParam::taps());
        static_assert(tr::opted_in && !tr::rests_at_zero && !tr::is_in_place, "a stage that writes its reset");
        static_assert(tr::input_group == m && tr::is_frame_synchronous == (m == 1), "M frames write exactly L");
        static_assert(same_terms(tr::max_shortfall, ratio{0, 1}), "never behind");
        static_assert(tr::max_lead == (ratio{m - 1, m}), "ahead by up to (M - 1) / M");
        static_assert(tr::output_quantum == (m == 1 ? l : 1), "an interpolator writes L a frame");
        static_assert(tr::has_exact_latency && tr::exact_latency == (ratio{(n - 1) / 2, m}),
                      "(N - 1) / (2 M) output frames, a constant of the type");
        static_assert(std::is_empty<TypeParam>::value && sizeof(TypeParam) == 1,
                      "the design is a type: the configuration holds nothing (D54)");
        constexpr TypeParam s{};
        EXPECT_FLOAT_EQ(s.latency().nominal, static_cast<float>(n - 1) / static_cast<float>(2 * m));
        EXPECT_FLOAT_EQ(s.latency().deviation, 0.0f);
        EXPECT_EQ(s.tail_frames(), l == 1 ? n - 1 : TypeParam::row_length()) << "SampleRateTap's window";
        EXPECT_EQ(s.practical_tail_frames(), s.tail_frames());
        EXPECT_FLOAT_EQ(s.flush_residue(), 0.0f);
    }

    // The table is built from the design at compile time as SampleRateTap's constructor builds it:
    // a decimator's branches are the design over M, its centre branch trimmed to the one tap; an
    // interpolator's phases are the design's branches, its centre phase the tap 1 alone, a copy.
    TEST(rational_table, IsTheDesignScaledReversedAndTrimmed) {
        // down2 at N = 7: branch 0 holds h0, h2, h4, h6 over 2, oldest first; branch 1 the centre.
        constexpr auto& h = nyquist_design<2, 2>::prototype;
        EXPECT_EQ(down2_d1::row_length(), 4u);
        EXPECT_EQ(down2_d1::coefficient(0, 0), static_cast<float>(h[6] / 2.0));
        EXPECT_EQ(down2_d1::coefficient(0, 3), static_cast<float>(h[0] / 2.0));
        EXPECT_EQ(down2_d1::coefficient(1, 3), 0.0f);
        EXPECT_EQ(down2_d1::coefficient(1, 2), static_cast<float>(h[3] / 2.0)); // h3 at s = 1, right-aligned
        // up3 at N = 11: phase p holds h[p + 3 t], tap-reversed; each phase sums to 1.
        constexpr auto& g = nyquist_design<3, 2>::prototype;
        for (std::size_t p = 0; p < 3; ++p) {
            double sum = 0.0;
            for (std::size_t t = 0; t < up3_d1::row_length(); ++t) {
                const std::size_t i = p + 3 * (up3_d1::row_length() - 1 - t);
                EXPECT_EQ(up3_d1::coefficient(p, t), i < g.size() ? static_cast<float>(g[i]) : 0.0f) << p << " " << t;
                sum += static_cast<double>(up3_d1::coefficient(p, t));
            }
            EXPECT_NEAR(sum, 1.0, 1e-7) << "phase " << p << " at unit DC gain";
        }
        // 2/3 going down: each of its two phases normalized to unit sum.
        for (std::size_t p = 0; p < 2; ++p) {
            double sum = 0.0;
            for (std::size_t t = 0; t < r23_d1::row_length(); ++t) {
                sum += static_cast<double>(r23_d1::coefficient(p, t));
            }
            EXPECT_NEAR(sum, 1.0, 1e-7) << "phase " << p;
        }
    }

    // An interpolating stage's centre phase is the single tap 1: those outputs are input frames,
    // bit for bit (the trimmed dot is one multiply by 1).
    TEST(rational_table, TheCentrePhaseIsACopy) {
        const std::vector<float> in  = input(200, 9);
        const std::vector<float> up2 = run_in<up2_d1>(in, {200});
        const std::vector<float> r32 = run_in<r32_d1>(in, {200});
        // up2, N = 7: y[2j + 1] = x[j - 1]; 3/2, N = 11: y[n] = x[floor(2 n / 3) - 1] at 2 n mod 3 = 2.
        for (std::size_t j = 1; j < 200; ++j) {
            ASSERT_EQ(std::memcmp(&up2[(2 * j + 1) * 2], &in[(j - 1) * 2], 2 * sizeof(float)), 0) << j;
        }
        for (std::size_t n = 3; n < r32.size() / 2; ++n) {
            if (2 * n % 3 == 2) {
                ASSERT_EQ(std::memcmp(&r32[n * 2], &in[(2 * n / 3 - 1) * 2], 2 * sizeof(float)), 0) << n;
            }
        }
    }

    // The alias computes each stage's design divisor and names the design its profile pins there
    // (PLAN.md 3.3): up2 then 3/2 runs its second stage relaxed by 2; down3 then down2 its first.
    TEST(rational_chain, EachStageTakesTheDesignOfItsDivisor) {
        using rates31 = rat::chain_rates<catenae::ratio_t<2, 1>, catenae::ratio_t<3, 2>>;
        using rates16 = rat::chain_rates<catenae::ratio_t<1, 3>, catenae::ratio_t<1, 2>>;
        static_assert(same_terms(rates31::divisor(0), ratio{1, 1}) && same_terms(rates31::divisor(1), ratio{2, 1}),
                      "1 -> 2 -> 3: the 3/2 stage's lower rate is twice the lowest");
        static_assert(same_terms(rates16::divisor(0), ratio{2, 1}) && same_terms(rates16::divisor(1), ratio{1, 1}),
                      "1 -> 1/3 -> 1/6: the down3 stage's lower rate is twice the lowest");
        static_assert(same_terms(rat::design_divisor(ratio{3, 2}), ratio{1, 1})
                          && same_terms(rat::design_divisor(ratio{13, 1}), ratio{12, 1})
                          && same_terms(rat::design_divisor(ratio{147, 160}), ratio{147, 160}),
                      "the largest 2^a 3^b at or below q, or q below 1");
        static_assert(std::is_same<chain31, catenae::chain<up2_d1, r32_relaxed>>::value, "up2 at m 2, 3/2 at m 1");
        static_assert(std::is_same<chain16, catenae::chain<down3_relaxed, down2_d1>>::value,
                      "down3 at m 1, down2 at m 2");
        static_assert(std::is_same<conv32, catenae::chain<r32_d1>>::value, "one stage: the converter");
        // The same types written with catenae's |.
        using catenae::operator|;
        constexpr auto built = up2_d1{} | r32_relaxed{};
        static_assert(std::is_same<std::remove_cv_t<decltype(built)>, chain31>::value, "| builds the named chain");
        static_assert(std::is_same<std::remove_cv_t<decltype(catenae::chain(r32_d1{}))>, conv32>::value, "chain(s)");
        // The configuration: a byte a stage, the tables in ROM.
        static_assert(sizeof(chain31) == 2 && std::is_trivially_copyable<chain31>::value, "D54");
        EXPECT_EQ(sizeof(catenae::state_of<chain31>), 1112u);
        EXPECT_EQ(sizeof(catenae::state_of<chain16>), 2724u);
    }

    // The composed numbers (PLAN.md 4) against the stages': ratio, group, shortfall, lead,
    // quantum and the exact latency, each stage's carried to the output rate.
    TEST(rational_chain, ComposesRatioGroupLeadAndLatency) {
        using t31 = catenae::stage_traits<chain31>;
        using t16 = catenae::stage_traits<chain16>;
        using t32 = catenae::stage_traits<conv32>;
        static_assert(same_terms(t31::rate_ratio, ratio{3, 1}) && t31::input_group == 1 && t31::is_frame_synchronous,
                      "up then 3/2: whole groups reach the 3/2 stage, so the chain keeps pace");
        static_assert(same_terms(t31::max_lead, ratio{0, 1}) && t31::output_quantum == 3, "3 frames for each");
        static_assert(same_terms(t16::rate_ratio, ratio{1, 6}) && t16::input_group == 6, "down by 6");
        static_assert(same_terms(t16::max_lead, ratio{5, 6}) && same_terms(t16::max_shortfall, ratio{0, 1}),
                      "2/3 carried to 1/3, plus 1/2: ceil(N / 6) leads N / 6 by up to 5/6");
        static_assert(same_terms(t32::max_lead, ratio{1, 2}) && t32::input_group == 2, "3/2 alone");
        // up2 at N = 7: 3 frames at 2x, 9/2 at 3x; 3/2 at N = 5: 4 / (2 x 2) = 1. Total 11/2.
        static_assert(t31::has_exact_latency && same_terms(t31::exact_latency, ratio{11, 2}), "11/2");
        // down3 at N = 5: 4/6 at 1/3, 1/3 at 1/6; down2 at N = 7: 6/4. Total 11/6.
        static_assert(t16::has_exact_latency && same_terms(t16::exact_latency, ratio{11, 6}), "11/6");
        EXPECT_FLOAT_EQ(chain31{}.latency().nominal, 5.5f);
        EXPECT_NEAR(chain16{}.latency().nominal, 11.0f / 6.0f, 1e-6f);
        // And counted: the composed numbers are what the chain does, frame by frame.
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(chain31{}, true));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(chain16{}, true));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(conv32{}, true));
    }

    // A chain is a stage (CAT-CHAIN-003): the named chains pass the generic tests too.
    TEST(rational_chain, NamedChainsPassTheGenericHarness) {
        constexpr chain31 c31 = chain31{};
        constexpr chain16 c16 = chain16{};
        constexpr conv32  c32 = conv32{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(c31));
        EXPECT_TRUE(catenae_test::counts_are_exact(c31));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(c31));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(c16));
        EXPECT_TRUE(catenae_test::counts_are_exact(c16));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(c16));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(c32));
        EXPECT_TRUE(catenae_test::counts_are_exact(c32));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(c32));
    }

    /// A chain of two of the skeleton's stages: whether its composed numbers bound what counting
    /// finds (they must), and whether they are what it finds (frame by frame, and the quantum).
    template <typename A, typename B>
    ::testing::AssertionResult composes(bool exact) {
        const catenae::chain<A, B> c(A{}, B{});
        if (!catenae_test::arrival_numbers_hold(c, false)) {
            return catenae_test::arrival_numbers_hold(c, false);
        }
        if (catenae_test::arrival_numbers_hold(c, true) != exact) {
            return ::testing::AssertionFailure() << "the composed numbers are " << (exact ? "loose" : "exact");
        }
        return ::testing::AssertionSuccess();
    }

    // Every pair of real L/M stages (writing on a group's first frame): the composed numbers
    // (PLAN.md 4) are never too small, and are exact on 32 of the 36. Pinned where they are
    // loose: 3/2 then down 3 (lead 5/6, counted 1/2), 2/3 then down 2 (5/6, counted 2/3), 2/3
    // then 3/2 (3/2, counted 1), and 3/2 then 2/3 (output quantum 1, counted 2).
    TEST(rational_chain, ComposedNumbersBoundEveryPairOfStages) {
        using u2  = stage<float, 1, catenae::ratio_t<2, 1>, nyquist_design<2, 2>>;
        using d2  = stage<float, 1, catenae::ratio_t<1, 2>, nyquist_design<2, 2>>;
        using u3  = stage<float, 1, catenae::ratio_t<3, 1>, nyquist_design<3, 1>>;
        using d3  = stage<float, 1, catenae::ratio_t<1, 3>, nyquist_design<3, 1>>;
        using r32 = stage<float, 1, catenae::ratio_t<3, 2>, nyquist_design<3, 1>>;
        using r23 = stage<float, 1, catenae::ratio_t<2, 3>, nyquist_design<3, 1>>;
        EXPECT_TRUE((composes<u2, u2>(true) && composes<u2, d2>(true) && composes<u2, u3>(true)));
        EXPECT_TRUE((composes<u2, d3>(true) && composes<u2, r32>(true) && composes<u2, r23>(true)));
        EXPECT_TRUE((composes<d2, u2>(true) && composes<d2, d2>(true) && composes<d2, u3>(true)));
        EXPECT_TRUE((composes<d2, d3>(true) && composes<d2, r32>(true) && composes<d2, r23>(true)));
        EXPECT_TRUE((composes<u3, u2>(true) && composes<u3, d2>(true) && composes<u3, u3>(true)));
        EXPECT_TRUE((composes<u3, d3>(true) && composes<u3, r32>(true) && composes<u3, r23>(true)));
        EXPECT_TRUE((composes<d3, u2>(true) && composes<d3, d2>(true) && composes<d3, u3>(true)));
        EXPECT_TRUE((composes<d3, d3>(true) && composes<d3, r32>(true) && composes<d3, r23>(true)));
        EXPECT_TRUE((composes<r32, u2>(true) && composes<r32, d2>(true) && composes<r32, u3>(true)));
        EXPECT_TRUE((composes<r32, d3>(false) && composes<r32, r32>(true) && composes<r32, r23>(false)));
        EXPECT_TRUE((composes<r23, u2>(true) && composes<r23, d2>(false) && composes<r23, u3>(true)));
        EXPECT_TRUE((composes<r23, d3>(true) && composes<r23, r32>(false) && composes<r23, r23>(true)));
    }

    // The exact latency is where the impulse response is centred: an impulse through up2 then
    // 3/2 comes out symmetric about 11/2 output frames.
    TEST(rational_chain, ExactLatencyIsTheImpulseCentroid) {
        constexpr chain31  c = chain31{};
        std::vector<float> in(2 * 16, 0.0f);
        in[0] = 1.0f;
        in[1] = 1.0f;
        std::vector<float> out(chain31::max_outputs_for(16) * 2);
        auto               m = std::make_unique<catenae::memory_of<chain31>>();
        c.reset(m->state);
        const std::size_t made   = c.process(*m, catenae::span<const float>(in), catenae::span<float>(out));
        double            moment = 0.0;
        double            mass   = 0.0;
        for (std::size_t n = 0; n < made; ++n) {
            moment += static_cast<double>(n) * static_cast<double>(out[n * 2]);
            mass += static_cast<double>(out[n * 2]);
        }
        EXPECT_NEAR(moment / mass, 5.5, 1e-6);
    }

    /// A chain run over `in` from reset in calls of the sizes given, through process.
    template <typename C>
    std::vector<float> process_in(const C& c, const std::vector<float>& in, const std::vector<std::size_t>& sizes) {
        auto m = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> out;
        std::vector<float> part;
        std::size_t        at = 0;
        for (const std::size_t n : sizes) {
            part.assign(C::max_outputs_for(n) * 2, 0.0f);
            const std::size_t made =
                c.process(*m, catenae::span<const float>(in.data() + at * 2, n * 2), catenae::span<float>(part));
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(made * 2));
            at += n;
        }
        return out;
    }

    // process in calls of any size writes what the stages alone write in one call, and a fixed
    // block of whole groups writes exactly N L / M frames, checked by the compiler.
    TEST(rational_chain, ProcessesDynamicAndFixedBlocks) {
        constexpr chain16        c        = chain16{};
        const std::vector<float> in       = input(960, 12);
        const std::vector<float> whole    = process_in(c, in, {960});
        const std::vector<float> by_stage = run_in<down2_d1>(run_in<down3_relaxed>(in, {960}), {320});
        ASSERT_EQ(whole.size(), 160u * 2);
        EXPECT_EQ(std::memcmp(whole.data(), by_stage.data(), whole.size() * sizeof(float)), 0);
        for (std::uint32_t seed = 1; seed < 4; ++seed) {
            const std::vector<float> pieces = process_in(c, in, catenae_test::chunks(960, 100 * seed, seed));
            ASSERT_EQ(pieces.size(), whole.size());
            EXPECT_EQ(std::memcmp(pieces.data(), whole.data(), whole.size() * sizeof(float)), 0) << seed;
        }
        // Fixed blocks of 96 frames, a whole number of the group of 6: 16 frames each, always.
        auto m = std::make_unique<catenae::memory_of<chain16>>();
        c.reset(m->state);
        std::array<float, 96 * 2> block{};
        std::array<float, 16 * 2> out{};
        std::vector<float>        fixed;
        for (std::size_t b = 0; b < 10; ++b) {
            std::copy(in.begin() + static_cast<std::ptrdiff_t>(b * 96 * 2),
                      in.begin() + static_cast<std::ptrdiff_t>((b + 1) * 96 * 2), block.begin());
            ASSERT_EQ(c.process(*m, block, out), 16u);
            fixed.insert(fixed.end(), out.begin(), out.end());
        }
        EXPECT_EQ(std::memcmp(fixed.data(), whole.data(), whole.size() * sizeof(float)), 0);
        // The dynamic entry checks the output against outputs_for: 7 frames from rest write 2.
        std::vector<float> small(2);
        c.reset(m->state);
        CATENAE_EXPECT_CHECK(catenae::check::output_too_small,
                             static_cast<void>(c.process(*m, catenae::span<const float>(in.data(), 7 * 2),
                                                         catenae::span<float>(small))));
    }

    /// The chain over `in`, then flushed one burst at a time until flush_remaining is 0.
    template <typename C>
    std::vector<float> flushed(const C& c, const std::vector<float>& in, std::size_t& calls) {
        auto m = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> got(C::max_outputs_for(in.size() / 2) * 2);
        got.resize(c.process(*m, catenae::span<const float>(in), catenae::span<float>(got)) * 2);
        std::vector<float> burst(C::max_outputs_for(1) * 2);
        calls = 0;
        while (c.flush_remaining(m->state) > 0 && calls < 10000) {
            const std::size_t n = c.flush(*m, catenae::span<float>(burst));
            got.insert(got.end(), burst.begin(), burst.begin() + static_cast<std::ptrdiff_t>(n * 2));
            ++calls;
        }
        // After the tail, zeros write only silence.
        const std::vector<float> zeros(2 * 120, 0.0f);
        std::vector<float>       more(C::max_outputs_for(120) * 2);
        const std::size_t        extra = c.process(*m, catenae::span<const float>(zeros), catenae::span<float>(more));
        for (std::size_t i = 0; i < extra * 2; ++i) {
            EXPECT_FALSE(more[i] < 0.0f || more[i] > 0.0f) << i;
        }
        return got;
    }

    // The flush is resumable zero padding (PLAN.md 4, CAT-CHAIN-006): one burst at a time it
    // writes what the input padded with the chain's tail writes, and then the output is silence
    // (no residue: an FIR's tail is exact).
    TEST(rational_chain, FlushDrainsTheTailToSilence) {
        constexpr chain16 c16 = chain16{};
        constexpr chain31 c31 = chain31{};
        // down2 at N = 7: 6; down3 at N = 5: 4 + ceil((6 + 2/3) x 3) = 24 input frames.
        EXPECT_EQ(c16.tail_frames(), 24u);
        // 3/2 at N = 5: its row of 2; up2 at N = 7: its row of 4 + ceil(2 / 2) = 5.
        EXPECT_EQ(c31.tail_frames(), 5u);
        EXPECT_EQ(c16.practical_tail_frames(), 24u);
        EXPECT_FLOAT_EQ(c16.flush_residue(), 0.0f);
        const std::vector<float> in      = input(101, 13);
        std::size_t              calls16 = 0;
        std::size_t              calls31 = 0;
        const std::vector<float> got16   = flushed(c16, in, calls16);
        const std::vector<float> got31   = flushed(c31, in, calls31);
        std::vector<float>       padded  = in;
        padded.resize((101 + 24) * 2, 0.0f);
        const std::vector<float> owed16 = process_in(c16, padded, {125});
        padded.resize((101 + 5) * 2);
        const std::vector<float> owed31 = process_in(c31, padded, {106});
        ASSERT_EQ(got16.size(), owed16.size());
        ASSERT_EQ(got31.size(), owed31.size());
        EXPECT_EQ(std::memcmp(got16.data(), owed16.data(), got16.size() * sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(got31.data(), owed31.data(), got31.size() * sizeof(float)), 0);
        EXPECT_GT(calls16, 1u);
        EXPECT_GT(calls31, 1u);
    }

    // A static chain's memory, constant-initialised in .bss, runs once reset.
    TEST(rational_chain, StaticMemoryIsPlainStorage) {
        constexpr chain31 c = chain31{};
        c.reset(s_memory31.state);
        std::array<float, 64 * 2>  in{};
        std::array<float, 192 * 2> out{};
        in.fill(0.25f);
        EXPECT_EQ(c.process(s_memory31, in, out), 192u);
    }

} // namespace
