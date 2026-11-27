/// @file test_meters.cpp
/// @brief The peak and RMS meters' contracts (include/catenae/peak_meter.h, rms_meter.h).
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
#include "catenae/peak_meter.h"
#include "catenae/rms_meter.h"

namespace {

    using analyzer_test::formats;

    template <typename S>
    class meters : public ::testing::Test {};
    TYPED_TEST_SUITE(meters, formats, );

    constexpr std::size_t k_ch     = 2;
    constexpr std::size_t k_hop    = 48;
    constexpr std::size_t k_window = 192; // four hops
    constexpr std::size_t k_frames = 48 * 23 + 17;

    /// |x| as the card says: a fixed-point magnitude saturates.
    template <typename V>
    V magnitude(V x) {
        if constexpr (std::is_floating_point<V>::value) {
            return std::fabs(x);
        }
        else {
            if (x >= 0) {
                return x;
            }
            return x == std::numeric_limits<V>::min() ? std::numeric_limits<V>::max() : static_cast<V>(-x);
        }
    }

    /// Each hop's expected peaks: the largest magnitude of each channel's last k_window frames,
    /// frames before reset counting as zero.
    template <typename S>
    std::vector<std::array<catenae::storage_t<S>, k_ch>> expected_peaks(const std::vector<catenae::storage_t<S>>& in) {
        using value = catenae::storage_t<S>;
        std::vector<std::array<value, k_ch>> out;
        for (std::size_t end = k_hop; end * k_ch <= in.size(); end += k_hop) {
            std::array<value, k_ch> peaks{};
            for (std::size_t f = end > k_window ? end - k_window : 0; f < end; ++f) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    const value m = magnitude(in[f * k_ch + c]);
                    peaks[c]      = m > peaks[c] ? m : peaks[c];
                }
            }
            out.push_back(peaks);
        }
        return out;
    }

    /// Each hop's expected energies, summed as the card says: each hop in frame order from 0,
    /// then the window's hops oldest first from 0, hops before reset being 0.
    template <typename S, std::size_t Window>
    auto expected_energies(const std::vector<catenae::storage_t<S>>& in, int shift) {
        using sum                               = decltype(analyzer_test::product<S>({}, {}, 0));
        constexpr std::size_t              hops = Window / k_hop;
        std::vector<std::array<sum, k_ch>> per_hop;
        for (std::size_t end = k_hop; end * k_ch <= in.size(); end += k_hop) {
            std::array<sum, k_ch> s{};
            for (std::size_t f = end - k_hop; f < end; ++f) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    s[c] += analyzer_test::product<S>(in[f * k_ch + c], in[f * k_ch + c], shift);
                }
            }
            per_hop.push_back(s);
        }
        std::vector<std::array<sum, k_ch>> out;
        for (std::size_t h = 0; h < per_hop.size(); ++h) {
            std::array<sum, k_ch> w{};
            for (std::size_t back = hops; back-- > 0;) {
                if (h >= back) {
                    for (std::size_t c = 0; c < k_ch; ++c) {
                        w[c] += per_hop[h - back][c];
                    }
                }
                else {
                    for (std::size_t c = 0; c < k_ch; ++c) {
                        w[c] += sum{};
                    }
                }
            }
            out.push_back(w);
        }
        return out;
    }

    // Verifies CAT-PEAK-METER-001.
    TYPED_TEST(meters, PeakIsTheWindowsLargestMagnitude) {
        using format  = TypeParam;
        using meter_t = catenae::basic_peak_meter<format, k_ch, k_hop, k_window>;
        static_assert(catenae::analyzer_traits<meter_t>::opted_in && meter_t::rests_at_zero,
                      "an analyzer at rest at zero");
        const auto    in       = analyzer_test::noise<format>(k_frames * k_ch, 3);
        const auto    expected = expected_peaks<format>(in);
        const meter_t meter{};
        for (const std::size_t largest : {std::size_t{1}, std::size_t{7}, k_hop}) {
            const auto got = analyzer_test::results_of(meter, in, largest, 11);
            ASSERT_EQ(got.size(), expected.size());
            for (std::size_t i = 0; i < got.size(); ++i) {
                EXPECT_TRUE(analyzer_test::same_bits(got[i], expected[i]))
                    << "hop " << i << ", calls up to " << largest;
            }
        }
        // One call of every frame: the last result.
        const auto whole = analyzer_test::results_of(meter, in, 0, 0);
        ASSERT_EQ(whole.size(), 1u);
        EXPECT_TRUE(analyzer_test::same_bits(whole[0], expected.back()));
    }

    // Verifies CAT-RMS-METER-001.
    TYPED_TEST(meters, EnergyIsTheWindowsSumOfSquares) {
        using format           = TypeParam;
        using meter_t          = catenae::basic_rms_meter<format, k_ch, k_hop, k_window>;
        const auto    in       = analyzer_test::noise<format>(k_frames * k_ch, 5);
        const auto    expected = expected_energies<format, k_window>(in, meter_t::shift);
        const meter_t meter{};
        for (const std::size_t largest : {std::size_t{1}, std::size_t{13}, k_hop}) {
            const auto got = analyzer_test::results_of(meter, in, largest, 17);
            ASSERT_EQ(got.size(), expected.size());
            for (std::size_t i = 0; i < got.size(); ++i) {
                EXPECT_TRUE(analyzer_test::same_bits(got[i], expected[i]))
                    << "hop " << i << ", calls up to " << largest;
            }
        }
        // A window of one hop: each result is its hop's own.
        using one_t        = catenae::basic_rms_meter<format, k_ch, k_hop>;
        const auto one_hop = expected_energies<format, k_hop>(in, one_t::shift);
        const auto got_one = analyzer_test::results_of(one_t{}, in, 5, 23);
        ASSERT_EQ(got_one.size(), one_hop.size());
        for (std::size_t i = 0; i < got_one.size(); ++i) {
            EXPECT_TRUE(analyzer_test::same_bits(got_one[i], one_hop[i])) << "hop " << i;
        }
    }

    // Verifies CAT-RMS-METER-003, CAT-PEAK-METER-003.
    TYPED_TEST(meters, ReadingsAreDecibelsOfFullScale) {
        using format = TypeParam;
        using value  = catenae::storage_t<format>;
        using rms    = catenae::basic_rms_meter<format, 1, 480, 4800>;
        using pk     = catenae::basic_peak_meter<format, 1, 480>;
        // A full-scale sine, at a whole number of periods in the window: -3.01 dBFS.
        constexpr double full = catenae::format_traits<format>::is_fixed_point
                                    ? double(std::int64_t{1} << catenae::format_traits<format>::fraction_bits)
                                    : 1.0;
        const double     top =
            catenae::format_traits<format>::is_fixed_point && catenae::format_traits<format>::integer_bits == 0
                    ? full - 1.0
                    : full;
        std::vector<value> sine(4800);
        for (std::size_t i = 0; i < sine.size(); ++i) {
            const double v = top * std::sin(6.283185307179586 * double(i) / 48.0);
            if constexpr (std::is_floating_point<value>::value) {
                sine[i] = static_cast<value>(v);
            }
            else {
                sine[i] = static_cast<value>(std::lround(v));
            }
        }
        const auto energy = analyzer_test::results_of(rms{}, sine, 0, 0);
        ASSERT_EQ(energy.size(), 1u);
        EXPECT_NEAR(static_cast<double>(rms::dbfs(energy[0][0])), -3.0103, 0.01);
        // Half of full scale peaks at -6.02 dBFS; Q4.27 reads its headroom above 0 dBFS.
        EXPECT_NEAR(static_cast<double>(pk::dbfs(static_cast<value>(full / 2))), -6.0206, 1e-4);
        if constexpr (catenae::format_traits<format>::is_fixed_point
                      && catenae::format_traits<format>::integer_bits > 0) {
            EXPECT_NEAR(static_cast<double>(pk::dbfs(static_cast<value>(full * 4))), 12.0412, 1e-4);
        }
        // Silence reads minus infinity.
        EXPECT_TRUE(std::isinf(rms::dbfs(0)) && rms::dbfs(0) < 0);
        EXPECT_TRUE(std::isinf(pk::dbfs(value{})) && pk::dbfs(value{}) < 0);
    }

    // Verifies CAT-PEAK-METER-002, CAT-RMS-METER-002.
    TEST(meters, FixedPointExtremesDoNotOverflow) {
        // The most negative Q15 reads as the most positive.
        using pk15 = catenae::peak_meter_q15<1, 4>;
        const std::vector<std::int16_t> low(4, std::numeric_limits<std::int16_t>::min());
        const auto                      peak = analyzer_test::results_of(pk15{}, low, 0, 0);
        ASSERT_EQ(peak.size(), 1u);
        EXPECT_EQ(peak[0][0], std::numeric_limits<std::int16_t>::max());
        // A window of the most negative Q31 sums without overflow, and reads 0 dBFS.
        using rms31 = catenae::rms_meter_q31<1, 4096, 8192>;
        static_assert(rms31::shift == 13, "floor(log2 8192)");
        const std::vector<std::int32_t> most(8192, std::numeric_limits<std::int32_t>::min());
        const auto                      e = analyzer_test::results_of(rms31{}, most, 0, 0);
        ASSERT_EQ(e.size(), 1u);
        EXPECT_EQ(e[0][0], std::int64_t{8192} << (62 - 13));
        EXPECT_NEAR(static_cast<double>(rms31::dbfs(e[0][0])), 0.0, 1e-5);
        // At the largest window whose sum the shift admits: 2^(s+1) - 1 frames of 2^62.
        using rms_edge = catenae::rms_meter_q31<1, 1, 8191>;
        static_assert(rms_edge::shift == 12, "floor(log2 8191)");
        const std::vector<std::int32_t> edge(8191, std::numeric_limits<std::int32_t>::min());
        const auto                      f = analyzer_test::results_of(rms_edge{}, edge, 0, 0);
        ASSERT_EQ(f.size(), 1u);
        EXPECT_EQ(f[0][0], std::int64_t{8191} << (62 - 12));
        EXPECT_GT(f[0][0], 0);
    }

    // Verifies CAT-PEAK-METER-002.
    TEST(meters, NaNIsThePeakUntilTheWindowPassesIt) {
        using pk = catenae::peak_meter<1, 4, 8>;
        std::vector<float> in(24, 0.25f);
        in[5]          = std::numeric_limits<float>::quiet_NaN();
        const auto got = analyzer_test::results_of(pk{}, in, 4, 1);
        ASSERT_EQ(got.size(), 6u);
        EXPECT_FALSE(std::isnan(got[0][0])); // frames 0 to 3
        EXPECT_TRUE(std::isnan(got[1][0]));  // the hop with it
        EXPECT_TRUE(std::isnan(got[2][0]));  // still in the window of 8
        EXPECT_FALSE(std::isnan(got[3][0])); // passed
        EXPECT_EQ(got[3][0], 0.25f);
    }

} // namespace
