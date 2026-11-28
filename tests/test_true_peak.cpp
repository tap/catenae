/// @file test_true_peak.cpp
/// @brief The true-peak meter's contract (include/catenae/true_peak_meter.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "analyzer_support.h"
#include "catenae/tap.h"
#include "catenae/true_peak_meter.h"
#include "catenae_test/harness.h"

namespace {

    using analyzer_test::formats;

    template <typename S>
    class true_peak : public ::testing::Test {};
    TYPED_TEST_SUITE(true_peak, formats, );

    constexpr std::size_t k_ch     = 2;
    constexpr std::size_t k_hop    = 40;
    constexpr std::size_t k_window = 120; // three hops

    /// BS.1770-4 Annex 2's table as printed, phase by phase.
    constexpr double k_annex[4][12] = {
        {0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000, -0.0594482421875, 0.1373291015625,
         0.9721679687500, -0.1022949218750, 0.0476074218750, -0.0266113281250, 0.0148925781250, -0.0083007812500},
        {-0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250, -0.1665039062500, 0.4650878906250,
         0.7797851562500, -0.2003173828125, 0.1015625000000, -0.0582275390625, 0.0330810546875, -0.0189208984375},
        {-0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000, -0.2003173828125, 0.7797851562500,
         0.4650878906250, -0.1665039062500, 0.0891113281250, -0.0517578125000, 0.0292968750000, -0.0291748046875},
        {-0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750, -0.1022949218750, 0.9721679687500,
         0.1373291015625, -0.0594482421875, 0.0332031250000, -0.0196533203125, 0.0109863281250, 0.0017089843750},
    };

    /// The sum a point is held in, as the test writes it: the sample's type when it floats, 64
    /// bits for a fixed-point format.
    template <typename S>
    using point_of = std::conditional_t<catenae::format_traits<S>::is_fixed_point, std::int64_t, catenae::storage_t<S>>;

    /// A coefficient in the test's sum: the printed number in S, or times 2^13 as an integer.
    template <typename S>
    point_of<S> coefficient(std::size_t k, std::size_t j) {
        if constexpr (catenae::format_traits<S>::is_fixed_point) {
            return std::llround(k_annex[k][j] * 8192.0); // long long: 64 bits wherever it is int64_t or not
        }
        else {
            return static_cast<catenae::storage_t<S>>(k_annex[k][j]);
        }
    }

    template <typename V>
    V magnitude(V x) {
        if constexpr (std::is_floating_point<V>::value) {
            return std::fabs(x);
        }
        else {
            return x < 0 ? -x : x;
        }
    }

    /// Frame f's largest point on channel c: the four rows of the table each dotted with the last
    /// 12 frames oldest first (frames before reset 0), the largest magnitude.
    template <typename S>
    point_of<S> largest_point(const std::vector<catenae::storage_t<S>>& in, std::size_t f, std::size_t c) {
        using point = point_of<S>;
        point largest{};
        for (std::size_t k = 0; k < 4; ++k) {
            point p{};
            for (std::size_t m = 0; m < 12; ++m) {
                const auto                  at = static_cast<std::ptrdiff_t>(f) - static_cast<std::ptrdiff_t>(11 - m);
                const catenae::storage_t<S> x  = at < 0 ? catenae::storage_t<S>{} : in[std::size_t(at) * k_ch + c];
                p += coefficient<S>(k, m) * static_cast<point>(x);
            }
            largest = magnitude(p) > largest ? magnitude(p) : largest; // the noise holds no NaN
        }
        return largest;
    }

    /// Each hop's expected peaks: each frame's largest point, over the window.
    template <typename S>
    std::vector<std::array<point_of<S>, k_ch>> expected_peaks(const std::vector<catenae::storage_t<S>>& in) {
        using point               = point_of<S>;
        const std::size_t  frames = in.size() / k_ch;
        std::vector<point> per_frame(frames * k_ch);
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t c = 0; c < k_ch; ++c) {
                per_frame[f * k_ch + c] = largest_point<S>(in, f, c);
            }
        }
        std::vector<std::array<point, k_ch>> out;
        for (std::size_t end = k_hop; end <= frames; end += k_hop) {
            std::array<point, k_ch> peaks{};
            for (std::size_t f = end > k_window ? end - k_window : 0; f < end; ++f) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    peaks[c] = per_frame[f * k_ch + c] > peaks[c] ? per_frame[f * k_ch + c] : peaks[c];
                }
            }
            out.push_back(peaks);
        }
        return out;
    }

    // Verifies CAT-TRUE-PEAK-001.
    TYPED_TEST(true_peak, PeakIsTheWindowsLargestInterpolatedMagnitude) {
        using format  = TypeParam;
        using meter_t = catenae::basic_true_peak_meter<format, k_ch, k_hop, k_window>;
        static_assert(catenae::analyzer_traits<meter_t>::opted_in && meter_t::rests_at_zero,
                      "an analyzer at rest at zero");
        const auto    in       = analyzer_test::noise<format>((k_hop * 17 + 9) * k_ch, 7);
        const auto    expected = expected_peaks<format>(in);
        const meter_t meter{};
        for (const std::size_t largest : {std::size_t{1}, std::size_t{11}, k_hop}) {
            const auto got = analyzer_test::results_of(meter, in, largest, 13);
            ASSERT_EQ(got.size(), expected.size());
            for (std::size_t i = 0; i < got.size(); ++i) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    EXPECT_EQ(static_cast<point_of<format>>(got[i][c]), expected[i][c])
                        << "hop " << i << ", channel " << c << ", calls up to " << largest;
                }
            }
        }
        // As a tap's analyzer: the audio passes through, bit for bit, in any chunking.
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(catenae::tap<k_ch, meter_t>{}));
    }

    /// The largest reading of a one-channel signal, in dB TP, in S, `samples` of it.
    template <typename S>
    double reading_of(const std::vector<double>& samples) {
        using meter_t = catenae::basic_true_peak_meter<S, 1, 64>;
        using value   = catenae::storage_t<S>;
        const meter_t               meter{};
        typename meter_t::state     st{};
        typename meter_t::peak_type top{};
        double                      full = 1.0;
        if constexpr (catenae::format_traits<S>::is_fixed_point) {
            full = std::ldexp(1.0, catenae::format_traits<S>::fraction_bits);
        }
        for (std::size_t i = 0; i < samples.size(); ++i) {
            value x{};
            if constexpr (std::is_floating_point<value>::value) {
                x = static_cast<value>(samples[i]);
            }
            else {
                const double v = std::nearbyint(samples[i] * full);
                x              = static_cast<value>(
                    v >= double(std::numeric_limits<value>::max()) ? std::numeric_limits<value>::max() : v);
            }
            meter.analyze(st, catenae::span<const value>(&x, 1));
            top = meter.result(st)[0] > top ? meter.result(st)[0] : top;
        }
        // The last frames' points, past the last hop.
        const std::vector<value> tail(64, value{});
        meter.analyze(st, catenae::span<const value>(tail.data(), tail.size()));
        top = meter.result(st)[0] > top ? meter.result(st)[0] : top;
        return static_cast<double>(meter_t::dbtp(top));
    }

    /// Tech 3341 signals 15 to 19: a sine at `cycles` of the rate, `amplitude` of full scale,
    /// `degrees` of phase, 100 ms with 10 ms linear fades, at 48 kHz.
    std::vector<double> tone(double cycles, double amplitude, double degrees) {
        constexpr std::size_t n    = 4800;
        constexpr std::size_t fade = 480;
        std::vector<double>   x(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double ramp = i < fade ? double(i) / fade : (i >= n - fade ? double(n - 1 - i) / fade : 1.0);
            x[i] = ramp * amplitude * std::sin(6.283185307179586 * cycles * double(i) + degrees * 0.017453292519943295);
        }
        return x;
    }

    /// I0, the Kaiser window's Bessel function, by its series.
    double bessel_i0(double x) {
        double sum  = 1.0;
        double term = 1.0;
        for (int k = 1; k < 64; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    /// Tech 3341 signals 20 to 23: at 4 x the rate, a sine of a sixth of the rate at 0.5 with a
    /// single period of a quarter of it at 1.0, from a zero crossing, continuous in phase; low-pass
    /// filtered (401 taps, a Kaiser window of beta 10, cut at 0.95 of the rate's Nyquist) and taken
    /// every fourth sample from `offset`. 600 frames around the period.
    std::vector<double> period_in_tone(std::size_t offset) {
        constexpr std::size_t frames = 600;
        constexpr std::size_t taps   = 401;
        constexpr std::size_t half   = taps / 2;
        constexpr std::size_t n4     = 4 * frames + 2 * half + 8;
        constexpr std::size_t start  = n4 / 2 / 24 * 24; // a zero crossing of the sixth
        std::vector<double>   x4(n4);
        for (std::size_t t = 0; t < n4; ++t) {
            x4[t] = t >= start && t < start + 16 ? std::sin(6.283185307179586 * double(t - start) / 16.0)
                                                 : 0.5 * std::sin(6.283185307179586 * double(t) / 24.0);
        }
        std::vector<double> h(taps);
        double              sum = 0.0;
        for (std::size_t i = 0; i < taps; ++i) {
            const double m      = double(i) - double(half);
            const double fc     = 0.125 * 0.95;
            const double sinc   = i == half ? 2.0 * fc : std::sin(6.283185307179586 * fc * m) / (3.141592653589793 * m);
            const double r      = m / double(half);
            const double kaiser = bessel_i0(10.0 * std::sqrt(1.0 - r * r)) / bessel_i0(10.0);
            h[i]                = sinc * kaiser;
            sum += h[i];
        }
        std::vector<double> y(frames);
        for (std::size_t f = 0; f < frames; ++f) {
            const std::size_t centre = half + offset + 4 * f;
            double            acc    = 0.0;
            for (std::size_t i = 0; i < taps; ++i) {
                acc += h[i] / sum * x4[centre + half - i];
            }
            y[f] = acc;
        }
        return y;
    }

    // Verifies CAT-TRUE-PEAK-002: the card's numbers, each within the Tech's +0.2/-0.4 dB.
    TYPED_TEST(true_peak, Tech3341SignalsReadTheCardsNumbers) {
        using format = TypeParam;
        if constexpr (!catenae::format_traits<format>::is_fixed_point) {
            const double read[] = {reading_of<format>(tone(0.25, 0.5, 0.0)), reading_of<format>(tone(0.25, 0.5, 45.0)),
                                   reading_of<format>(tone(1.0 / 6.0, 0.5, 60.0)),
                                   reading_of<format>(tone(0.125, 0.5, 67.5)),
                                   reading_of<format>(tone(0.25, 1.41, 45.0))};
            const double card[] = {-6.217, -5.975, -6.314, -6.028, 3.030};
            const double want[] = {-6.0, -6.0, -6.0, -6.0, 3.0};
            // The samples' own peaks, which a sample-peak meter reads: 15's is its true peak, 16's
            // is 3 dB below it.
            const auto sample_peak_db = [](const std::vector<double>& x) {
                double top = 0.0;
                for (const double v : x) {
                    top = std::fabs(v) > top ? std::fabs(v) : top;
                }
                return 20.0 * std::log10(top);
            };
            EXPECT_NEAR(sample_peak_db(tone(0.25, 0.5, 0.0)), -6.02, 0.01);
            EXPECT_NEAR(sample_peak_db(tone(0.25, 0.5, 45.0)), -9.03, 0.01);
            for (std::size_t i = 0; i < 5; ++i) {
                EXPECT_NEAR(read[i], card[i], 0.01) << "signal " << 15 + i;
                EXPECT_LE(read[i], want[i] + 0.2) << "signal " << 15 + i;
                EXPECT_GE(read[i], want[i] - 0.4) << "signal " << 15 + i;
            }
            const double period[] = {-0.031, 0.028, -0.136, -0.179};
            for (std::size_t offset = 0; offset < 4; ++offset) {
                const double r = reading_of<format>(period_in_tone(offset));
                EXPECT_NEAR(r, period[offset], 0.01) << "signal " << 20 + offset;
                EXPECT_LE(r, 0.2) << "signal " << 20 + offset;
                EXPECT_GE(r, -0.4) << "signal " << 20 + offset;
            }
        }
        else {
            // A fixed-point format reads what float reads of the same values, within its
            // quantization (Q4.27 holds 1.41 of full scale too).
            for (const double amplitude : {0.5, 0.99}) {
                const auto          x     = tone(0.25, amplitude, 45.0);
                const double        fixed = reading_of<format>(x);
                std::vector<double> quantized(x.size());
                const double        full = std::ldexp(1.0, catenae::format_traits<format>::fraction_bits);
                for (std::size_t i = 0; i < x.size(); ++i) {
                    quantized[i] = std::nearbyint(x[i] * full) / full;
                }
                EXPECT_NEAR(fixed, reading_of<double>(quantized), 1e-6) << amplitude;
            }
        }
    }

    template <typename S>
    class true_peak_fixed : public ::testing::Test {};
    using fixed_formats = ::testing::Types<catenae::q15, catenae::q31, catenae::q4_27>;
    TYPED_TEST_SUITE(true_peak_fixed, fixed_formats, );

    // Verifies CAT-TRUE-PEAK-002: the largest sums are exact and cannot overflow.
    TYPED_TEST(true_peak_fixed, ExtremesAreExact) {
        using format = TypeParam;
        using value  = catenae::storage_t<format>;
        {
            using meter_t = catenae::basic_true_peak_meter<format, 1, 12>;
            // Each phase's largest sum: the extremes of the format in the signs of its row.
            for (std::size_t k = 0; k < 4; ++k) {
                std::vector<value> in(12);
                std::int64_t       want = 0;
                for (std::size_t m = 0; m < 12; ++m) {
                    const std::int64_t tap = coefficient<format>(k, m);
                    in[m] = tap > 0 ? std::numeric_limits<value>::max() : std::numeric_limits<value>::min();
                    want += tap * static_cast<std::int64_t>(in[m]);
                }
                const auto got = analyzer_test::results_of(meter_t{}, in, 0, 0);
                ASSERT_EQ(got.size(), 1u);
                EXPECT_GE(static_cast<std::int64_t>(got[0][0]), want) << "row " << k;
            }
            // The largest of all: row 1's (or 2's), 2.023 x full scale, +6.12 dB TP from full scale.
            std::vector<value> in(12);
            std::int64_t       want = 0;
            for (std::size_t m = 0; m < 12; ++m) {
                const std::int64_t tap = coefficient<format>(1, m);
                in[m] = tap > 0 ? std::numeric_limits<value>::max() : std::numeric_limits<value>::min();
                want += tap * static_cast<std::int64_t>(in[m]);
            }
            const auto got = analyzer_test::results_of(meter_t{}, in, 0, 0);
            EXPECT_EQ(static_cast<std::int64_t>(got[0][0]), want);
            const double over = catenae::format_traits<format>::integer_bits > 0
                                    ? 20.0 * std::log10(std::ldexp(1.0, catenae::format_traits<format>::integer_bits))
                                    : 0.0;
            EXPECT_NEAR(static_cast<double>(meter_t::dbtp(got[0][0])), 6.12 + over, 0.01);
        }
    }

    // Verifies CAT-TRUE-PEAK-003.
    TYPED_TEST(true_peak, DbtpIsDecibelsOfFullScale) {
        using format  = TypeParam;
        using meter_t = catenae::basic_true_peak_meter<format, 1, 8>;
        using peak    = typename meter_t::peak_type;
        if constexpr (catenae::format_traits<format>::is_fixed_point) {
            const peak full =
                static_cast<peak>(std::int64_t{1} << (catenae::format_traits<format>::fraction_bits + 13));
            EXPECT_NEAR(static_cast<double>(meter_t::dbtp(full)), 0.0, 1e-5);
            EXPECT_NEAR(static_cast<double>(meter_t::dbtp(static_cast<peak>(full / 2))), -6.0206, 1e-4);
        }
        else {
            EXPECT_NEAR(static_cast<double>(meter_t::dbtp(peak{1})), 0.0, 1e-6);
            EXPECT_NEAR(static_cast<double>(meter_t::dbtp(peak{0.5})), -6.0206, 1e-4);
        }
        EXPECT_TRUE(std::isinf(meter_t::dbtp(peak{})) && meter_t::dbtp(peak{}) < 0);
    }

    // Verifies CAT-TRUE-PEAK-002: a NaN is the channel's result while it is in the window.
    TEST(true_peak, NaNIsThePeakWhileInTheWindow) {
        using meter_t = catenae::true_peak_meter<1, 8, 16>;
        std::vector<float> in(64, 0.25f);
        in[10]         = std::numeric_limits<float>::quiet_NaN();
        const auto got = analyzer_test::results_of(meter_t{}, in, 8, 1);
        ASSERT_EQ(got.size(), 8u);
        EXPECT_FALSE(std::isnan(got[0][0])); // frames 0 to 7
        EXPECT_TRUE(std::isnan(got[1][0]));  // frames 8 to 15 hold it and its next 5
        EXPECT_TRUE(std::isnan(got[2][0]));  // frames 16 to 23 hold its last 6 points
        EXPECT_TRUE(std::isnan(got[3][0]));  // the window of 16 still holds frames 16 to 23
        EXPECT_FALSE(std::isnan(got[4][0])); // passed
    }

} // namespace
