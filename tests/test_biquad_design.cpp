/// @file test_biquad_design.cpp
/// @brief The recursive designs' contract (include/catenae/biquad_design.h).
// Copyright 2026 Timothy Place and the catenae contributors
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

#include "catenae/biquad_design.h"
#include "catenae/check.h"
#include "catenae_test/check_trap.h"

namespace {

    template <typename T>
    class biquad_design_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(biquad_design_formats, formats, );

    using catenae::biquad_response;
    using catenae::biquad_spec;
    using row_d = catenae::detail::biquad_row<double>;

    constexpr double k_pi = 3.141592653589793238462643383279502884;

    /// The cookbook's formula as it is printed (1 - cos w0, not the design's rewriting), a0
    /// divided out.
    row_d cookbook(const biquad_spec& s) {
        const double w0    = 2.0 * k_pi * s.frequency;
        const double cs    = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * s.q);
        const double a     = std::pow(10.0, s.gain_db / 40.0);
        const double sa    = 2.0 * std::sqrt(a) * alpha;
        double       b0    = 0.0;
        double       b1    = 0.0;
        double       b2    = 0.0;
        double       a0    = 1.0 + alpha;
        double       a1    = -2.0 * cs;
        double       a2    = 1.0 - alpha;
        switch (s.response) {
        case biquad_response::lowpass:
            b0 = (1.0 - cs) / 2.0;
            b1 = 1.0 - cs;
            b2 = (1.0 - cs) / 2.0;
            break;
        case biquad_response::highpass:
            b0 = (1.0 + cs) / 2.0;
            b1 = -(1.0 + cs);
            b2 = (1.0 + cs) / 2.0;
            break;
        case biquad_response::bandpass_skirt:
            b0 = std::sin(w0) / 2.0;
            b1 = 0.0;
            b2 = -std::sin(w0) / 2.0;
            break;
        case biquad_response::bandpass_peak:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            break;
        case biquad_response::notch:
            b0 = 1.0;
            b1 = -2.0 * cs;
            b2 = 1.0;
            break;
        case biquad_response::allpass:
            b0 = 1.0 - alpha;
            b1 = -2.0 * cs;
            b2 = 1.0 + alpha;
            break;
        case biquad_response::peaking:
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * cs;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a2 = 1.0 - alpha / a;
            break;
        case biquad_response::low_shelf:
            b0 = a * ((a + 1.0) - (a - 1.0) * cs + sa);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cs);
            b2 = a * ((a + 1.0) - (a - 1.0) * cs - sa);
            a0 = (a + 1.0) + (a - 1.0) * cs + sa;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cs);
            a2 = (a + 1.0) + (a - 1.0) * cs - sa;
            break;
        case biquad_response::high_shelf:
            b0 = a * ((a + 1.0) + (a - 1.0) * cs + sa);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cs);
            b2 = a * ((a + 1.0) + (a - 1.0) * cs - sa);
            a0 = (a + 1.0) - (a - 1.0) * cs + sa;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cs);
            a2 = (a + 1.0) - (a - 1.0) * cs - sa;
            break;
        }
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }

    /// H(f) of rows held in any format, in double.
    template <typename C, std::size_t K>
    std::complex<double> response(const std::array<catenae::detail::biquad_row<C>, K>& rows, double f) {
        const std::complex<double> z1 = std::polar(1.0, -2.0 * k_pi * f);
        const std::complex<double> z2 = z1 * z1;
        std::complex<double>       h  = 1.0;
        for (const auto& c : rows) {
            h *= (static_cast<double>(c[0]) + static_cast<double>(c[1]) * z1 + static_cast<double>(c[2]) * z2)
                 / (1.0 + static_cast<double>(c[3]) * z1 + static_cast<double>(c[4]) * z2);
        }
        return h;
    }

    /// The group delay at f, by a central difference of the unwrapped phase.
    template <typename C, std::size_t K>
    double group_delay(const std::array<catenae::detail::biquad_row<C>, K>& rows, double f) {
        constexpr double h  = 1e-6;
        const double     lo = f - h < 0.0 ? 0.0 : f - h;
        const double     hi = f + h > 0.5 ? 0.5 : f + h;
        if (f <= 0.0 || f >= 0.5) {
            // At DC and Nyquist the phase is odd about f: twice the one-sided difference.
            const double at = f <= 0.0 ? 0.0 : 0.5;
            const double to = f <= 0.0 ? h : 0.5 - h;
            const double d  = std::arg(response(rows, to) / response(rows, at));
            return -d / (2.0 * k_pi * (to - at));
        }
        return -std::arg(response(rows, hi) / response(rows, lo)) / (2.0 * k_pi * (hi - lo));
    }

    constexpr std::array<biquad_response, 9> k_responses = {
        biquad_response::lowpass,       biquad_response::highpass,  biquad_response::bandpass_skirt,
        biquad_response::bandpass_peak, biquad_response::notch,     biquad_response::allpass,
        biquad_response::peaking,       biquad_response::low_shelf, biquad_response::high_shelf};

    /// The gain at f0 the cookbook gives each response.
    double gain_at_f0(const biquad_spec& s) {
        const double a = std::pow(10.0, s.gain_db / 40.0);
        switch (s.response) {
        case biquad_response::lowpass:
        case biquad_response::highpass:
        case biquad_response::bandpass_skirt:
            return s.q;
        case biquad_response::bandpass_peak:
        case biquad_response::allpass:
            return 1.0;
        case biquad_response::notch:
            return 0.0;
        case biquad_response::peaking:
            return a * a;
        case biquad_response::low_shelf:
        case biquad_response::high_shelf:
            return a; // a shelf: half its gain in dB
        }
        return 0.0;
    }

    /// The nominal latency's frequency: Nyquist for the high-pass, f0 for the band-passes, DC
    /// for the rest.
    double reference_frequency(const biquad_spec& spec) {
        if (spec.response == biquad_response::highpass) {
            return 0.5;
        }
        const bool band =
            spec.response == biquad_response::bandpass_skirt || spec.response == biquad_response::bandpass_peak;
        return band ? spec.frequency : 0.0;
    }

    /// One spec through make() in S, against the formula as printed.
    template <typename S>
    void is_its_formula(const biquad_spec& spec) {
        const double tolerance = std::is_same<S, float>::value ? 1e-6 : 1e-12;
        const auto   made      = catenae::biquad_design<S>::make(spec);
        ASSERT_TRUE(made.has_value()) << static_cast<int>(spec.response) << " at " << spec.frequency;
        const row_d want = cookbook(spec);
        const auto& got  = made->table()[0];
        for (std::size_t j = 0; j < 5; ++j) {
            // Within the formula's own rounding (its 1 - cos w0 at f0 = 0.01) and S's.
            ASSERT_NEAR(static_cast<double>(got[j]), want[j], tolerance * (1.0 + std::fabs(want[j])))
                << static_cast<int>(spec.response) << " coefficient " << j << " at " << spec.frequency << ", Q "
                << spec.q;
        }
        const double owed = gain_at_f0(spec);
        EXPECT_NEAR(std::abs(response(made->table(), spec.frequency)), owed, 1e4 * tolerance * (1.0 + owed))
            << static_cast<int>(spec.response) << " at " << spec.frequency << ", Q " << spec.q;
        if (spec.response == biquad_response::allpass) {
            for (const double f : {0.0, 0.05, 0.2, 0.45}) {
                EXPECT_NEAR(std::abs(response(made->table(), f)), 1.0, 1e3 * tolerance);
            }
        }
        // The nominal latency is the group delay at the reference frequency, of the double design,
        // stored as a float.
        const std::array<row_d, 1> exact = {{want}};
        const double               gd    = group_delay(exact, reference_frequency(spec));
        EXPECT_NEAR(static_cast<double>(made->latency().nominal), gd, 1e-3 * (1.0 + std::fabs(gd)))
            << static_cast<int>(spec.response) << " at " << spec.frequency << ", Q " << spec.q;
        EXPECT_GE(made->latency().deviation, 0.0f);
    }

    // Verifies CAT-BIQUAD-DESIGN-001.
    TYPED_TEST(biquad_design_formats, EachCookbookResponseIsItsFormula) {
#if defined(CATENAE_TEST_EMULATED)
        // Each make() scans 1,025 points in software double there: one point of the grid below.
        const std::vector<double> frequencies = {0.1};
        const std::vector<double> qs          = {4.0};
        const std::vector<double> gains       = {9.0};
#else
        const std::vector<double> frequencies = {0.01, 0.1, 0.3};
        const std::vector<double> qs          = {0.5, 0.7071, 4.0};
        const std::vector<double> gains       = {-6.0, 9.0};
#endif
        for (const biquad_response r : k_responses) {
            for (const double f0 : frequencies) {
                for (const double q : qs) {
                    for (const double g : gains) {
                        is_its_formula<TypeParam>({r, f0, q, g});
                    }
                }
            }
        }
    }

    /// The largest |group delay - nominal| over 4,097 points from 0 to 1/2, where the gain is
    /// within 3 dB of its largest there, or everywhere.
    double deviation_by_scan(const std::array<row_d, 1>& rows, double nominal, bool everywhere) {
        constexpr int points = 4096;
        double        peak   = 0.0;
        for (int i = 0; i <= points; ++i) {
            const double m = std::abs(response(rows, 0.5 * i / points));
            peak           = m > peak ? m : peak;
        }
        double dev = 0.0;
        for (int i = 0; i <= points; ++i) {
            const double f = 0.5 * i / points;
            if (everywhere || std::abs(response(rows, f)) >= peak / std::sqrt(2.0)) {
                const double d = std::fabs(group_delay(rows, f) - nominal);
                dev            = d > dev ? d : dev;
            }
        }
        return dev;
    }

    // Verifies CAT-BIQUAD-DESIGN-001.
    TEST(biquad_design, TheDeviationIsReadOverThePassband) {
        // A low-pass's is over the band within 3 dB of its largest gain: at Q = 1/sqrt(2) the
        // band ends at f0, and its group delay peaks inside it, near 0.07.
        const auto lp = catenae::biquad_design<double>::make({biquad_response::lowpass, 0.1, 0.70710678118654752, 0.0});
        ASSERT_TRUE(lp.has_value());
        const double lp_dev = deviation_by_scan(lp->table(), group_delay(lp->table(), 0.0), false);
        EXPECT_NEAR(static_cast<double>(lp->latency().deviation), lp_dev, 0.01 * lp_dev);
        EXPECT_GT(group_delay(lp->table(), 0.07), group_delay(lp->table(), 0.1)); // the peak is inside the band
        // A band-pass's is around f0, where its nominal is read.
        const auto bp = catenae::biquad_design<double>::make({biquad_response::bandpass_peak, 0.2, 3.0, 0.0});
        ASSERT_TRUE(bp.has_value());
        const double bp_dev = deviation_by_scan(bp->table(), group_delay(bp->table(), 0.2), false);
        EXPECT_NEAR(static_cast<double>(bp->latency().deviation), bp_dev, 0.01 * bp_dev);
        // An all-pass's is over every frequency: its group delay peaks near f0.
        const auto ap = catenae::biquad_design<double>::make({biquad_response::allpass, 0.2, 2.0, 0.0});
        ASSERT_TRUE(ap.has_value());
        const double ap_dev = deviation_by_scan(ap->table(), group_delay(ap->table(), 0.0), true);
        EXPECT_NEAR(static_cast<double>(ap->latency().deviation), ap_dev, 0.01 * ap_dev);
        // A peaking EQ of 0 dB delays nothing.
        const auto flat = catenae::biquad_design<double>::make({biquad_response::peaking, 0.2, 2.0, 0.0});
        ASSERT_TRUE(flat.has_value());
        EXPECT_NEAR(static_cast<double>(flat->latency().nominal), 0.0, 1e-12);
        EXPECT_NEAR(static_cast<double>(flat->latency().deviation), 0.0, 1e-12);
    }

    template <typename S, std::size_t Order>
    void butterworth_is_its_definition(double fc) {
        const auto made = catenae::butterworth_design<S, Order>::make(fc);
        ASSERT_TRUE(made.has_value()) << Order << " at " << fc;
        const auto& rows = made->table();
        // The coefficients rounded to float move the response most at a low cutoff: measured at
        // the battery's, 7.5e-7 at the cutoff and 2.3e-5 at DC (order 8 at 0.01), so twice that.
        const bool   is_float = std::is_same<S, float>::value;
        const double at_cut   = is_float ? 1.5e-6 : 1e-9;
        const double at_dc    = is_float ? 5e-5 : 1e-9;
        EXPECT_NEAR(std::abs(response(rows, fc)), 1.0 / std::sqrt(2.0), at_cut) << Order << " at " << fc;
        EXPECT_NEAR(std::abs(response(rows, 0.0)), 1.0, at_dc) << Order << " at " << fc;
        EXPECT_EQ(std::abs(response(rows, 0.5)), 0.0) << Order << " at " << fc; // b0 - b1 + b2 is 0 exactly
        const double tol = is_float ? 1e-6 : 1e-12;
        // Each section is the cookbook low-pass at a Butterworth Q, the lowest Q first, so the
        // poles' radius, sqrt(a2), increases.
        double last_radius = 0.0;
        for (std::size_t i = 0; i < Order / 2; ++i) {
            const std::size_t k = Order / 2 - 1 - i;
            const double      q = 1.0 / (2.0 * std::sin(static_cast<double>(2 * k + 1) * k_pi / (2.0 * Order)));
            const row_d       c = cookbook({biquad_response::lowpass, fc, q, 0.0});
            for (std::size_t j = 0; j < 5; ++j) {
                EXPECT_NEAR(static_cast<double>(rows[i][j]), c[j], tol * (1.0 + std::fabs(c[j])))
                    << Order << " section " << i << " coefficient " << j;
            }
            const double radius = std::sqrt(static_cast<double>(rows[i][4]));
            EXPECT_GT(radius, last_radius) << Order << " section " << i;
            last_radius = radius;
        }
    }

    // Verifies CAT-BIQUAD-DESIGN-002.
    TYPED_TEST(biquad_design_formats, AButterworthIsMinusThreeDecibelsAtItsCutoff) {
        for (const double fc : {0.01, 0.1, 0.25, 0.45}) {
            butterworth_is_its_definition<TypeParam, 2>(fc);
            butterworth_is_its_definition<TypeParam, 4>(fc);
            butterworth_is_its_definition<TypeParam, 8>(fc);
            butterworth_is_its_definition<TypeParam, 16>(fc);
        }
        // DspTap's latency numbers at order 4: 2.51 frames of deviation on a nominal of 4.02 at
        // 0.1, 2.39 on 1.31 at 0.25.
        const auto at_tenth = catenae::butterworth_design<TypeParam, 4>::make(0.1);
        const auto at_fifth = catenae::butterworth_design<TypeParam, 4>::make(0.25);
        ASSERT_TRUE(at_tenth.has_value() && at_fifth.has_value());
        EXPECT_NEAR(at_tenth->latency().nominal, 4.02f, 0.005f);
        EXPECT_NEAR(at_tenth->latency().deviation, 2.51f, 0.005f);
        EXPECT_NEAR(at_fifth->latency().nominal, 1.31f, 0.005f);
        EXPECT_NEAR(at_fifth->latency().deviation, 2.39f, 0.005f);
    }

    template <typename E>
    catenae::status error_of(const E& e) {
        return e.has_value() ? catenae::status{} : e.error();
    }

    template <typename S, std::size_t Order>
    void butterworth_bounds() {
        using b             = catenae::butterworth_design<S, Order>;
        const double lowest = catenae::min_frequency<S>;
        EXPECT_TRUE(b::make(lowest).has_value()) << Order;
        EXPECT_TRUE(b::make(0.5 - lowest).has_value()) << Order;
        EXPECT_EQ(error_of(b::make(lowest * 0.99)), catenae::status::out_of_range) << Order;
        EXPECT_EQ(error_of(b::make(0.5 - lowest * 0.99)), catenae::status::out_of_range) << Order;
        EXPECT_EQ(error_of(b::make(0.0)), catenae::status::out_of_range) << Order;
        EXPECT_EQ(error_of(b::make(std::numeric_limits<double>::quiet_NaN())), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(b::make(std::numeric_limits<double>::infinity())), catenae::status::invalid_argument);
    }

    // Verifies CAT-BIQUAD-DESIGN-003.
    TYPED_TEST(biquad_design_formats, MakeRejectsWhatItsCardNames) {
        using d                 = catenae::biquad_design<TypeParam>;
        constexpr double nan    = std::numeric_limits<double>::quiet_NaN();
        constexpr double inf    = std::numeric_limits<double>::infinity();
        const double     lowest = catenae::min_frequency<TypeParam>;
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, nan, 0.7, 0.0})), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, inf, 0.7, 0.0})), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, 0.1, 0.0, 0.0})), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, 0.1, -1.0, 0.0})), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, 0.1, nan, 0.0})), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(d::make({biquad_response::peaking, 0.1, 0.7, nan})), catenae::status::invalid_argument);
        EXPECT_EQ(error_of(d::make({static_cast<biquad_response>(99), 0.1, 0.7, 0.0})),
                  catenae::status::invalid_argument);
        EXPECT_TRUE(d::make({biquad_response::lowpass, 0.1, 0.7, nan}).has_value()); // a low-pass reads no gain
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, lowest * 0.99, 0.7, 0.0})),
                  catenae::status::out_of_range);
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, 0.5, 0.7, 0.0})), catenae::status::out_of_range);
        EXPECT_EQ(error_of(d::make({biquad_response::lowpass, -0.1, 0.7, 0.0})), catenae::status::out_of_range);
        // Coefficients that overflow TypeParam.
        EXPECT_EQ(error_of(d::make({biquad_response::peaking, 0.1, 0.7, 4000.0})), catenae::status::out_of_range);
        for (const biquad_response r : k_responses) {
            EXPECT_TRUE(d::make({r, lowest, 0.7, 6.0}).has_value()) << static_cast<int>(r);
            EXPECT_TRUE(d::make({r, 0.5 - lowest, 0.7, 6.0}).has_value()) << static_cast<int>(r);
        }
        butterworth_bounds<TypeParam, 2>();
        butterworth_bounds<TypeParam, 4>();
        butterworth_bounds<TypeParam, 8>();
        butterworth_bounds<TypeParam, 16>();
    }

    /// The transition matrix of the rows as S runs them, from the kernel itself: column j is
    /// one zero-input frame from the state e_j. Independent of the design's algebra.
    template <typename S, std::size_t K>
    std::vector<double> kernel_transition(const std::array<catenae::detail::biquad_row<S>, K>& rows) {
        constexpr std::size_t D = 2 * K;
        std::vector<double>   a(D * D);
        std::array<row_d, K>  exact{};
        for (std::size_t k = 0; k < K; ++k) {
            for (std::size_t j = 0; j < 5; ++j) {
                exact[k][j] = static_cast<double>(rows[k][j]);
            }
        }
        for (std::size_t j = 0; j < D; ++j) {
            std::array<double, D> z{};
            z[j] = 1.0;
            // The kernel's state order is z1[K] then z2[K]; the matrix's is (z1, z2) per section.
            std::array<double, D> kz{};
            for (std::size_t k = 0; k < K; ++k) {
                kz[k]     = z[2 * k];
                kz[K + k] = z[2 * k + 1];
            }
            static_cast<void>(catenae::detail::biquad_cascade_frame<double, K>(exact, kz.data(), 0.0));
            for (std::size_t k = 0; k < K; ++k) {
                a[(2 * k) * D + j]     = kz[k];
                a[(2 * k + 1) * D + j] = kz[K + k];
            }
        }
        return a;
    }

    /// The first n with start + ln ||A^n||_inf < goal, and one past the last n without, up to a
    /// horizon: multiplying A in, one frame at a time.
    struct crossings {
        std::size_t first;
        std::size_t after_last;
    };
    /// p a, both D x D.
    template <std::size_t D>
    std::vector<double> product(const std::vector<double>& p, const std::vector<double>& a) {
        std::vector<double> r(D * D, 0.0);
        for (std::size_t i = 0; i < D; ++i) {
            for (std::size_t k = 0; k < D; ++k) {
                for (std::size_t j = 0; j < D; ++j) {
                    r[i * D + j] += p[i * D + k] * a[k * D + j];
                }
            }
        }
        return r;
    }

    /// The largest row sum of magnitudes.
    template <std::size_t D>
    double inf_norm(const std::vector<double>& m) {
        double norm = 0.0;
        for (std::size_t i = 0; i < D; ++i) {
            double r = 0.0;
            for (std::size_t j = 0; j < D; ++j) {
                r += std::fabs(m[i * D + j]);
            }
            norm = r > norm ? r : norm;
        }
        return norm;
    }

    template <std::size_t D>
    crossings frames_until(const std::vector<double>& a, double start, double goal, std::size_t horizon) {
        std::vector<double> p(D * D, 0.0);
        for (std::size_t i = 0; i < D; ++i) {
            p[i * D + i] = 1.0;
        }
        double    scale = 0.0;
        crossings c{0, 1};
        for (std::size_t n = 1; n <= horizon; ++n) {
            p                 = product<D>(p, a);
            const double norm = inf_norm<D>(p);
            for (double& v : p) {
                v /= norm;
            }
            scale += std::log(norm);
            if (start + scale < goal) {
                c.first = c.first == 0 ? n : c.first;
            }
            else {
                c.after_last = n + 1;
            }
        }
        return c;
    }

    template <typename S, std::size_t K>
    void tails_are_their_decay(const catenae::biquad_design<S, K>& d, const char* what) {
        const std::vector<double> a = kernel_transition<S, K>(d.table());
        // ||A^n|| is not monotone: it rises and falls within a period of the slowest pole, so
        // the bound can be met at one frame and missed at a later one. The search lands between
        // the first frame it is met and the last it is missed (the card's honest limit).
        const double      top       = std::log(static_cast<double>(std::numeric_limits<S>::max()));
        const double      bottom    = std::log(static_cast<double>(std::numeric_limits<S>::min()));
        const std::size_t horizon   = 2 * d.tail_frames() + 64;
        const crossings   exact     = frames_until<2 * K>(a, top, bottom, horizon);
        const crossings   practical = frames_until<2 * K>(a, 0.0, std::log(1e-6), horizon);
        EXPECT_GE(d.tail_frames(), exact.first + K) << what;
        EXPECT_LE(d.tail_frames(), exact.after_last + K) << what;
        EXPECT_GE(d.practical_tail_frames(), practical.first + K) << what;
        EXPECT_LE(d.practical_tail_frames(), practical.after_last + K) << what;
        EXPECT_GT(d.tail_frames(), d.practical_tail_frames()) << what;
        EXPECT_EQ(d.flush_residue(), catenae::detail::residue_bound<S>()) << what;
    }

    // Verifies CAT-BIQUAD-DESIGN-004.
    TYPED_TEST(biquad_design_formats, TheTailsAreTheDecayTheyState) {
        tails_are_their_decay(*catenae::butterworth_design<TypeParam, 2>::make(0.1), "order 2 at 0.1");
        tails_are_their_decay(*catenae::biquad_design<TypeParam>::make({biquad_response::allpass, 0.3, 0.7, 0.0}),
                              "all-pass");
#if !defined(CATENAE_TEST_EMULATED)
        // Under an emulator these take minutes of software double: the hosted legs run them.
        tails_are_their_decay(*catenae::butterworth_design<TypeParam, 4>::make(0.05), "order 4 at 0.05");
        tails_are_their_decay(*catenae::butterworth_design<TypeParam, 8>::make(0.2), "order 8 at 0.2");
        tails_are_their_decay(*catenae::biquad_design<TypeParam>::make({biquad_response::peaking, 0.05, 3.0, 9.0}),
                              "peaking");
        tails_are_their_decay(*catenae::biquad_design<TypeParam>::make({biquad_response::notch, 0.15, 8.0, 0.0}),
                              "notch");
#endif
    }

    // Verifies CAT-BIQUAD-DESIGN-004.
    TEST(biquad_design, TheTailsArePlansFigures) {
        // PLAN.md 4: an order-8 Butterworth's exact tail is 14,736 frames at 0.01 of the rate and
        // 149,085 at 0.001 (DspTap's float window, carried bit for bit).
        EXPECT_EQ((catenae::butterworth_design<float, 8>::make(0.01)->tail_frames()), 14736u);
        EXPECT_EQ((catenae::butterworth_design<float, 8>::make(0.001)->tail_frames()), 149085u);
    }

    // Verifies CAT-BIQUAD-DESIGN-005.
    TYPED_TEST(biquad_design_formats, FromTableAcceptsAStableTableBitForBit) {
        using d2             = catenae::biquad_design<TypeParam, 2>;
        using table          = typename d2::table_type;
        constexpr table rows = {{{TypeParam(0.25), TypeParam(0.5), TypeParam(0.25), TypeParam(-0.5), TypeParam(0.25)},
                                 {TypeParam(1), TypeParam(-1), TypeParam(0.5), TypeParam(0.1), TypeParam(0.9)}}};
        constexpr catenae::biquad_numbers numbers = {2.5f, 1.0f, 900, 120, 3e-36f};
        constexpr d2                      made = d2::from_table(rows, numbers); // a constant: checked at compile time
        static_assert(made.latency().nominal > 2.0f, "the table's numbers, as given");
        EXPECT_EQ(std::memcmp(made.table().data(), rows.data(), sizeof(table)), 0);
        EXPECT_EQ(made.tail_frames(), 900u);
        EXPECT_EQ(made.practical_tail_frames(), 120u);
        EXPECT_EQ(made.flush_residue(), 3e-36f);
    }

    // Verifies CAT-BIQUAD-DESIGN-005.
    TYPED_TEST(biquad_design_formats, FromTableRefusesAPoleOnOrOutsideTheCircle) {
        using d1            = catenae::biquad_design<TypeParam, 1>;
        using table         = typename d1::table_type;
        const auto      n   = catenae::biquad_numbers{0.0f, 0.0f, 1, 1, 0.0f};
        const TypeParam nan = std::numeric_limits<TypeParam>::quiet_NaN();
        const TypeParam inf = std::numeric_limits<TypeParam>::infinity();
        for (const table t :
             {table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(0), TypeParam(1)}}},      // |a2| = 1
              table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(0), TypeParam(-1)}}},     // a2 = -1
              table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(1.5), TypeParam(0.5)}}},  // a1 = 1 + a2
              table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(-1.5), TypeParam(0.5)}}}, // -a1 = 1 + a2
              table{
                  {{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(2.5), TypeParam(0.9)}}}, // outside the triangle
              table{{{nan, TypeParam(0), TypeParam(0), TypeParam(0), TypeParam(0.5)}}},          // a NaN
              table{{{TypeParam(1), TypeParam(0), TypeParam(0), nan, TypeParam(0.5)}}},    // a NaN in the denominator
              table{{{inf, TypeParam(0), TypeParam(0), TypeParam(0), TypeParam(0.5)}}}}) { // an infinity
            CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d1::from_table(t, n)));
        }
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(d1::from_table(
                                 table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(0), TypeParam(0.5)}}},
                                 {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1, 1, 0.0f})));
        // Judged exactly: a2 a hair inside, and a1 just below 1 + a2 where the sum rounds to 1.
        const TypeParam tiny   = std::numeric_limits<TypeParam>::min();
        const d1        inside = d1::from_table(table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(0),
                                                        std::nextafter(TypeParam(1), TypeParam(0))}}},
                                                n);
        EXPECT_LT(inside.table()[0][4], TypeParam(1));
        const d1 edge = d1::from_table(table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(1), tiny}}},
                                       n); // 1 < 1 + tiny, exactly
        EXPECT_EQ(edge.table()[0][3], TypeParam(1));
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(d1::from_table(
                                 table{{{TypeParam(1), TypeParam(0), TypeParam(0), TypeParam(1), -tiny}}}, n)));
    }

    // Verifies CAT-BIQUAD-DESIGN-006.
    TEST(biquad_design, InQ31TheTableIsQ2Point30) {
        using d1                   = catenae::biquad_design<catenae::q31, 1>;
        using table                = d1::table_type;
        constexpr std::int32_t one = std::int32_t{1} << 30;
        static_assert(std::is_same<d1::coefficient_type, std::int32_t>::value, "Q2.30 in an int32");
        constexpr d1 half = d1::from_table(table{{{one / 4, one / 2, one / 4, -one / 2, one / 4}}},
                                           catenae::biquad_numbers{1.0f, 0.5f, 10, 5, 0.0f});
        EXPECT_EQ(half.table()[0][1], one / 2);
        const auto n = catenae::biquad_numbers{0.0f, 0.0f, 1, 1, 0.0f};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(d1::from_table(table{{{one, 0, 0, 0, one}}}, n)));
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(d1::from_table(table{{{one, 0, 0, one + one / 2, one / 2}}}, n)));
        EXPECT_EQ(d1::from_table(table{{{one, 0, 0, one, 1}}}, n).table()[0][4], 1); // 2^30 < 2^30 + 1
    }

} // namespace
