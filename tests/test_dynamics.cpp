/// @file test_dynamics.cpp
/// @brief The dynamics stages' contract (include/catenae/compressor.h, limiter.h, expander.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/compressor.h"
#include "catenae/expander.h"
#include "catenae/gain.h"
#include "catenae/limiter.h"
#include "catenae/parallel.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T>
    class dynamics_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(dynamics_formats, formats, );

    constexpr double k_np    = 0.11512925464970228420089957273422; // ln 10 / 20
    const double     k_inf   = std::numeric_limits<double>::infinity();
    constexpr double k_floor = 2.3283064365386962890625e-10;                       // 2^-32
    constexpr double k_rest  = 5.42101086242752217003726400434970855712890625e-20; // 2^-64

    template <typename S>
    catenae::compressor_design<S> compressor_of(double t, double r, double w, double a, double rel, double m) {
        const auto d = catenae::compressor_design<S>::make({t, r, w, a, rel, m});
        EXPECT_TRUE(d.has_value());
        return *d;
    }
    template <typename S>
    catenae::limiter_design<S> limiter_of(double t, double rel) {
        const auto d = catenae::limiter_design<S>::make({t, rel});
        EXPECT_TRUE(d.has_value());
        return *d;
    }
    template <typename S>
    catenae::expander_design<S> expander_of(double t, double r, double w, double range, double a, std::uint32_t h,
                                            double rel) {
        const auto d = catenae::expander_design<S>::make({t, r, w, range, a, h, rel});
        EXPECT_TRUE(d.has_value());
        return *d;
    }

    /// Programme-like input: noise whose level steps between quiet and loud every `hop` frames,
    /// the right channel half the left plus its own noise.
    template <typename S>
    std::vector<S> bursts(std::size_t frames, std::size_t hop, std::uint32_t seed, double loud = 2.0,
                          double quiet = 0.003) {
        catenae_test::signal s(seed);
        std::vector<S>       v(2 * frames);
        for (std::size_t f = 0; f < frames; ++f) {
            const double level = (f / hop) % 2 == 1 ? loud : quiet;
            const double a     = level * s.next<double>();
            v[2 * f]           = static_cast<S>(a);
            v[2 * f + 1]       = static_cast<S>(0.5 * a + 0.1 * level * s.next<double>());
        }
        return v;
    }

    /// One call of a stage over all of `in`, from reset.
    template <typename Stage, typename V>
    std::vector<V> run_once(const Stage& stage, const std::vector<V>& in) {
        std::vector<typename Stage::state> st(1); // value-initialised: zero, on the heap, however large
        catenae::stage_traits<Stage>::reset(stage, st[0]);
        typename Stage::workspace ws{};
        std::vector<V>            out(in.size());
        static_cast<void>(stage.run(st[0], ws, catenae::span<const V>(in), catenae::span<V>(out)));
        return out;
    }

    // ---- The references: the cards' equations, written out frame by frame. ----

    template <typename T>
    T level_of(const T* frame, std::size_t ch) {
        T peak = static_cast<T>(k_floor);
        for (std::size_t c = 0; c < ch; ++c) {
            const T a = std::fabs(frame[c]);
            peak      = a > peak ? a : peak;
        }
        return std::log(peak);
    }

    template <typename T>
    T rest(T x) {
        return x < static_cast<T>(k_rest) ? T{0} : x;
    }

    template <typename T>
    std::vector<T> reference_compressor(const catenae::compressor_numbers<T>& n, const std::vector<T>& in,
                                        std::size_t ch) {
        std::vector<T> out(in.size());
        T              y1 = 0;
        T              y  = 0;
        for (std::size_t f = 0; f < in.size() / ch; ++f) {
            const T x = level_of(&in[f * ch], ch);
            const T d = x - n.threshold;
            T       r = 0;
            if (d > n.half_knee) {
                r = n.above * d;
            }
            else if (!(d < -n.half_knee)) {
                r = n.knee_scale * (d + n.half_knee) * (d + n.half_knee);
            }
            const T held = n.release * y1 + n.release_complement * r;
            y1           = rest(r > held ? r : held);
            y            = rest(n.attack * y + n.attack_complement * y1);
            const T g    = std::exp(n.makeup - y);
            for (std::size_t c = 0; c < ch; ++c) {
                out[f * ch + c] = in[f * ch + c] * g;
            }
        }
        return out;
    }

    template <typename T>
    std::vector<T> reference_expander(const catenae::expander_numbers<T>& n, const std::vector<T>& in, std::size_t ch) {
        std::vector<T> out(in.size());
        T              y1   = n.range;
        T              y    = n.range;
        std::uint32_t  hold = 0;
        for (std::size_t f = 0; f < in.size() / ch; ++f) {
            const T x = level_of(&in[f * ch], ch);
            const T d = x - n.threshold;
            T       r = 0;
            if (!(d >= n.half_knee)) {
                r = d <= -n.half_knee ? n.below * -d : n.knee_scale * (d - n.half_knee) * (d - n.half_knee);
                r = r < n.range ? r : n.range;
            }
            if (!(r > y1)) {
                y1   = r;
                hold = n.hold;
            }
            else if (hold > 0) {
                --hold;
            }
            else {
                y1 = n.release * y1 + n.release_complement * r;
            }
            y1        = rest(y1);
            y         = rest(n.attack * y + n.attack_complement * y1);
            const T g = std::exp(-y);
            for (std::size_t c = 0; c < ch; ++c) {
                out[f * ch + c] = in[f * ch + c] * g;
            }
        }
        return out;
    }

    /// The largest of v[f - look .. f], those before 0 being 0.
    std::uint32_t window_max(const std::vector<std::uint32_t>& v, std::size_t f, std::size_t look) {
        std::uint32_t m = 0;
        for (std::size_t k = 0; k <= look && k <= f; ++k) {
            m = std::max(m, v[f - k]);
        }
        return m;
    }

    /// The mean of v[f - look .. f], rounded up, those before 0 being 0.
    std::uint32_t window_mean_up(const std::vector<std::uint32_t>& v, std::size_t f, std::size_t look) {
        std::uint64_t sum = 0;
        for (std::size_t k = 0; k <= look && k <= f; ++k) {
            sum += v[f - k];
        }
        return static_cast<std::uint32_t>((sum + look) / (look + 1));
    }

    /// The limiter by brute force: every window's maximum and every mean summed out, no wedge.
    /// `clamped` false leaves the last hold off, to see what it absorbs.
    template <typename T>
    std::vector<T> reference_limiter(const catenae::limiter_numbers<T>& n, const std::vector<T>& in, std::size_t ch,
                                     std::size_t look, bool clamped = true) {
        const std::size_t          frames = in.size() / ch;
        std::vector<std::uint32_t> q(frames);
        std::vector<std::uint32_t> m(frames);
        std::vector<T>             out(in.size());
        T                          y = 0;
        for (std::size_t f = 0; f < frames; ++f) {
            T r             = level_of(&in[f * ch], ch) - n.threshold;
            r               = std::min(std::max(r, T{0}), T{128});
            q[f]            = static_cast<std::uint32_t>(std::ceil(r * T{4096}));
            m[f]            = window_max(q, f, look);
            const auto a    = window_mean_up(m, f, look);
            const T    mean = static_cast<T>(a) / T{4096};
            const T    held = n.release * y + n.release_complement * mean;
            y               = rest(mean > held ? mean : held);
            const T g       = std::exp(-y);
            for (std::size_t c = 0; c < ch; ++c) {
                const T v       = f >= look ? in[(f - look) * ch + c] * g : T{0} * g;
                out[f * ch + c] = clamped ? (v > n.ceiling ? n.ceiling : (v < -n.ceiling ? -n.ceiling : v)) : v;
            }
        }
        return out;
    }

    /// A double signal rounded to float, sample by sample.
    std::vector<float> narrowed(const std::vector<double>& d) {
        std::vector<float> f(d.size());
        for (std::size_t i = 0; i < d.size(); ++i) {
            f[i] = static_cast<float>(d[i]);
        }
        return f;
    }

    /// The largest relative difference between a float output and the double one, over the
    /// samples the double one does not make tiny: the gains' difference, for a gain stage.
    double worst_relative(const std::vector<float>& f, const std::vector<double>& d) {
        double worst = 0.0;
        for (std::size_t i = 0; i < d.size(); ++i) {
            if (std::fabs(d[i]) > 1e-6) {
                worst = std::max(worst, std::fabs(static_cast<double>(f[i]) - d[i]) / std::fabs(d[i]));
            }
        }
        return worst;
    }

    template <typename T>
    bool same_bits(const std::vector<T>& a, const std::vector<T>& b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
    }

    /// The output level relative to the input, dB, after the stage has settled on a steady tone
    /// of amplitude `amp` (a square wave: its magnitude, and so its level, is constant).
    template <typename Stage, typename S>
    double settled_gain_db(const Stage& stage, double amp) {
        std::vector<S> in(2 * 4000);
        for (std::size_t f = 0; f < 4000; ++f) {
            in[2 * f]     = static_cast<S>(f % 2 == 0 ? amp : -amp);
            in[2 * f + 1] = static_cast<S>(0.25 * static_cast<double>(in[2 * f]));
        }
        const std::vector<S> out = run_once(stage, in);
        return 20.0 * std::log10(std::fabs(static_cast<double>(out[2 * 3999])) / amp);
    }

    // The paper's static curves, in dB.
    double compressor_curve_db(double x, double t, double r, double w, double m) {
        double y = x;
        if (2.0 * (x - t) > w) {
            y = t + (x - t) / r;
        }
        else if (2.0 * std::fabs(x - t) <= w && w > 0.0) {
            y = x + (1.0 / r - 1.0) * (x - t + w / 2.0) * (x - t + w / 2.0) / (2.0 * w);
        }
        return y - x + m;
    }
    double expander_curve_db(double x, double t, double r, double w, double range) {
        if (2.0 * (x - t) >= w) {
            return 0.0; // at and above the knee's top: at a ratio of infinity, (1 - R) 0 is no number
        }
        const double y =
            2.0 * (x - t) < -w ? t + r * (x - t) : x + (1.0 - r) * (x - t - w / 2.0) * (x - t - w / 2.0) / (2.0 * w);
        return std::max(y - x, -range);
    }

    // ---- The compressor. ----

    // Verifies CAT-COMPRESSOR-001.
    TYPED_TEST(dynamics_formats, TheCompressorIsItsCardsEquations) {
        const auto                   d   = compressor_of<TypeParam>(-24.0, 4.0, 6.0, 5.0, 400.0, 6.0);
        const std::vector<TypeParam> in  = bursts<TypeParam>(6000, 700, 11);
        const std::vector<TypeParam> out = run_once(catenae::basic_compressor<TypeParam, 2>(d), in);
        EXPECT_TRUE(same_bits(out, reference_compressor(d.numbers(), in, 2)));
        // In place, the same bits.
        std::vector<TypeParam> io = in;
        {
            const catenae::basic_compressor<TypeParam, 2> stage(d);
            typename decltype(stage)::state               st{};
            catenae::no_workspace                         ws{};
            static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(io), catenae::span<TypeParam>(io)));
        }
        EXPECT_TRUE(same_bits(io, out));
        // One level for the frame: the right channel is cut as the left is.
        for (std::size_t f = 1000; f < 6000; f += 97) {
            if (std::fabs(static_cast<double>(in[2 * f])) > 1e-3
                && std::fabs(static_cast<double>(in[2 * f + 1])) > 1e-3) {
                EXPECT_NEAR(static_cast<double>(out[2 * f] / in[2 * f]),
                            static_cast<double>(out[2 * f + 1] / in[2 * f + 1]), 1e-5)
                    << f;
            }
        }
        // The generic contract.
        const catenae::basic_compressor<TypeParam, 2> stage(d);
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
    }

    // Verifies CAT-COMPRESSOR-002.
    TEST(compressor, SettledItIsThePapersStaticCurve) {
        for (const double r : {1.0, 2.0, 4.0, k_inf}) {
            for (const double w : {0.0, 6.0, 12.0}) {
                const catenae::compressor_f64<2> stage(compressor_of<double>(-20.0, r, w, 1.0, 1.0, 3.0));
                for (double x = -40.0; x <= 6.0; x += 1.0) {
                    EXPECT_NEAR((settled_gain_db<decltype(stage), double>(stage, std::pow(10.0, x / 20.0))),
                                compressor_curve_db(x, -20.0, r, w, 3.0), 1e-9)
                        << "ratio " << r << ", knee " << w << ", at " << x << " dB";
                }
            }
        }
        // Float within its floor.
        const catenae::compressor<2> f(compressor_of<float>(-20.0, 4.0, 6.0, 1.0, 1.0, 0.0));
        for (double x = -40.0; x <= 6.0; x += 2.0) {
            EXPECT_NEAR((settled_gain_db<decltype(f), float>(f, std::pow(10.0, x / 20.0))),
                        compressor_curve_db(x, -20.0, 4.0, 6.0, 0.0), 1e-4)
                << x;
        }
    }

    // Verifies CAT-COMPRESSOR-002.
    TEST(compressor, ItsTimeConstantsAreFramesToOneOverE) {
        // From reset, a step to a steady level 12 dB over a hard-knee threshold at a ratio of
        // infinity: the reduction wanted is 12 dB, and the attack reaches 1 - 1/e of it in tau.
        constexpr double    tau = 40.0;
        const auto          d   = compressor_of<double>(-24.0, k_inf, 0.0, tau, 1e6, 0.0);
        std::vector<double> in(2 * 400, std::pow(10.0, -12.0 / 20.0));
        const auto          out = run_once(catenae::compressor_f64<2>(d), in);
        const double        cut = -20.0 * std::log10(out[2 * 39] / in[2 * 39]); // after 40 frames
        EXPECT_NEAR(cut, 12.0 * (1.0 - std::exp(-1.0)), 1e-9);
        // The release, with an instant attack: from 12 dB of reduction, silence lets it fall to
        // 12 / e in tau frames (the floor's level is far below the threshold).
        const auto                        r = compressor_of<double>(-24.0, k_inf, 0.0, 0.0, tau, 0.0);
        catenae::compressor_f64<1>        stage(r);
        catenae::compressor_f64<1>::state st{};
        catenae::no_workspace             ws{};
        std::vector<double>               loud(100, std::pow(10.0, -12.0 / 20.0));
        std::vector<double>               scratch(100);
        static_cast<void>(stage.run(st, ws, catenae::span<const double>(loud), catenae::span<double>(scratch)));
        EXPECT_NEAR(st.reduced, 12.0 * k_np, 1e-12);
        std::vector<double> quiet(40, 0.0);
        static_cast<void>(stage.run(st, ws, catenae::span<const double>(quiet), catenae::span<double>(scratch)));
        EXPECT_NEAR(st.reduced, 12.0 * k_np * std::exp(-1.0), 1e-12);
    }

    // Verifies CAT-COMPRESSOR-003.
    TYPED_TEST(dynamics_formats, TheCompressorComesToRestExactly) {
        using comp_t = catenae::basic_compressor<TypeParam, 2>;
        static_assert(catenae::stage_traits<comp_t>::has_exact_latency
                          && catenae::stage_traits<comp_t>::exact_latency == (catenae::ratio{0, 1})
                          && catenae::stage_traits<comp_t>::rests_at_zero,
                      "latency 0, at rest at zero");
        const comp_t stage(compressor_of<TypeParam>(-30.0, 8.0, 0.0, 2.0, 100.0, 0.0));
        EXPECT_EQ(stage.tail_frames(), 0u);
        typename comp_t::state st{};
        catenae::no_workspace  ws{};
        std::vector<TypeParam> loud = bursts<TypeParam>(500, 1000, 3, 0.0, 1.0);
        std::vector<TypeParam> out(loud.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(loud), catenae::span<TypeParam>(out)));
        ASSERT_GT(st.reduced, TypeParam{0});
        std::vector<TypeParam> silence(2 * 64, TypeParam{0});
        std::size_t            frames = 0;
        while ((st.held > TypeParam{0} || st.reduced > TypeParam{0}) && frames < 20000) {
            static_cast<void>(
                stage.run(st, ws, catenae::span<const TypeParam>(silence), catenae::span<TypeParam>(out)));
            frames += 64;
            for (std::size_t i = 0; i < silence.size(); ++i) {
                ASSERT_EQ(out[i], TypeParam{0}) << "silence in is silence out";
            }
        }
        EXPECT_LT(frames, 20000u) << "the detector reached 0";
        EXPECT_EQ(st.held, TypeParam{0});
        EXPECT_EQ(st.reduced, TypeParam{0});
    }

    // Verifies CAT-COMPRESSOR-004.
    TYPED_TEST(dynamics_formats, ANewCompressorDesignAppliesToTheStateAsItStands) {
        const auto                              a  = compressor_of<TypeParam>(-24.0, 4.0, 6.0, 5.0, 400.0, 0.0);
        const auto                              b  = compressor_of<TypeParam>(-12.0, 2.0, 0.0, 1.0, 50.0, 3.0);
        const std::vector<TypeParam>            in = bursts<TypeParam>(2000, 300, 5);
        catenae::basic_compressor<TypeParam, 2> stage(a);
        typename catenae::basic_compressor<TypeParam, 2>::state st{};
        catenae::no_workspace                                   ws{};
        std::vector<TypeParam>                                  out(in.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data(), 2000),
                                    catenae::span<TypeParam>(out.data(), 2000)));
        auto held = st;
        stage.set_design(b);
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data() + 2000, 2000),
                                    catenae::span<TypeParam>(out.data() + 2000, 2000)));
        const catenae::basic_compressor<TypeParam, 2> fresh(b);
        std::vector<TypeParam>                        want(2000);
        static_cast<void>(fresh.run(held, ws, catenae::span<const TypeParam>(in.data() + 2000, 2000),
                                    catenae::span<TypeParam>(want)));
        EXPECT_EQ(std::memcmp(want.data(), out.data() + 2000, 2000 * sizeof(TypeParam)), 0);
    }

    // Verifies CAT-COMPRESSOR-005.
    TEST(compressor, FloatFollowsTheDoubleCompressor) {
        const std::vector<double> ind = bursts<double>(20000, 1500, 21);
        const std::vector<float>  inf = narrowed(ind);
        std::vector<double>       back(inf.begin(), inf.end()); // the same samples in both
        const auto                od =
            run_once(catenae::compressor_f64<2>(compressor_of<double>(-24.0, 4.0, 6.0, 5.0, 400.0, 6.0)), back);
        const auto   of = run_once(catenae::compressor<2>(compressor_of<float>(-24.0, 4.0, 6.0, 5.0, 400.0, 6.0)), inf);
        const double worst = worst_relative(of, od);
        // Relative, so the gains' difference: measured 8.4e-6 on the host (7.3e-5 dB), the
        // detector's one-poles adding a rounding a frame over the release's 400; the bound is 1.6e-5.
        EXPECT_LT(worst, 1.6e-5) << worst;
    }

    // Verifies CAT-COMPRESSOR-001.
    TEST(compressor, ANaNPassesAndAnInfinityHoldsTheGainAtZero) {
        const catenae::compressor<2> stage(compressor_of<float>(-24.0, 4.0, 0.0, 5.0, 400.0, 0.0));
        std::vector<float>           in(2 * 10, 0.5f);
        in[2 * 3]              = std::numeric_limits<float>::quiet_NaN();
        std::vector<float> out = run_once(stage, in);
        EXPECT_TRUE(std::isnan(out[2 * 3]));
        EXPECT_FALSE(std::isnan(out[2 * 3 + 1])); // its frame's level is the right channel's
        const std::vector<float> clean = [&] {
            std::vector<float> c(in);
            c[2 * 3] = 0.5f;
            return run_once(stage, c);
        }();
        EXPECT_EQ(out[2 * 9], clean[2 * 9]) << "a NaN is not read as a level";
        in[2 * 3] = std::numeric_limits<float>::infinity();
        out       = run_once(stage, in);
        for (std::size_t f = 4; f < 10; ++f) {
            EXPECT_EQ(out[2 * f], 0.0f) << f;
        }
    }

    // ---- The expander. ----

    // Verifies CAT-EXPANDER-001.
    TYPED_TEST(dynamics_formats, TheExpanderIsItsCardsEquations) {
        for (const double r : {2.0, k_inf}) {
            const auto                   d   = expander_of<TypeParam>(-40.0, r, 6.0, 50.0, 3.0, 50, 300.0);
            const std::vector<TypeParam> in  = bursts<TypeParam>(6000, 700, 13, 0.5, 0.002);
            const std::vector<TypeParam> out = run_once(catenae::basic_expander<TypeParam, 2>(d), in);
            EXPECT_TRUE(same_bits(out, reference_expander(d.numbers(), in, 2))) << r;
            const catenae::basic_expander<TypeParam, 2> stage(d);
            EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
            EXPECT_TRUE(catenae_test::counts_are_exact(stage));
            EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
            EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
        }
    }

    // Verifies CAT-EXPANDER-002.
    TEST(expander, SettledItIsTheStaticCurveWithinItsRange) {
        for (const double r : {1.5, 3.0, k_inf}) {
            for (const double w : {0.0, 10.0}) {
                const catenae::expander_f64<2> stage(expander_of<double>(-40.0, r, w, 30.0, 1.0, 0, 1.0));
                for (double x = -90.0; x <= 0.0; x += 1.0) {
                    if (std::isinf(r) && std::fabs(x - (-40.0 + w / 2.0)) < 1e-6) {
                        continue; // a gate steps from 0 to the range here; rounding picks the side (card)
                    }
                    EXPECT_NEAR((settled_gain_db<decltype(stage), double>(stage, std::pow(10.0, x / 20.0))),
                                expander_curve_db(x, -40.0, r, w, 30.0), 1e-9)
                        << "ratio " << r << ", knee " << w << ", at " << x << " dB";
                }
            }
        }
        // Open, the output is the input bit for bit.
        const catenae::expander<2> f(expander_of<float>(-40.0, 4.0, 0.0, 30.0, 1.0, 0, 1.0));
        std::vector<float>         in(2 * 3000);
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = (i / 2) % 2 == 0 ? 0.5f : -0.25f; // a steady level, far above the threshold
        }
        const auto out = run_once(f, in);
        for (std::size_t i = 2 * 200; i < in.size(); ++i) {
            ASSERT_EQ(out[i], in[i]) << i;
        }
    }

    // Verifies CAT-EXPANDER-003.
    TEST(expander, ItHoldsBeforeItClosesAndStartsClosed) {
        constexpr std::uint32_t hold = 100;
        const auto              d    = expander_of<double>(-40.0, 4.0, 0.0, 40.0, 0.0, hold, 50.0);
        // Open on a loud tone for 300 frames, then a tone 20 dB under the threshold.
        std::vector<double> in(2 * 600);
        for (std::size_t f = 0; f < 600; ++f) {
            const double a = f < 300 ? 0.5 : std::pow(10.0, -60.0 / 20.0);
            in[2 * f]      = f % 2 == 0 ? a : -a;
            in[2 * f + 1]  = in[2 * f];
        }
        const auto out = run_once(catenae::expander_f64<2>(d), in);
        for (std::size_t f = 300; f < 300 + hold; ++f) {
            EXPECT_EQ(out[2 * f], in[2 * f]) << "held open at " << f;
        }
        EXPECT_LT(std::fabs(out[2 * (300 + hold + 1)]), std::fabs(in[2 * (300 + hold + 1)])) << "then it closes";
        // From reset it is closed: the first frame of a loud tone is cut by aA times the range.
        const auto slow = expander_of<double>(-40.0, 4.0, 0.0, 40.0, 10.0, 0, 50.0);
        const auto o2   = run_once(catenae::expander_f64<2>(slow), in);
        EXPECT_NEAR(o2[0] / in[0], std::exp(-std::exp(-0.1) * 40.0 * k_np), 1e-12);
        static_assert(catenae::stage_traits<catenae::expander<2>>::exact_latency == (catenae::ratio{0, 1}), "0");
        EXPECT_EQ(catenae::expander<2>(expander_of<float>(-40.0, 4.0, 0.0, 40.0, 0.0, 0, 50.0)).tail_frames(), 0u);
    }

    // Verifies CAT-EXPANDER-004.
    TYPED_TEST(dynamics_formats, ANewExpanderDesignAppliesToTheStateAsItStands) {
        const auto                            a  = expander_of<TypeParam>(-40.0, 2.0, 0.0, 50.0, 3.0, 20, 300.0);
        const auto                            b  = expander_of<TypeParam>(-30.0, k_inf, 0.0, 80.0, 1.0, 0, 100.0);
        const std::vector<TypeParam>          in = bursts<TypeParam>(2000, 300, 7, 0.5, 0.002);
        catenae::basic_expander<TypeParam, 2> stage(a);
        typename catenae::basic_expander<TypeParam, 2>::state st{};
        stage.reset(st);
        catenae::no_workspace  ws{};
        std::vector<TypeParam> out(in.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data(), 2000),
                                    catenae::span<TypeParam>(out.data(), 2000)));
        auto held = st;
        stage.set_design(b);
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data() + 2000, 2000),
                                    catenae::span<TypeParam>(out.data() + 2000, 2000)));
        const catenae::basic_expander<TypeParam, 2> fresh(b);
        std::vector<TypeParam>                      want(2000);
        static_cast<void>(fresh.run(held, ws, catenae::span<const TypeParam>(in.data() + 2000, 2000),
                                    catenae::span<TypeParam>(want)));
        EXPECT_EQ(std::memcmp(want.data(), out.data() + 2000, 2000 * sizeof(TypeParam)), 0);
    }

    // Verifies CAT-EXPANDER-005.
    TEST(expander, FloatFollowsTheDoubleExpander) {
        const std::vector<double> ind = bursts<double>(20000, 1500, 23, 0.5, 0.002);
        const std::vector<float>  inf = narrowed(ind);
        std::vector<double>       back(inf.begin(), inf.end());
        const auto                od =
            run_once(catenae::expander_f64<2>(expander_of<double>(-40.0, 3.0, 6.0, 50.0, 3.0, 50, 300.0)), back);
        const auto of = run_once(catenae::expander<2>(expander_of<float>(-40.0, 3.0, 6.0, 50.0, 3.0, 50, 300.0)), inf);
        const double worst = worst_relative(of, od);
        // Relative: measured 5.0e-6 on the host; the bound is 1e-5.
        EXPECT_LT(worst, 1e-5) << worst;
    }

    // ---- The limiter. ----

    // Verifies CAT-LIMITER-001.
    TYPED_TEST(dynamics_formats, TheLimiterIsItsCardsEquations) {
        const auto                   d  = limiter_of<TypeParam>(-6.0, 200.0);
        const std::vector<TypeParam> in = bursts<TypeParam>(5000, 333, 17, 4.0, 0.2);
        const auto                   o1 = run_once(catenae::basic_limiter<TypeParam, 2, 1>(d), in);
        EXPECT_TRUE(same_bits(o1, reference_limiter(d.numbers(), in, 2, 1)));
        const auto o48 = run_once(catenae::basic_limiter<TypeParam, 2, 48>(d), in);
        EXPECT_TRUE(same_bits(o48, reference_limiter(d.numbers(), in, 2, 48)));
        const catenae::basic_limiter<TypeParam, 2, 48> stage(d);
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
        // In place.
        std::vector<TypeParam> io = in;
        {
            typename catenae::basic_limiter<TypeParam, 2, 48>::state st{};
            catenae::no_workspace                                    ws{};
            static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(io), catenae::span<TypeParam>(io)));
        }
        EXPECT_TRUE(same_bits(io, o48));
    }

    /// The signals the ceiling is tried on: noise bursts far over it, lone spikes after silence,
    /// a square wave at +40 dB, and spikes every few frames.
    template <typename S>
    std::vector<std::vector<S>> hard_cases() {
        std::vector<std::vector<S>> cases;
        cases.push_back(bursts<S>(8000, 211, 31, 100.0, 0.5));
        std::vector<S>       spikes(2 * 8000, S{0});
        catenae_test::signal s(41);
        for (std::size_t f = 50; f < 8000; f += 37 + s.next<std::uint32_t>() % 300) {
            spikes[2 * f] = static_cast<S>(1e6 * s.next<double>());
        }
        cases.push_back(spikes);
        std::vector<S> square(2 * 8000);
        for (std::size_t f = 0; f < 8000; ++f) {
            square[2 * f]     = static_cast<S>((f / 60) % 2 == 0 ? 100.0 : -100.0);
            square[2 * f + 1] = static_cast<S>(0.1);
        }
        cases.push_back(square);
        std::vector<S> dense(2 * 8000, static_cast<S>(0.01));
        for (std::size_t f = 0; f < 8000; f += 5) {
            dense[2 * f + 1] = static_cast<S>(f % 2 == 0 ? 30.0 : -30.0);
        }
        cases.push_back(dense);
        return cases;
    }

    template <typename S, std::size_t Look>
    void the_ceiling_holds(int& worst_ulps) {
        const auto                               d = limiter_of<S>(-3.0, 300.0);
        const catenae::basic_limiter<S, 2, Look> stage(d);
        const S                                  ceiling = d.numbers().ceiling;
        for (const auto& in : hard_cases<S>()) {
            const auto out = run_once(stage, in);
            const auto raw = reference_limiter(d.numbers(), in, 2, Look, false);
            for (std::size_t i = 0; i < out.size(); ++i) {
                ASSERT_LE(std::fabs(out[i]), ceiling) << Look << " at " << i;
                // What the last hold absorbed, in ULP of the ceiling.
                int     ulps = 0;
                S       v    = ceiling;
                const S a    = std::fabs(raw[i]);
                while (a > v && ulps < 1000) {
                    v = std::nextafter(v, std::numeric_limits<S>::infinity());
                    ++ulps;
                }
                worst_ulps = std::max(worst_ulps, ulps);
            }
        }
    }

    // Verifies CAT-LIMITER-002.
    TYPED_TEST(dynamics_formats, NoSampleExceedsTheCeiling) {
        int worst = 0;
        the_ceiling_holds<TypeParam, 1>(worst);
        the_ceiling_holds<TypeParam, 7>(worst);
        the_ceiling_holds<TypeParam, 64>(worst);
        // Measured: see the bound. The hold absorbs log's and exp's rounding, never a reduction.
        EXPECT_LE(worst, 4) << worst;
    }

    // Verifies CAT-LIMITER-003.
    TYPED_TEST(dynamics_formats, BelowTheThresholdItIsADelay) {
        using lim_t = catenae::basic_limiter<TypeParam, 2, 32>;
        static_assert(catenae::stage_traits<lim_t>::exact_latency == (catenae::ratio{32, 1})
                          && catenae::stage_traits<lim_t>::rests_at_zero,
                      "the look-ahead, at rest at zero");
        const lim_t stage(limiter_of<TypeParam>(-1.0, 100.0));
        EXPECT_EQ(stage.tail_frames(), 32u);
        EXPECT_EQ(stage.latency().nominal, 32.0f);
        const std::vector<TypeParam> in  = bursts<TypeParam>(3000, 400, 19, 0.8, 0.1);
        const auto                   out = run_once(stage, in);
        for (std::size_t f = 0; f < 3000; ++f) {
            for (std::size_t c = 0; c < 2; ++c) {
                const TypeParam want = f < 32 ? TypeParam{0} : in[2 * (f - 32) + c];
                ASSERT_EQ(out[2 * f + c], want) << f;
            }
        }
    }

    // Verifies CAT-LIMITER-004.
    TYPED_TEST(dynamics_formats, ANewLimiterDesignAppliesToTheStateAsItStands) {
        using lim_t                     = catenae::basic_limiter<TypeParam, 2, 16>;
        const auto                   a  = limiter_of<TypeParam>(-6.0, 200.0);
        const auto                   b  = limiter_of<TypeParam>(-1.0, 20.0);
        const std::vector<TypeParam> in = bursts<TypeParam>(2000, 300, 9, 3.0, 0.2);
        lim_t                        stage(a);
        typename lim_t::state        st{};
        catenae::no_workspace        ws{};
        std::vector<TypeParam>       out(in.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data(), 2000),
                                    catenae::span<TypeParam>(out.data(), 2000)));
        auto held = st;
        stage.set_design(b);
        static_cast<void>(stage.run(st, ws, catenae::span<const TypeParam>(in.data() + 2000, 2000),
                                    catenae::span<TypeParam>(out.data() + 2000, 2000)));
        const lim_t            fresh(b);
        std::vector<TypeParam> want(2000);
        static_cast<void>(fresh.run(held, ws, catenae::span<const TypeParam>(in.data() + 2000, 2000),
                                    catenae::span<TypeParam>(want)));
        EXPECT_EQ(std::memcmp(want.data(), out.data() + 2000, 2000 * sizeof(TypeParam)), 0);
    }

    // Verifies CAT-LIMITER-005.
    TEST(limiter, FloatFollowsTheDoubleLimiter) {
        const std::vector<double> ind = bursts<double>(20000, 1500, 29, 4.0, 0.2);
        const std::vector<float>  inf = narrowed(ind);
        std::vector<double>       back(inf.begin(), inf.end());
        const auto                od    = run_once(catenae::limiter_f64<2, 48>(limiter_of<double>(-3.0, 300.0)), back);
        const auto                of    = run_once(catenae::limiter<2, 48>(limiter_of<float>(-3.0, 300.0)), inf);
        const double              worst = worst_relative(of, od);
        // Relative: measured 2.0e-6 on the host, the window exact and only the release a one-pole;
        // the bound is 4e-6.
        EXPECT_LT(worst, 4e-6) << worst;
    }

    // Verifies CAT-LIMITER-001.
    TEST(limiter, ANonFiniteSampleIsItsOwnOutputAlone) {
        const catenae::limiter<2, 8> stage(limiter_of<float>(-1.0, 50.0));
        std::vector<float>           in(2 * 4000, 0.25f);
        in[2 * 100]    = std::numeric_limits<float>::quiet_NaN();
        const auto out = run_once(stage, in);
        EXPECT_TRUE(std::isnan(out[2 * 108]));
        EXPECT_EQ(out[2 * 108 + 1], 0.25f) << "not read as a level";
        in[2 * 100]   = std::numeric_limits<float>::infinity();
        const auto o2 = run_once(stage, in);
        EXPECT_TRUE(std::isnan(o2[2 * 108])) << "inf times a gain of 0";
        for (std::size_t i = 0; i < o2.size(); ++i) {
            if (i != 2 * 108) {
                ASSERT_LE(std::fabs(o2[i]), 0.25f) << i;
            }
        }
        EXPECT_EQ(o2[2 * 3999], 0.25f) << "and the release recovers";
    }

    // ---- In a chain, and beside a dry arm. ----

    // Verifies CAT-LIMITER-003.
    TEST(dynamics, TheyChainAndTheLimiterAlignsADryArm) {
        using strip = catenae::chain<catenae::expander<2>, catenae::compressor<2>, catenae::limiter<2, 32>>;
        static_assert(catenae::stage_traits<strip>::exact_latency == (catenae::ratio{32, 1}), "the look-ahead");
        using arms = catenae::parallel<catenae::limiter<2, 32>, catenae::gain<2>>;
        static_assert(catenae::stage_traits<arms>::exact_latency == (catenae::ratio{32, 1}), "aligned by type");
    }

} // namespace
