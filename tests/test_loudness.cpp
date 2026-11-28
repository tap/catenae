/// @file test_loudness.cpp
/// @brief The loudness meter's contract and its K-weighting's (include/catenae/loudness_meter.h,
/// loudness_design.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "analyzer_support.h"
#include "catenae/biquad.h"
#include "catenae/check.h"
#include "catenae/loudness_design.h"
#include "catenae/loudness_meter.h"
#include "catenae/tap.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"
#include "loudness.h"

namespace {

    template <typename T>
    class loudness : public ::testing::Test {};
    using floating = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(loudness, floating, );

    constexpr double k_pi = 3.141592653589793;

    /// BS.1770-4 Tables 1 and 2, as printed.
    constexpr double k_tables[2][5] = {
        {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585},
        {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621},
    };

    /// The cascade's response at `hz`, rows in double.
    template <typename Rows>
    std::complex<double> response(const Rows& rows, double hz, double rate) {
        const std::complex<double> z = std::polar(1.0, -2.0 * k_pi * hz / rate);
        std::complex<double>       r = 1.0;
        for (const auto& c : rows) {
            r *= (double(c[0]) + double(c[1]) * z + double(c[2]) * z * z)
                 / (1.0 + double(c[3]) * z + double(c[4]) * z * z);
        }
        return r;
    }

    // Verifies CAT-K-WEIGHTING-001.
    TYPED_TEST(loudness, KWeightingIsTheTablesCarriedToTheRate) {
        using sample    = TypeParam;
        using k         = catenae::k_weighting_design<sample>;
        const auto at48 = k::make(48000.0);
        ASSERT_TRUE(at48.has_value());
        for (std::size_t s = 0; s < 2; ++s) {
            for (std::size_t j = 0; j < 5; ++j) {
                EXPECT_EQ(at48->table()[s][j], static_cast<sample>(k_tables[s][j])) << s << ", " << j;
            }
        }
        // Each section's f0, read back from its coefficients at any rate, is the shelf's 1681.974
        // Hz and the high-pass's 38.13547 Hz: the transform pre-warped at f0.
        struct row_at {
            double rate;
            double in_double; // the card's
            double in_float;
        };
        for (const row_at r :
             {row_at{8000.0, 0.287, 0.287}, row_at{16000.0, 0.066, 0.066}, row_at{22050.0, 0.031, 0.031},
              row_at{32000.0, 0.0104, 0.0104}, row_at{44100.0, 0.0016, 0.0049}, row_at{96000.0, 0.0063, 0.028},
              row_at{192000.0, 0.0078, 0.015}}) {
            const auto d = k::make(r.rate);
            ASSERT_TRUE(d.has_value()) << r.rate;
            if constexpr (std::is_same<sample, double>::value) {
                const double f0[] = {1681.9744509555323, 38.13547087611305};
                for (std::size_t s = 0; s < 2; ++s) {
                    const auto&  c  = d->table()[s];
                    const double kk = std::sqrt((1.0 + c[3] + c[4]) / (1.0 - c[3] + c[4]));
                    EXPECT_NEAR(r.rate * std::atan(kk) / k_pi, f0[s], 1e-6) << r.rate << ", section " << s;
                }
            }
            // Against the 48 kHz response from 20 Hz to 20 kHz, or 0.45 of the rate: the card's.
            double worst = 0.0;
            for (int i = 0; i <= 2000; ++i) {
                const double hz = 20.0 * std::pow(1000.0, double(i) / 2000.0);
                if (hz > 0.45 * r.rate) {
                    break;
                }
                const double dev = 20.0
                                   * std::log10(std::abs(response(d->table(), hz, r.rate))
                                                / std::abs(response(k_tables, hz, 48000.0)));
                worst = std::fabs(dev) > std::fabs(worst) ? dev : worst;
            }
            const double bound = std::is_same<sample, float>::value ? r.in_float : r.in_double;
            EXPECT_LE(std::fabs(worst), bound) << r.rate;
            EXPECT_GE(std::fabs(worst), 0.5 * r.in_double) << r.rate;
        }
        // BS.1770-4's reference: 997 Hz through the 48 kHz filter is +0.691 dB, which -0.691 cancels;
        // its group delay there, the design's nominal latency, is -1.31 frames.
        EXPECT_NEAR(20.0 * std::log10(std::abs(response(at48->table(), 997.0, 48000.0))), 0.691, 0.0005);
        EXPECT_NEAR(at48->latency().nominal, -1.307f, 0.001f);
    }

    // Verifies CAT-K-WEIGHTING-001: the example's ROM table is make()'s, its rows bit for bit, its
    // latency within a float ulp and its tails within a frame where another libm rounds otherwise.
    TEST(k_weighting, ExampleTableIsTheStandards) {
        const auto made = catenae::k_weighting_design<float>::make(48000.0);
        ASSERT_TRUE(made.has_value());
        const auto& rom = example::k_weighting_48k;
        EXPECT_TRUE(analyzer_test::same_bits(rom.table(), made->table()));
        EXPECT_NEAR(rom.latency().nominal, made->latency().nominal, 2e-7f);
        EXPECT_NEAR(rom.latency().deviation, made->latency().deviation, 3e-7f);
        EXPECT_LE(rom.tail_frames(), made->tail_frames() + 1);
        EXPECT_GE(rom.tail_frames() + 1, made->tail_frames());
        EXPECT_LE(rom.practical_tail_frames(), made->practical_tail_frames() + 1);
        EXPECT_GE(rom.practical_tail_frames() + 1, made->practical_tail_frames());
        EXPECT_EQ(rom.flush_residue(), made->flush_residue());
    }

    // Verifies CAT-K-WEIGHTING-002.
    TYPED_TEST(loudness, KWeightingRejectsWhatItsCardNames) {
        using sample = TypeParam;
        using k      = catenae::k_weighting_design<sample>;
        for (const double rate :
             {0.0, -48000.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            const auto d = k::make(rate);
            ASSERT_FALSE(d.has_value()) << rate;
            EXPECT_EQ(d.error(), catenae::status::invalid_argument) << rate;
        }
        // The shelf's f0 within min_frequency of Nyquist, and the high-pass's below it.
        const double low  = 1681.9744509555323 / (0.5 - catenae::min_frequency<sample>);
        const double high = 38.13547087611305 / catenae::min_frequency<sample>;
        EXPECT_EQ(k::make(std::floor(low)).error(), catenae::status::out_of_range);
        EXPECT_TRUE(k::make(std::ceil(low)).has_value());
        EXPECT_TRUE(k::make(std::floor(high)).has_value());
        EXPECT_EQ(k::make(std::ceil(high)).error(), catenae::status::out_of_range);
        if constexpr (std::is_same<sample, float>::value) {
            EXPECT_DOUBLE_EQ(std::floor(high), 381354.0);
            EXPECT_DOUBLE_EQ(std::ceil(low), 3365.0);
        }
    }

    /// A sine of `hz` in one period's worth of frames that repeat at `rate`: computed once in
    /// double, so a stream of minutes takes no sine per frame (and none in software double).
    std::vector<double> cycle_of(double hz, double rate) {
        std::size_t n = 1;
        while (std::fabs(double(n) * hz / rate - std::round(double(n) * hz / rate)) > 1e-9) {
            ++n;
        }
        std::vector<double> v(n);
        for (std::size_t i = 0; i < n; ++i) {
            v[i] = std::sin(2.0 * k_pi * hz * double(i) / rate);
        }
        return v;
    }

    /// One piece of a test signal: `seconds` of the 1 kHz tone, each channel at its dBFS peak
    /// (minus infinity for silence).
    template <std::size_t Ch>
    struct piece {
        double                 seconds;
        std::array<double, Ch> dbfs;
    };

    template <std::size_t Ch>
    piece<Ch> all_at(double seconds, double dbfs) {
        piece<Ch> p{seconds, {}};
        for (double& d : p.dbfs) {
            d = dbfs;
        }
        return p;
    }

    constexpr double k_silence = -std::numeric_limits<double>::infinity();

    /// `k` frames from `x` into the meter, `frame` of them before: each hop's end is a call's end,
    /// so `at(frame, result)` sees every result.
    template <typename M, typename At>
    void analyze_by_hops(const M& meter, typename M::state& st, const typename M::value_type* x, std::size_t k,
                         std::size_t frame, std::uint32_t& seen, At& at) {
        constexpr std::size_t ch   = M::input_port::channels;
        std::size_t           done = 0;
        while (done < k) {
            const std::size_t to_hop = M::hop_frames - (frame + done) % M::hop_frames;
            const std::size_t m      = k - done < to_hop ? k - done : to_hop;
            meter.analyze(st, catenae::span<const typename M::value_type>(x + done * ch, m * ch));
            done += m;
            if (meter.results_ready(st) != seen) {
                seen = meter.results_ready(st);
                at(frame + done, meter.result(st));
            }
        }
    }

    /// Streams `pieces` through `meter` from `st` in calls of 241 frames, calling `at(frame,
    /// result)` at each hop's end. Nothing is stored but a call's frames.
    template <typename M, typename At>
    void stream(const M& meter, typename M::state& st, double rate,
                const std::vector<piece<M::input_port::channels>>& pieces, At at) {
        using value              = typename M::value_type;
        constexpr std::size_t ch = M::input_port::channels;
        // The tone in the meter's own type, so a float stream takes no double per frame.
        const std::vector<double> period = cycle_of(1000.0, rate);
        const std::vector<value>  cycle(period.begin(), period.end());
        std::vector<value>        buf(241 * ch);
        std::size_t               frame = 0;
        std::uint32_t             seen  = meter.results_ready(st);
        for (const auto& p : pieces) {
            const auto            n = static_cast<std::size_t>(std::llround(p.seconds * rate));
            std::array<value, ch> gain{};
            for (std::size_t c = 0; c < ch; ++c) {
                gain[c] = std::isinf(p.dbfs[c]) ? value(0) : static_cast<value>(std::pow(10.0, p.dbfs[c] / 20.0));
            }
            std::size_t done = 0;
            while (done < n) {
                const std::size_t k = n - done < 241 ? n - done : 241;
                for (std::size_t f = 0; f < k; ++f) {
                    const value s = cycle[(frame + f) % cycle.size()];
                    for (std::size_t c = 0; c < ch; ++c) {
                        buf[f * ch + c] = gain[c] * s;
                    }
                }
                analyze_by_hops(meter, st, buf.data(), k, frame, seen, at);
                frame += k;
                done += k;
            }
        }
    }

    /// The meter's state, at rest.
    template <typename M>
    std::unique_ptr<typename M::state> rested() {
        return std::make_unique<typename M::state>(typename M::state{});
    }

    template <typename R>
    double lufs_of(R power) {
        return static_cast<double>(catenae::basic_loudness_meter<R, 1, 1, 1>::lufs(power));
    }

    // Verifies CAT-LOUDNESS-001.
    TYPED_TEST(loudness, PowersAreTheKWeightedSums) {
        using sample               = TypeParam;
        constexpr std::size_t ch   = 3;
        constexpr std::size_t hop  = 40;
        constexpr std::size_t step = 2; // hops
        using meter_t              = catenae::basic_loudness_meter<sample, ch, hop, step>;
        const auto design          = catenae::k_weighting_design<sample>::make(48000.0);
        ASSERT_TRUE(design.has_value());
        const typename meter_t::weights_type weights = {sample(1), sample(0.5), sample(1.41)};
        const meter_t                        meter   = meter_t::from_table(*design, weights);
        // The reference: the biquad stage over the input, then the card's sums written out.
        const auto                                           in = analyzer_test::noise<sample>(ch * hop * 75, 29);
        std::vector<sample>                                  y(in.size());
        catenae::basic_biquad<sample, ch, 2>                 filter{*design};
        typename catenae::basic_biquad<sample, ch, 2>::state fs{};
        catenae::no_workspace                                ws{};
        filter.run(fs, ws, catenae::span<const sample>(in), catenae::span<sample>(y));
        std::vector<sample> per_hop;
        for (std::size_t h = 0; h * hop < in.size() / ch; ++h) {
            std::array<sample, ch> e{};
            for (std::size_t f = h * hop; f < (h + 1) * hop; ++f) {
                for (std::size_t c = 0; c < ch; ++c) {
                    e[c] += y[f * ch + c] * y[f * ch + c];
                }
            }
            sample q = 0;
            for (std::size_t c = 0; c < ch; ++c) {
                q += weights[c] * e[c];
            }
            per_hop.push_back(q);
        }
        const auto window = [&](std::size_t h, std::size_t hops) {
            sample s = 0;
            for (std::size_t back = hops; back-- > 0;) {
                s += h >= back ? per_hop[h - back] : sample(0);
            }
            return s / static_cast<sample>(hop * hops);
        };
        for (const std::size_t largest : {std::size_t{1}, std::size_t{17}, hop}) {
            const auto got = analyzer_test::results_of(meter, in, largest, 19);
            ASSERT_EQ(got.size(), per_hop.size());
            for (std::size_t h = 0; h < got.size(); ++h) {
                EXPECT_TRUE(analyzer_test::same_bits(got[h].momentary, window(h, 4 * step))) << h << ", " << largest;
                EXPECT_TRUE(analyzer_test::same_bits(got[h].short_term, window(h, 30 * step))) << h << ", " << largest;
            }
        }
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(catenae::tap<ch, meter_t>{meter}));
    }

    /// The largest momentary, short-term and the last integrated readings of a stream, in LUFS.
    struct readings {
        double momentary;
        double short_term;
        double integrated;
    };

    template <typename M>
    readings read(const M& meter, double rate, const std::vector<piece<M::input_port::channels>>& pieces) {
        auto     st = rested<M>();
        readings r{-1e9, -1e9, -1e9};
        stream(meter, *st, rate, pieces, [&](std::size_t, const typename M::result_type& p) {
            r.momentary  = std::fmax(r.momentary, lufs_of(p.momentary));
            r.short_term = std::fmax(r.short_term, lufs_of(p.short_term));
            r.integrated = lufs_of(p.integrated);
        });
        return r;
    }

    // Verifies CAT-LOUDNESS-002: Tech 3341's Table 1, tests 1 to 6.
    TYPED_TEST(loudness, Tech3341SteadyAndGatedTones) {
        using sample = TypeParam;
        using stereo = catenae::basic_loudness_meter<sample, 2, 960, 5>;
        const auto m = stereo::make(48000.0);
        ASSERT_TRUE(m.has_value());
        // 1 and 2: M, S and I of a steady tone; the integrated reads its bin's centre.
        for (const double level : {-23.0, -33.0}) {
            auto     st = rested<stereo>();
            readings last{};
            stream(*m, *st, 48000.0, {all_at<2>(20.0, level)}, [&](std::size_t, const typename stereo::result_type& p) {
                last = {lufs_of(p.momentary), lufs_of(p.short_term), lufs_of(p.integrated)};
            });
            EXPECT_NEAR(last.momentary, level, 0.1) << level;
            EXPECT_NEAR(last.short_term, level, 0.1) << level;
            EXPECT_NEAR(last.integrated, level, 0.002) << level; // the bin's centre
        }
        // 3 to 5: the relative gate.
        EXPECT_NEAR(read(*m, 48000.0, {all_at<2>(10, -36), all_at<2>(60, -23), all_at<2>(10, -36)}).integrated, -23.0,
                    0.1);
        EXPECT_NEAR(
            read(*m, 48000.0,
                 {all_at<2>(10, -72), all_at<2>(10, -36), all_at<2>(60, -23), all_at<2>(10, -36), all_at<2>(10, -72)})
                .integrated,
            -23.0, 0.1);
        EXPECT_NEAR(read(*m, 48000.0, {all_at<2>(20, -26), all_at<2>(20.1, -20), all_at<2>(20, -26)}).integrated, -23.0,
                    0.1);
        // 6: five channels, the surrounds weighed 1.41 (BS.1770-4 Table 3).
        using five        = catenae::basic_loudness_meter<sample, 5, 960, 5>;
        const auto five_m = five::make(48000.0, {sample(1), sample(1), sample(1), sample(1.41), sample(1.41)});
        ASSERT_TRUE(five_m.has_value());
        EXPECT_NEAR(read(*five_m, 48000.0, {piece<5>{20.0, {-28, -28, -24, -30, -30}}}).integrated, -23.0, 0.1);
    }

    // Verifies CAT-LOUDNESS-002: tests 9 and 12, the windows' readings constant once full.
    TYPED_TEST(loudness, Tech3341WindowsHoldTheirMean) {
        using sample = TypeParam;
        using stereo = catenae::basic_loudness_meter<sample, 2, 960, 5>;
        const auto m = stereo::make(48000.0);
        ASSERT_TRUE(m.has_value());
        std::vector<piece<2>> nine;
        for (int i = 0; i < 5; ++i) {
            nine.push_back(all_at<2>(1.34, -20));
            nine.push_back(all_at<2>(1.66, -30));
        }
        auto st = rested<stereo>();
        stream(*m, *st, 48000.0, nine, [](std::size_t frame, const typename stereo::result_type& p) {
            if (frame >= 3 * 48000) {
                EXPECT_NEAR(lufs_of(p.short_term), -23.0, 0.1) << frame;
            }
        });
        std::vector<piece<2>> twelve;
        for (int i = 0; i < 25; ++i) {
            twelve.push_back(all_at<2>(0.18, -20));
            twelve.push_back(all_at<2>(0.22, -30));
        }
        st = rested<stereo>();
        stream(*m, *st, 48000.0, twelve, [](std::size_t frame, const typename stereo::result_type& p) {
            if (frame >= 48000) {
                EXPECT_NEAR(lufs_of(p.momentary), -23.0, 0.1) << frame;
            }
        });
    }

    // Verifies CAT-LOUDNESS-002: tests 10, 11, 13 and 14, the windows' largest readings.
    TYPED_TEST(loudness, Tech3341LargestReadingsAtTwentyMilliseconds) {
        using sample = TypeParam;
        using stereo = catenae::basic_loudness_meter<sample, 2, 960, 5>;
        const auto m = stereo::make(48000.0);
        ASSERT_TRUE(m.has_value());
        for (int i = 0; i < 20; ++i) {
            // 10 and 13, file by file: each segment from reset.
            const auto ten =
                read(*m, 48000.0, {all_at<2>(0.15 * i, k_silence), all_at<2>(3.0, -23), all_at<2>(1.0, k_silence)});
            EXPECT_NEAR(ten.short_term, -23.0, 0.1) << "test 10, " << i;
            const auto thirteen =
                read(*m, 48000.0, {all_at<2>(0.02 * i, k_silence), all_at<2>(0.4, -23), all_at<2>(1.0, k_silence)});
            EXPECT_NEAR(thirteen.momentary, -23.0, 0.1) << "test 13, " << i;
        }
        // 11 and 14, live: one stream, each segment's largest reading.
        std::vector<piece<2>> eleven;
        std::vector<piece<2>> fourteen;
        for (int i = 0; i < 20; ++i) {
            eleven.push_back(all_at<2>(0.15 * i, k_silence));
            eleven.push_back(all_at<2>(3.0, -38.0 + i));
            eleven.push_back(all_at<2>(3.0 - 0.15 * i, k_silence));
            fourteen.push_back(all_at<2>(0.02 * i, k_silence));
            fourteen.push_back(all_at<2>(0.4, -38.0 + i));
            fourteen.push_back(all_at<2>(0.4 - 0.02 * i, k_silence));
        }
        std::array<double, 20> largest{};
        largest.fill(-1e9);
        auto st = rested<stereo>();
        stream(*m, *st, 48000.0, eleven, [&](std::size_t frame, const typename stereo::result_type& p) {
            const std::size_t segment = (frame - 1) / (6 * 48000);
            if (segment < 20) {
                largest[segment] = std::fmax(largest[segment], lufs_of(p.short_term));
            }
        });
        for (int i = 0; i < 20; ++i) {
            EXPECT_NEAR(largest[std::size_t(i)], -38.0 + i, 0.1) << "test 11, " << i;
        }
        largest.fill(-1e9);
        st = rested<stereo>();
        stream(*m, *st, 48000.0, fourteen, [&](std::size_t frame, const typename stereo::result_type& p) {
            const std::size_t segment = (frame - 1) / (48000 * 4 / 5);
            if (segment < 20) {
                largest[segment] = std::fmax(largest[segment], lufs_of(p.momentary));
            }
        });
        for (int i = 0; i < 20; ++i) {
            EXPECT_NEAR(largest[std::size_t(i)], -38.0 + i, 0.1) << "test 14, " << i;
        }
    }

    // Verifies CAT-LOUDNESS-002: tests 1 and 2 at 44.1 and 96 kHz.
    TYPED_TEST(loudness, Tech3341AtOtherRates) {
        using sample = TypeParam;
        using at441  = catenae::basic_loudness_meter<sample, 2, 882, 5>;
        using at96   = catenae::basic_loudness_meter<sample, 2, 1920, 5>;
        const auto a = at441::make(44100.0);
        const auto b = at96::make(96000.0);
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        for (const double level : {-23.0, -33.0}) {
            const auto ra = read(*a, 44100.0, {all_at<2>(20.0, level)});
            const auto rb = read(*b, 96000.0, {all_at<2>(20.0, level)});
            for (const readings& r : {ra, rb}) {
                EXPECT_NEAR(r.momentary, level, 0.1) << level;
                EXPECT_NEAR(r.short_term, level, 0.1) << level;
                EXPECT_NEAR(r.integrated, level, 0.1) << level;
            }
        }
    }

    // Verifies CAT-LOUDNESS-003.
    TYPED_TEST(loudness, IntegratedIsTheHistogramsGatedMean) {
        using sample = TypeParam;
        using mono   = catenae::basic_loudness_meter<sample, 1, 960, 5>;
        const auto m = mono::make(48000.0);
        ASSERT_TRUE(m.has_value());
        // 0 until the first block: the fourth step's end.
        auto st = rested<mono>();
        stream(*m, *st, 48000.0, {all_at<1>(0.4, -20.0)}, [&](std::size_t frame, const typename mono::result_type& p) {
            if (frame < 19200) {
                EXPECT_EQ(p.integrated, sample(0)) << frame;
            }
            else {
                EXPECT_GT(p.integrated, sample(0)) << frame;
            }
        });
        // Below the absolute gate nothing is counted.
        EXPECT_TRUE(std::isinf(read(*m, 48000.0, {all_at<1>(5.0, -75.0)}).integrated));
        // A programme of levels from -60 to -12 dBFS: the meter's integrated value is the card's
        // histogram over its own blocks (each step's momentary power, from the fourth), and within
        // 0.05 LU of BS.1770-4's exact gated mean over them.
        std::vector<piece<1>> programme;
        catenae_test::signal  levels(31);
        for (int i = 0; i < 40; ++i) {
            programme.push_back(all_at<1>(0.73, -60.0 + double(levels.next<std::uint32_t>() % 49)));
        }
        std::vector<double> blocks;
        sample              integrated = 0;
        st                             = rested<mono>();
        stream(*m, *st, 48000.0, programme, [&](std::size_t frame, const typename mono::result_type& p) {
            if (frame % 4800 == 0 && frame >= 19200) {
                blocks.push_back(lufs_of(p.momentary));
            }
            integrated = p.integrated;
        });
        const auto gated = [&](bool binned) {
            std::vector<double> l;
            for (const double b : blocks) {
                if (b > -70.0) {
                    l.push_back(binned ? -70.0 + 0.1 * std::fmin(999.0, std::round((b + 70.0) * 10.0)) : b);
                }
            }
            double sum = 0.0;
            for (const double x : l) {
                sum += std::pow(10.0, (x + 0.691) / 10.0);
            }
            const double gate  = 10.0 * std::log10(sum / double(l.size())) - 0.691 - 10.0;
            double       kept  = 0.0;
            double       total = 0.0;
            for (const double x : l) {
                if (x > gate) {
                    kept += 1.0;
                    total += std::pow(10.0, (x + 0.691) / 10.0);
                }
            }
            return 10.0 * std::log10(total / kept) - 0.691;
        };
        EXPECT_NEAR(lufs_of(integrated), gated(true), 0.002);
        EXPECT_NEAR(lufs_of(integrated), gated(false), 0.05);
        // Above the top bin, the top bin: +29.9 LUFS.
        const auto loud = mono::make(48000.0, {sample(4000.0)});
        ASSERT_TRUE(loud.has_value());
        EXPECT_NEAR(read(*loud, 48000.0, {all_at<1>(1.0, 0.0)}).integrated, 29.9, 0.002);
        // The bins' constants.
        EXPECT_NEAR(std::pow(10.0, (-70.0 + 0.691) / 10.0), 1.1724653045822956e-07, 1e-20);
        EXPECT_NEAR(std::pow(10.0, 0.01), 1.023292992280754, 1e-15);
    }

    // Verifies CAT-LOUDNESS-004.
    TYPED_TEST(loudness, MakeChecksTheStepAndWeights) {
        using sample = TypeParam;
        using stereo = catenae::basic_loudness_meter<sample, 2, 960, 5>;
        EXPECT_TRUE(stereo::make(48000.0).has_value());
        EXPECT_TRUE(stereo::make(47995.0).has_value()); // 4799.5 rounds to 4800 frames
        EXPECT_EQ(stereo::make(44100.0).error(), catenae::status::out_of_range);
        EXPECT_EQ(stereo::make(48010.0).error(), catenae::status::out_of_range);
        EXPECT_EQ(stereo::make(0.0).error(), catenae::status::invalid_argument);
        EXPECT_EQ(stereo::make(48000.0, {sample(1), sample(-1)}).error(), catenae::status::invalid_argument);
        EXPECT_EQ(stereo::make(48000.0, {sample(1), std::numeric_limits<sample>::quiet_NaN()}).error(),
                  catenae::status::invalid_argument);
        EXPECT_EQ(stereo::make(48000.0, {sample(1), std::numeric_limits<sample>::infinity()}).error(),
                  catenae::status::invalid_argument);
        EXPECT_TRUE(stereo::make(48000.0, {sample(0), sample(0)}).has_value());
        // A table at run time with a weight it refuses reaches the handler.
        const auto design = catenae::k_weighting_design<sample>::make(48000.0);
        ASSERT_TRUE(design.has_value());
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(stereo::from_table(*design, {sample(1), sample(-0.5)})));
        // A NaN holds the filter's state until reset: the windows read NaN, and no block of it
        // is counted.
        auto                st    = rested<stereo>();
        const stereo        meter = *stereo::make(48000.0);
        std::vector<sample> in(2 * 960 * 40, sample(0.1));
        in[2 * 960 * 2] = std::numeric_limits<sample>::quiet_NaN();
        meter.analyze(*st, catenae::span<const sample>(in));
        EXPECT_TRUE(std::isnan(meter.result(*st).momentary));
        EXPECT_TRUE(std::isnan(meter.result(*st).short_term));
        EXPECT_EQ(meter.result(*st).integrated, sample(0));
    }

} // namespace
