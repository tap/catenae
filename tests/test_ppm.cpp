/// @file test_ppm.cpp
/// @brief The peak programme meter's contract and its design's (include/catenae/ppm_meter.h,
/// ppm_design.h).
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
#include "catenae/check.h"
#include "catenae/ppm_design.h"
#include "catenae/ppm_meter.h"
#include "catenae_test/check_trap.h"
#include "meters.h"

namespace {

    template <typename T>
    class ppm : public ::testing::Test {};
    using floating = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(ppm, floating);

    /// One channel's readings, frame by frame, over `frames` frames that `signal(i)` gives: `at(i,
    /// y)` sees each. Nothing is stored, so a run of seconds fits a Cortex-M's memory.
    template <typename T, typename Signal, typename At>
    void stream(const catenae::ppm_design<T>& design, std::size_t frames, Signal signal, At at) {
        using meter_t = catenae::basic_ppm_meter<T, 1, 1>;
        const meter_t           meter{design};
        typename meter_t::state st{};
        for (std::size_t i = 0; i < frames; ++i) {
            const T x = signal(i);
            meter.analyze(st, catenae::span<const T>(&x, 1));
            at(i, meter.result(st)[0]);
        }
    }

    /// A burst of 5 kHz at full scale from phase 0, `ms` long, then silence.
    template <typename T>
    auto burst(double rate, double ms) {
        const auto n = static_cast<std::size_t>(std::lround(ms * rate / 1000.0));
        return [rate, n](std::size_t i) {
            return i < n ? static_cast<T>(std::sin(6.283185307179586 * 5000.0 * double(i) / rate)) : T{0};
        };
    }

    /// The tolerance of a reading that follows from a design's rounding to T.
    template <typename T>
    constexpr double rounding_db() {
        return std::is_same<T, float>::value ? 0.2 : 1e-6;
    }

    /// The largest reading over `ms` of a signal, in dB.
    template <typename T, typename Signal>
    double largest_db(const catenae::ppm_design<T>& design, double rate, double ms, Signal signal) {
        double top = 0.0;
        stream(design, static_cast<std::size_t>(std::lround(ms * rate / 1000.0)), signal,
               [&top](std::size_t, T y) { top = static_cast<double>(y) > top ? static_cast<double>(y) : top; });
        return 20.0 * std::log10(top);
    }

    // Verifies CAT-PPM-DESIGN-001.
    TYPED_TEST(ppm, EbuDesignReadsItsIntegrationAndReturn) {
        using T = TypeParam;
        for (const double rate : {44100.0, 48000.0, 96000.0}) {
            const auto design = catenae::ppm_design<T>::make(catenae::ebu_ppm(rate));
            ASSERT_TRUE(design.has_value()) << rate;
            // The 10 ms burst reads 2 dB below the steady reading (the steady: 0.5 s of tone).
            const double steady = largest_db(*design, rate, 500.0, burst<T>(rate, 500.0));
            const double ten    = largest_db(*design, rate, 60.0, burst<T>(rate, 10.0));
            EXPECT_NEAR(ten - steady, -2.0, 0.01) << rate;
            // The release falls 24 dB in 2.8 s, within rounding to T.
            const double fall =
                2800.0 * rate / 1000.0 * 20.0 * std::log10(static_cast<double>(design->numbers().release));
            EXPECT_NEAR(fall, -24.0, rounding_db<T>()) << rate;
        }
        const catenae::ppm_spec ebu = catenae::ebu_ppm(48000.0);
        EXPECT_EQ(ebu.integration_ms, 10.0);
        EXPECT_EQ(ebu.return_db, 24.0);
        EXPECT_EQ(ebu.return_ms, 2800.0);
    }

    // Verifies CAT-PPM-002.
    TYPED_TEST(ppm, EbuBurstTableAsMeasured) {
        using T             = TypeParam;
        const double rate   = 48000.0;
        const auto   design = *catenae::ppm_design<T>::make(catenae::ebu_ppm(rate));
        const double steady = largest_db(design, rate, 500.0, burst<T>(rate, 500.0));
        // EBU Tech 3205-E Table 2, in dB below the steady reading, beside what this detector
        // reads: the first three within the table's tolerance, the last two below it (the card's
        // honest limit).
        struct row {
            double ms, table, tolerance, reads;
        };
        const std::array<row, 5> rows{{{100.0, 0.0, 0.5, -0.01},
                                       {10.0, -2.0, 0.5, -2.00},
                                       {5.0, -4.0, 0.75, -4.37},
                                       {1.5, -9.0, 1.0, -11.13},
                                       {0.5, -17.0, 2.0, -19.31}}};
        for (const row& r : rows) {
            const double reads = largest_db(design, rate, r.ms + 50.0, burst<T>(rate, r.ms)) - steady;
            EXPECT_NEAR(reads, r.reads, 0.05) << r.ms << " ms";
            EXPECT_EQ(std::fabs(reads - r.table) <= r.tolerance, r.ms >= 5.0) << r.ms << " ms";
        }
        // A steady 1 kHz sine reads 0.21 dB below its peak: the detector's ripple between peaks.
        const double sine_1k =
            largest_db(design, rate, 500.0,
                       [rate](std::size_t i) {
                           return static_cast<T>(0.5 * std::sin(6.283185307179586 * 1000.0 * double(i) / rate));
                       })
            - 20.0 * std::log10(0.5);
        EXPECT_NEAR(sine_1k, -0.21, 0.01);
        // A steady tone removed: the reading falls 24 dB in 2.8 s, at a constant rate.
        double at_stop = 0.0;
        double later   = 0.0;
        stream(design, 24000 + 134400, burst<T>(rate, 500.0), [&](std::size_t i, T y) {
            if (i == 24000 - 1) {
                at_stop = 20.0 * std::log10(static_cast<double>(y));
            }
            if (i == 24000 - 1 + 134400) {
                later = 20.0 * std::log10(static_cast<double>(y));
            }
        });
        EXPECT_NEAR(later - at_stop, -24.0, rounding_db<T>());
    }

    // Verifies CAT-PPM-001.
    TYPED_TEST(ppm, EachFrameFollowsTheDetector) {
        using T                       = TypeParam;
        constexpr std::size_t ch      = 2;
        constexpr std::size_t hop     = 64;
        const auto            design  = *catenae::ppm_design<T>::make(catenae::ebu_ppm(48000.0));
        const T               attack  = design.numbers().attack;
        const T               release = design.numbers().release;
        std::vector<T>        in      = analyzer_test::noise<T>(ch * (hop * 20 + 9), 31);
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] *= static_cast<T>(i % 3000 < 1500 ? 1.0 : 0.01); // loud, then quiet: both branches
        }
        std::vector<std::array<T, ch>> expected;
        std::array<T, ch>              y{};
        for (std::size_t f = 0; f * ch < in.size(); ++f) {
            for (std::size_t c = 0; c < ch; ++c) {
                const T a = std::fabs(in[f * ch + c]);
                if (a > y[c]) {
                    y[c] += attack * (a - y[c]);
                }
                else {
                    y[c] *= release;
                }
                y[c] = y[c] < std::numeric_limits<T>::min() ? T{0} : y[c];
            }
            if ((f + 1) % hop == 0) {
                expected.push_back(y);
            }
        }
        const catenae::basic_ppm_meter<T, ch, hop> meter{design};
        for (const std::size_t largest : {std::size_t{1}, std::size_t{19}, hop}) {
            EXPECT_TRUE(analyzer_test::same_results(analyzer_test::results_of(meter, in, largest, 7), expected))
                << "calls up to " << largest;
        }
    }

    // Verifies CAT-PPM-002.
    TYPED_TEST(ppm, SilenceHoldsNoSubnormalAndNaNHolds) {
        using T           = TypeParam;
        const auto design = *catenae::ppm_design<T>::make(catenae::ebu_ppm(48000.0));
        // A tiny level, its attack and then its release, reaches exactly 0, never a subnormal.
        bool reached_zero = false;
        bool subnormal    = false;
        stream(
            design, 200000, [](std::size_t i) { return i == 0 ? std::numeric_limits<T>::min() * T{1000} : T{0}; },
            [&](std::size_t, T y) {
                const bool zero = !(y > T{0});
                subnormal       = subnormal || (!zero && y < std::numeric_limits<T>::min());
                reached_zero    = reached_zero || zero;
            });
        EXPECT_FALSE(subnormal);
        EXPECT_TRUE(reached_zero);
        // A NaN holds its channel until reset.
        std::vector<T> poisoned(64, T{0.5});
        poisoned[3]  = std::numeric_limits<T>::quiet_NaN();
        const auto p = analyzer_test::results_of(catenae::basic_ppm_meter<T, 1, 8>{design}, poisoned, 8, 1);
        ASSERT_EQ(p.size(), 8u);
        for (const auto& r : p) {
            EXPECT_TRUE(std::isnan(r[0]));
        }
        EXPECT_TRUE(std::isinf(catenae::basic_ppm_meter<T, 1, 8>::dbfs(T{0})));
        EXPECT_NEAR(static_cast<double>(catenae::basic_ppm_meter<T, 1, 8>::dbfs(T{0.5})), -6.0206, 1e-4);
    }

    // Verifies CAT-PPM-DESIGN-002.
    TYPED_TEST(ppm, MakeRejectsWhatItsCardNames) {
        using d          = catenae::ppm_design<TypeParam>;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (const catenae::ppm_spec bad :
             {catenae::ppm_spec{nan, 10.0, 24.0, 2800.0}, catenae::ppm_spec{48000.0, inf, 24.0, 2800.0},
              catenae::ppm_spec{48000.0, 10.0, 0.0, 2800.0}, catenae::ppm_spec{48000.0, 10.0, 24.0, -1.0},
              catenae::ppm_spec{0.0, 10.0, 24.0, 2800.0}}) {
            const auto r = d::make(bad);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), catenae::status::invalid_argument);
        }
        // A rate not above 10 kHz; an integration above 1 s; a burst of one frame, at phase 0 a
        // single zero; and a tone so near Nyquist that 10 ms of it holds no sample near its peak.
        for (const catenae::ppm_spec far :
             {catenae::ppm_spec{10000.0, 10.0, 24.0, 2800.0}, catenae::ppm_spec{48000.0, 1001.0, 24.0, 2800.0},
              catenae::ppm_spec{48000.0, 1000.0 / 48000.0, 24.0, 2800.0},
              catenae::ppm_spec{10001.0, 10.0, 24.0, 2800.0}}) {
            const auto r = d::make(far);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), catenae::status::out_of_range);
        }
        // Accepted: 1 s, five frames of burst at 48 kHz, and 22.05 kHz.
        EXPECT_TRUE(d::make({48000.0, 1000.0, 24.0, 2800.0}).has_value());
        EXPECT_TRUE(d::make({48000.0, 5000.0 / 48000.0, 24.0, 2800.0}).has_value());
        EXPECT_TRUE(d::make({22050.0, 10.0, 24.0, 2800.0}).has_value());
    }

    // Verifies CAT-PPM-DESIGN-001.
    TEST(ppm_design, ExampleTableIsTheEbuDesign) {
        // examples/meters.h holds make(ebu_ppm(48000.0))'s numbers as printed on the hosted Linux
        // leg; another libm may round the double design to the next float.
        const auto made  = *catenae::ppm_design<float>::make(catenae::ebu_ppm(48000.0));
        const auto table = example::ebu_ppm_48k.numbers();
        const auto ulp   = [](float x) { return std::nextafter(x, 2.0f) - x; };
        EXPECT_LE(std::fabs(made.numbers().attack - table.attack), ulp(table.attack));
        EXPECT_LE(std::fabs(made.numbers().release - table.release), ulp(table.release));
    }

    // Verifies CAT-PPM-DESIGN-003.
    TYPED_TEST(ppm, FromTableChecksItsNumbers) {
        using d          = catenae::ppm_design<TypeParam>;
        const auto made  = *d::make(catenae::ebu_ppm(48000.0));
        const auto again = d::from_table(made.numbers());
        EXPECT_EQ(again.numbers().attack, made.numbers().attack);
        EXPECT_EQ(again.numbers().release, made.numbers().release);
        auto n    = made.numbers();
        n.release = TypeParam{1};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(n)));
        n.release = TypeParam{0.5};
        n.attack  = TypeParam{0};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(n)));
        n.attack = std::numeric_limits<TypeParam>::quiet_NaN();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(n)));
        constexpr auto table = d::from_table({TypeParam{0.25}, TypeParam{0.5}});
        static_assert(table.numbers().attack == TypeParam{0.25}, "a constant design from a table");
    }

} // namespace
