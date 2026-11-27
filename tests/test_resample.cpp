/// @file test_resample.cpp
/// @brief resample's contract (include/catenae/resample.h) and its generated designs
/// (include/catenae/nyquist_tables.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/resample.h"
#include "catenae/sample_traits.h"
#include "catenae_test/harness.h"
#include "catenae_test/nyquist_test_tables.h"

namespace {

    template <typename T>
    class resample_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double, catenae::q15, catenae::q31, catenae::q4_27>;
    TYPED_TEST_SUITE(resample_formats, formats, );

    template <std::uint32_t L, std::uint32_t M>
    using r = catenae::ratio_t<L, M>;
    template <std::size_t Band>
    using short_design = catenae::nyquist_design<Band, 2, 70>;

    constexpr double k_pi = 3.141592653589793;

    // ---- The designs ---------------------------------------------------------------------------

    /// |H(f)| of a prototype over its branch's DC gain, in dB; f in cycles per composite sample.
    template <typename Design>
    double response_db(double f) {
        std::complex<double>       sum{};
        const std::complex<double> turn = std::polar(1.0, -2.0 * k_pi * f);
        std::complex<double>       at{1.0, 0.0};
        for (const double h : Design::prototype) {
            sum += h * at;
            at *= turn;
        }
        return 20.0 * std::log10(std::abs(sum) / static_cast<double>(Design::band));
    }

    /// The largest response from `from` to 1/2.
    template <typename Design>
    double worst_stopband_db(double from) {
#if defined(CATENAE_TEST_EMULATED)
        constexpr int grid = 1024;
#else
        constexpr int grid = 8192;
#endif
        double worst = -std::numeric_limits<double>::infinity();
        for (int i = 0; i <= grid; ++i) {
            const double f = from + (0.5 - from) * static_cast<double>(i) / grid;
            worst          = std::max(worst, response_db<Design>(f));
        }
        return worst;
    }

    template <typename Design>
    void is_a_nyquist_design() {
        static_assert(catenae::detail::is_nyquist_table<Design>(), "the Nyquist structure");
        static_assert(Design::taps == 2 * Design::taps_per_branch * Design::band - 1, "N = 2 m B - 1");
        for (std::size_t j = 0; j < Design::band; ++j) {
            double sum = 0.0;
            for (std::size_t i = j; i < Design::taps; i += Design::band) {
                sum += Design::prototype[i];
            }
            EXPECT_NEAR(sum, 1.0, 1e-15) << "band " << Design::band << " branch " << j;
        }
    }

    // Verifies CAT-RESAMPLE-002.
    TEST(nyquist_tables, EveryDesignIsNyquistAndThePinsMeetTheirStopband) {
        is_a_nyquist_design<catenae::half_band<1>>();
        is_a_nyquist_design<catenae::half_band<2>>();
        is_a_nyquist_design<short_design<2>>();
        is_a_nyquist_design<short_design<3>>();
        is_a_nyquist_design<short_design<4>>();
        is_a_nyquist_design<short_design<6>>();
        is_a_nyquist_design<short_design<8>>();
        static_assert(catenae::half_band<1>::taps == 39 && catenae::half_band<2>::taps == 19, "m = 10 and 5");
        // Economy: the passband 3/8 of the chain's lowest rate at 70 dB, pinned with SampleRateTap's
        // 1 dB of margin. At divisor 1 the stopband is from 5/8 of the stage's lower rate, 5/16 of
        // the composite; relaxed at divisor 2 the passband is 3/16 of its own and the stopband from
        // 13/32.
        EXPECT_LT(worst_stopband_db<catenae::half_band<1>>(5.0 / 16.0), -71.0);
        EXPECT_LT(worst_stopband_db<catenae::half_band<2>>(13.0 / 32.0), -71.0);
        EXPECT_NEAR(response_db<catenae::half_band<1>>(0.0), 0.0, 1e-12);
    }

    // ---- The reference, summed in the card's order ----------------------------------------------

    /// A stage's shape, read from its type.
    template <typename Stage>
    struct geometry {
        static constexpr std::size_t l    = Stage::rate_ratio.up;
        static constexpr std::size_t m    = Stage::rate_ratio.down;
        static constexpr std::size_t ch   = Stage::input_port::channels;
        static constexpr std::size_t len  = Stage::row_length();
        static constexpr std::size_t rows = l == 1 ? m : l;
    };

    /// Row r's nonzero span, [first, last).
    template <typename Stage>
    std::pair<std::size_t, std::size_t> span_of(std::size_t r) {
        using g           = geometry<Stage>;
        std::size_t first = 0;
        while (first < g::len && catenae::detail::is_zero_coeff(Stage::coefficient(r, first))) {
            ++first;
        }
        std::size_t last = g::len;
        while (last > first && catenae::detail::is_zero_coeff(Stage::coefficient(r, last - 1))) {
            --last;
        }
        return {first, last};
    }

    /// The card's formula in the card's order for output n of channel c: rows in order, each
    /// oldest tap first, in the format's arithmetic; float in four partial sums of each row's
    /// taps, double and fixed point in the traits' mac chain and one rounding.
    template <typename Stage, typename V>
    V one_output(const std::vector<V>& x, std::size_t n, std::size_t c) {
        using g                                      = geometry<Stage>;
        using traits                                 = catenae::sample_traits<V>;
        [[maybe_unused]] float                  s[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        [[maybe_unused]] typename traits::accum acc{};
        const auto add = [&](std::size_t row, std::ptrdiff_t newest, std::ptrdiff_t stride) {
            const auto span = span_of<Stage>(row);
            for (std::size_t t = span.first; t < span.second; ++t) {
                const std::ptrdiff_t i = newest - static_cast<std::ptrdiff_t>(g::len - 1 - t) * stride;
                const V              v = i < 0 ? V{0} : x[static_cast<std::size_t>(i) * g::ch + c];
                const auto           k = Stage::coefficient(row, t);
                if constexpr (std::is_same<V, float>::value) {
                    s[(t - span.first) % 4] += k * v;
                }
                else {
                    acc = traits::mac(acc, v, k);
                }
            }
        };
        if constexpr (g::l == 1) {
            for (std::size_t j = 0; j < g::m; ++j) {
                add(j, static_cast<std::ptrdiff_t>(n * g::m) - static_cast<std::ptrdiff_t>(j),
                    static_cast<std::ptrdiff_t>(g::m));
            }
        }
        else {
            add((n * g::m) % g::l, static_cast<std::ptrdiff_t>((n * g::m) / g::l), 1);
        }
        if constexpr (std::is_same<V, float>::value) {
            return (s[0] + s[1]) + (s[2] + s[3]);
        }
        else if constexpr (Stage::table_gain() != 1) {
            return catenae::finalize_divided<V, static_cast<std::uint32_t>(Stage::table_gain())>(acc);
        }
        else {
            return traits::finalize(acc);
        }
    }

    /// Every output of the formula from rest.
    template <typename Stage, typename V>
    std::vector<V> by_formula(const std::vector<V>& x) {
        using g                = geometry<Stage>;
        const std::size_t outs = Stage::outputs_from_rest(x.size() / g::ch);
        std::vector<V>    y(outs * g::ch);
        for (std::size_t n = 0; n < outs; ++n) {
            for (std::size_t c = 0; c < g::ch; ++c) {
                y[n * g::ch + c] = one_output<Stage>(x, n, c);
            }
        }
        return y;
    }

    /// The stage from reset over `x`, in calls of `chunk` frames.
    template <typename Stage, typename V>
    std::vector<V> run_stage(const std::vector<V>& x, std::size_t chunk) {
        using g = geometry<Stage>;
        const Stage                  stage{};
        static typename Stage::state st;
        catenae::no_workspace        ws{};
        stage.reset(st);
        const std::size_t frames = x.size() / g::ch;
        std::vector<V>    y(Stage::outputs_from_rest(frames) * g::ch);
        std::size_t       done = 0;
        std::size_t       made = 0;
        while (done < frames) {
            const std::size_t n    = std::min(chunk, frames - done);
            const std::size_t outs = stage.outputs_for(st, n);
            made += stage.run(st, ws, catenae::span<const V>(x.data() + done * g::ch, n * g::ch),
                              catenae::span<V>(y.data() + made * g::ch, outs * g::ch));
            done += n;
        }
        EXPECT_EQ(made * g::ch, y.size());
        return y;
    }

    template <typename S>
    std::vector<catenae::storage_t<S>> noise(std::size_t values, std::uint32_t seed) {
        catenae_test::signal               s(seed);
        std::vector<catenae::storage_t<S>> v(values);
        for (auto& x : v) {
            x = s.next<catenae::storage_t<S>>();
        }
        return v;
    }

    template <typename S, std::size_t Ch, typename Ratio, typename Design>
    void machine_is_the_formula() {
        using stage                  = catenae::basic_resample<S, Ch, Ratio, Design>;
        using v                      = catenae::storage_t<S>;
        constexpr std::size_t frames = 240;
        const std::vector<v>  x      = noise<S>(frames * Ch, 3u + Ratio::up * 7u + Ratio::down);
        const std::vector<v>  want   = by_formula<stage>(x);
        for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{64}, frames}) {
            const std::vector<v> got = run_stage<stage>(x, chunk);
            ASSERT_EQ(got.size(), want.size());
            ASSERT_EQ(std::memcmp(got.data(), want.data(), got.size() * sizeof(v)), 0)
                << Ratio::up << "/" << Ratio::down << ", " << Ch << " channels, chunks of " << chunk;
        }
    }

    // Verifies CAT-RESAMPLE-001.
    TYPED_TEST(resample_formats, EachMachineIsTheCardsFormula) {
        machine_is_the_formula<TypeParam, 2, r<2, 1>, catenae::half_band<1>>();
        machine_is_the_formula<TypeParam, 2, r<2, 1>, catenae::half_band<2>>();
        machine_is_the_formula<TypeParam, 2, r<1, 2>, catenae::half_band<1>>();
        machine_is_the_formula<TypeParam, 3, r<1, 2>, catenae::half_band<2>>();
        machine_is_the_formula<TypeParam, 1, r<3, 1>, short_design<3>>();
        machine_is_the_formula<TypeParam, 2, r<1, 3>, short_design<3>>();
        machine_is_the_formula<TypeParam, 2, r<3, 2>, short_design<3>>();
        machine_is_the_formula<TypeParam, 2, r<2, 3>, short_design<3>>();
        machine_is_the_formula<TypeParam, 1, r<4, 3>, short_design<4>>();
        machine_is_the_formula<TypeParam, 2, r<3, 4>, short_design<4>>();
        machine_is_the_formula<TypeParam, 1, r<1, 6>, short_design<6>>();
        machine_is_the_formula<TypeParam, 1, r<8, 3>, short_design<8>>();
        machine_is_the_formula<TypeParam, 2, r<3, 8>, short_design<8>>();
        machine_is_the_formula<TypeParam, 1, r<1, 8>, short_design<8>>();
    }

    // ---- The rows --------------------------------------------------------------------------------

    template <typename Stage>
    std::int64_t row_sum(std::size_t row) {
        std::int64_t sum = 0;
        for (std::size_t t = 0; t < Stage::row_length(); ++t) {
            sum += Stage::coefficient(row, t);
        }
        return sum;
    }

    // Verifies CAT-RESAMPLE-002.
    TEST(resample, TheRowsAreTheCards) {
        using h1 = catenae::half_band<1>;
        // Going up the rows are the design's branches, tap-reversed.
        using up = catenae::up2_f64<1>;
        for (std::size_t p = 0; p < 2; ++p) {
            for (std::size_t k = 0; k < up::row_length(); ++k) {
                const std::size_t i = p + 2 * k;
                EXPECT_EQ(up::coefficient(p, up::row_length() - 1 - k), i < h1::taps ? h1::prototype[i] : 0.0);
            }
        }
        // A decimator's are h / M, right-aligned to the newest sample.
        using down = catenae::down2_f64<1>;
        for (std::size_t j = 0; j < 2; ++j) {
            for (std::size_t s = 0; s < (h1::taps - j + 1) / 2; ++s) {
                EXPECT_EQ(down::coefficient(j, down::row_length() - 1 - s), h1::prototype[j + 2 * s] / 2.0);
            }
        }
        // A mixed ratio going down: each phase at unity.
        using mixed = catenae::resample_f64<1, r<2, 3>, short_design<3>>;
        for (std::size_t p = 0; p < 2; ++p) {
            double sum = 0.0;
            for (std::size_t t = 0; t < mixed::row_length(); ++t) {
                sum += mixed::coefficient(p, t);
            }
            EXPECT_NEAR(sum, 1.0, 1e-15);
        }
        // Fixed point: every row at its gain, exactly. Q15 up and Q31 every row at unity; a Q15
        // decimator's branches each at unity (gain M); a Q15 mixed ratio going down at G.
        static_assert(catenae::up2_q15<1>::table_gain() == 1 && catenae::down2_q31<1>::table_gain() == 1, "unity");
        static_assert(catenae::down2_q15<1>::table_gain() == 2, "M");
        static_assert(catenae::resample_q15<1, r<1, 3>, short_design<3>>::table_gain() == 3, "M");
        static_assert(catenae::resample_q15<1, r<2, 3>, short_design<3>>::table_gain() == 2, "2/3: G = 2");
        static_assert(catenae::resample_q15<1, r<3, 4>, short_design<4>>::table_gain() == 2, "3/4: G = 2");
        static_assert(catenae::resample_q15<1, r<3, 8>, short_design<8>>::table_gain() == 4, "3/8: G = 4");
        EXPECT_EQ(row_sum<catenae::up2_q15<1>>(0), 16384);
        EXPECT_EQ(row_sum<catenae::up2_q15<1>>(1), 16384);
        EXPECT_EQ((row_sum<catenae::up2_q31<1, 2>>(0)), std::int64_t{1} << 30);
        EXPECT_EQ(row_sum<catenae::down2_q15<1>>(0), 16384);
        EXPECT_EQ(row_sum<catenae::down2_q15<1>>(1), 16384);
        EXPECT_EQ(row_sum<catenae::down2_q31<1>>(0) + row_sum<catenae::down2_q31<1>>(1), std::int64_t{1} << 30);
        using q15_23 = catenae::resample_q15<1, r<2, 3>, short_design<3>>;
        EXPECT_EQ(row_sum<q15_23>(0), 2 * 16384);
        EXPECT_EQ(row_sum<q15_23>(1), 2 * 16384);
        // Q4.27 runs Q31's rows.
        using d31 = catenae::down2_q31<1>;
        using d27 = catenae::down2_q4_27<1>;
        for (std::size_t t = 0; t < d31::row_length(); ++t) {
            EXPECT_EQ(d31::coefficient(1, t), d27::coefficient(1, t));
        }
    }

    template <typename S, typename Ratio, typename Design>
    void full_scale_dc_is_full_scale() {
        using stage = catenae::basic_resample<S, 1, Ratio, Design>;
        using v     = catenae::storage_t<S>;
        for (const v level : {std::numeric_limits<v>::max(), std::numeric_limits<v>::min()}) {
            const std::vector<v> x(400, level);
            const std::vector<v> y = run_stage<stage>(x, 400);
            for (std::size_t i = y.size() / 2; i < y.size(); ++i) {
                ASSERT_EQ(y[i], level) << Ratio::up << "/" << Ratio::down << " at " << i;
            }
        }
    }

    // Verifies CAT-RESAMPLE-002.
    TEST(resample, FixedPointDcIsExactAtFullScale) {
        full_scale_dc_is_full_scale<catenae::q15, r<2, 1>, catenae::half_band<1>>();
        full_scale_dc_is_full_scale<catenae::q15, r<1, 2>, catenae::half_band<1>>();
        full_scale_dc_is_full_scale<catenae::q15, r<1, 3>, short_design<3>>();
        full_scale_dc_is_full_scale<catenae::q15, r<2, 3>, short_design<3>>();
        full_scale_dc_is_full_scale<catenae::q15, r<3, 8>, short_design<8>>();
        full_scale_dc_is_full_scale<catenae::q31, r<2, 1>, catenae::half_band<2>>();
        full_scale_dc_is_full_scale<catenae::q31, r<1, 2>, catenae::half_band<1>>();
        full_scale_dc_is_full_scale<catenae::q31, r<3, 4>, short_design<4>>();
    }

    // ---- Against double ----------------------------------------------------------------------

    /// upfirdn(h, x, up = L, down = M) in double from the prototype, the rows' scaling applied.
    template <typename Ratio, typename Design>
    std::vector<double> upfirdn(const std::vector<double>& x) {
        constexpr std::size_t l    = Ratio::up;
        constexpr std::size_t m    = Ratio::down;
        const std::size_t     outs = (x.size() * l + m - 1) / m;
        std::vector<double>   y(outs);
        for (std::size_t n = 0; n < outs; ++n) {
            double acc = 0.0;
            for (std::size_t i = 0; i < Design::taps; ++i) {
                // y[n] = sum over k of h[k] u[n M - k], u the input upsampled by L.
                const std::ptrdiff_t up = static_cast<std::ptrdiff_t>(n * m) - static_cast<std::ptrdiff_t>(i);
                if (up >= 0 && up % static_cast<std::ptrdiff_t>(l) == 0
                    && static_cast<std::size_t>(up) / l < x.size()) {
                    acc += Design::prototype[i] * x[static_cast<std::size_t>(up) / l];
                }
            }
            y[n] = acc * static_cast<double>(l) / static_cast<double>(Design::band);
        }
        return y;
    }

    template <typename Ratio, typename Design>
    double largest_departure() {
        using stage = catenae::resample_f64<1, Ratio, Design>;
        catenae_test::signal s(5);
        std::vector<double>  x(300);
        for (double& v : x) {
            v = s.next<double>();
        }
        const std::vector<double> got   = run_stage<stage>(x, 300);
        const std::vector<double> want  = upfirdn<Ratio, Design>(x);
        double                    worst = 0.0;
        for (std::size_t i = 0; i < got.size(); ++i) {
            worst = std::max(worst, std::fabs(got[i] - want[i]));
        }
        return worst;
    }

    // Verifies CAT-RESAMPLE-001.
    TEST(resample, DoubleIsUpfirdn) {
        // The rows' scaling is the formula's (h going up, h / M down, each mixed phase at unity),
        // so double meets scipy's upfirdn within its rounding wherever a phase sums to its gain.
        EXPECT_LT((largest_departure<r<2, 1>, catenae::half_band<1>>()), 1e-14);
        EXPECT_LT((largest_departure<r<1, 2>, catenae::half_band<1>>()), 1e-14);
        EXPECT_LT((largest_departure<r<3, 1>, short_design<3>>()), 1e-14);
        EXPECT_LT((largest_departure<r<1, 3>, short_design<3>>()), 1e-14);
        EXPECT_LT((largest_departure<r<3, 2>, short_design<3>>()), 1e-14);
    }

    /// The largest distance of a format's stage from the double one over the same input, in the
    /// format's full scale.
    template <typename S, typename Ratio, typename Design>
    double floor_of() {
        using v   = catenae::storage_t<S>;
        using fmt = catenae::basic_resample<S, 1, Ratio, Design>;
        using dbl = catenae::resample_f64<1, Ratio, Design>;
        constexpr double scale =
            std::is_floating_point<v>::value ? 1.0 : -static_cast<double>(std::numeric_limits<v>::min());
        catenae_test::signal s(9);
        std::vector<v>       x(600);
        std::vector<double>  xd(600);
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double u = 0.5 * static_cast<double>(s.next<float>());
            x[i]  = std::is_floating_point<v>::value ? static_cast<v>(u) : static_cast<v>(std::lround(u * scale));
            xd[i] = static_cast<double>(x[i]) / scale;
        }
        const std::vector<v>      got   = run_stage<fmt>(x, 600);
        const std::vector<double> want  = run_stage<dbl>(xd, 600);
        double                    worst = 0.0;
        for (std::size_t i = 0; i < got.size(); ++i) {
            worst = std::max(worst, std::fabs(static_cast<double>(got[i]) / scale - want[i]));
        }
        return worst;
    }

    // Verifies CAT-RESAMPLE-001.
    TEST(resample, EachFormatFollowsDoubleWithinItsFloor) {
        // Measured on the hosted legs at contraction off, noise at -6 dBFS, at divisor 1: float
        // 7.5e-8 up and 3.2e-8 down; Q31 2.5 and 1.9 LSB; Q15 2.1 and 1.2 LSB, its taps' Q1.14
        // rounding the format's limit.
        EXPECT_LT((floor_of<float, r<2, 1>, catenae::half_band<1>>()), 1e-7);
        EXPECT_LT((floor_of<float, r<1, 2>, catenae::half_band<1>>()), 5e-8);
        EXPECT_LT((floor_of<catenae::q31, r<2, 1>, catenae::half_band<1>>()) * 2147483648.0, 3.0);
        EXPECT_LT((floor_of<catenae::q31, r<1, 2>, catenae::half_band<1>>()) * 2147483648.0, 3.0);
        EXPECT_LT((floor_of<catenae::q15, r<2, 1>, catenae::half_band<1>>()) * 32768.0, 3.0);
        EXPECT_LT((floor_of<catenae::q15, r<1, 2>, catenae::half_band<1>>()) * 32768.0, 3.0);
    }

    // ---- The contract ------------------------------------------------------------------------

    template <typename S, std::size_t Ch, typename Ratio, typename Design>
    void the_generic_tests_pass() {
        const catenae::basic_resample<S, Ch, Ratio, Design> stage{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage)) << Ratio::up << "/" << Ratio::down;
        EXPECT_TRUE(catenae_test::counts_are_exact(stage)) << Ratio::up << "/" << Ratio::down;
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true)) << Ratio::up << "/" << Ratio::down;
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage)) << Ratio::up << "/" << Ratio::down;
    }

    // Verifies CAT-RESAMPLE-003.
    TYPED_TEST(resample_formats, ItKeepsTheStageContract) {
        the_generic_tests_pass<TypeParam, 2, r<2, 1>, catenae::half_band<1>>();
        the_generic_tests_pass<TypeParam, 2, r<1, 2>, catenae::half_band<2>>();
        the_generic_tests_pass<TypeParam, 1, r<3, 2>, short_design<3>>();
        the_generic_tests_pass<TypeParam, 2, r<2, 3>, short_design<3>>();
        the_generic_tests_pass<TypeParam, 1, r<1, 6>, short_design<6>>();
        using up   = catenae::stage_traits<catenae::basic_up2<TypeParam, 2>>;
        using down = catenae::stage_traits<catenae::basic_down2<TypeParam, 2>>;
        using mix  = catenae::stage_traits<catenae::basic_resample<TypeParam, 2, r<3, 2>, short_design<3>>>;
        static_assert(up::is_frame_synchronous && up::output_quantum == 2
                          && up::exact_latency == (catenae::ratio{19, 1}),
                      "up2: N = 39, (N - 1) / 2 frames at the output");
        static_assert(down::input_group == 2 && down::max_lead == (catenae::ratio{1, 2})
                          && down::exact_latency == (catenae::ratio{19, 2}),
                      "down2: a lead of 1/2, (N - 1) / 4 frames");
        static_assert(mix::input_group == 2 && mix::output_quantum == 1 && mix::exact_latency == (catenae::ratio{5, 2}),
                      "3/2: N = 11, 5/2 frames");
        static_assert(catenae::stage_traits<catenae::basic_up2<TypeParam, 1, 2>>::exact_latency
                          == (catenae::ratio{9, 1}),
                      "relaxed: N = 19");
    }

    // Verifies CAT-RESAMPLE-003.
    TEST(resample, AnImpulsePeaksOnTheLatency) {
        // up2 then down2 at divisor 1: 19/2 + 19/2 = 19 frames, whole, and the round trip's
        // impulse response is symmetric about it.
        const auto trip = catenae::up2_f64<1>{} | catenae::down2_f64<1>{};
        using trip_t    = std::remove_const_t<decltype(trip)>;
        static_assert(trip_t::exact_latency == (catenae::ratio{19, 1}) && trip_t::is_frame_synchronous, "19");
        static catenae::memory_of<trip_t> mem;
        trip.reset(mem.state);
        std::vector<double> x(64, 0.0);
        x[0] = 1.0;
        std::vector<double> y(64);
        static_cast<void>(trip.process(mem, x, y));
        EXPECT_EQ(std::max_element(y.begin(), y.end()) - y.begin(), 19);
        for (std::size_t k = 1; k <= 19; ++k) {
            EXPECT_NEAR(y[19 - k], y[19 + k], 1e-16) << k;
        }
        // up2 alone: the prototype, peaking at its centre.
        std::vector<double> one(30, 0.0);
        one[0]                        = 1.0;
        const std::vector<double> imp = run_stage<catenae::up2_f64<1>>(one, 30);
        EXPECT_EQ(std::max_element(imp.begin(), imp.end()) - imp.begin(), 19);
    }

    template <typename S, std::size_t Ch, typename Ratio, typename Design>
    void silent_after_the_tail() {
        using stage = catenae::basic_resample<S, Ch, Ratio, Design>;
        using v     = catenae::storage_t<S>;
        const stage    s{};
        std::vector<v> x = noise<S>(50 * Ch, 2);
        x.resize((50 + s.tail_frames() + 24) * Ch, v{0});
        const std::vector<v> y    = run_stage<stage>(x, 17);
        const std::size_t    from = stage::outputs_from_rest(50 + s.tail_frames());
        for (std::size_t i = from * Ch; i < y.size(); ++i) {
            ASSERT_EQ(y[i], v{0}) << Ratio::up << "/" << Ratio::down << " output value " << i;
        }
    }

    // Verifies CAT-RESAMPLE-003.
    TYPED_TEST(resample_formats, AfterItsTailTheOutputIsSilence) {
        silent_after_the_tail<TypeParam, 2, r<2, 1>, catenae::half_band<1>>();
        silent_after_the_tail<TypeParam, 2, r<1, 2>, catenae::half_band<1>>();
        silent_after_the_tail<TypeParam, 1, r<2, 3>, short_design<3>>();
        silent_after_the_tail<TypeParam, 1, r<1, 8>, short_design<8>>();
    }

} // namespace
