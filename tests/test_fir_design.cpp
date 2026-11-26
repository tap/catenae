/// @file test_fir_design.cpp
/// @brief The FIR designs' contract (include/catenae/fir_design.h).
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
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/check.h"
#include "catenae/fir.h"
#include "catenae/fir_design.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::fir_bandpass;
    using catenae::fir_bandstop;
    using catenae::fir_highpass;
    using catenae::fir_lowpass;
    using catenae::status;

    constexpr double k_pi = 3.141592653589793;

    /// A design by whichever make() its size takes: a longer one into a table of its own, which
    /// the next design of the same type and spec reuses.
    template <typename S, std::size_t Taps, typename Spec>
    catenae::expected<catenae::fir_design<S, Taps>, status> design(const Spec& spec) {
        using d = catenae::fir_design<S, Taps>;
        if constexpr (d::holds_by_value) {
            return d::make(spec);
        }
        else {
            static std::array<typename d::coefficient_type, Taps> table{};
            static std::array<double, d::workspace_size>          workspace{};
            return d::make(spec, catenae::span<typename d::coefficient_type, Taps>(table),
                           catenae::span<double, d::workspace_size>(workspace));
        }
    }

    /// A tap as a real number.
    template <typename S, typename C>
    double real(C c) {
        if constexpr (std::is_integral<C>::value) {
            return static_cast<double>(c) / catenae::sample_traits<catenae::storage_t<S>>::coeff_scale;
        }
        else {
            return static_cast<double>(c);
        }
    }

    /// |H(f)| of a design's taps.
    template <typename S, std::size_t Taps>
    double gain(const catenae::fir_design<S, Taps>& d, double f) {
        std::complex<double>       sum{};
        const std::complex<double> turn = std::polar(1.0, -2.0 * k_pi * f);
        std::complex<double>       at{1.0, 0.0};
        for (std::size_t k = 0; k < Taps; ++k) {
            sum += real<S>(d.table()[k]) * at;
            at *= turn;
        }
        return std::abs(sum);
    }

    /// The bands of a response: what passes, what stops, measured past `margin` of the
    /// estimate's transition from each edge.
    struct bands {
        double worst_stop; // the largest gain in the stopbands
        double worst_pass; // the largest departure from 1 in the passbands
    };

    template <std::size_t Taps>
    bands measure(const catenae::fir_design<double, Taps>& d, double low, double high, bool pass_below,
                  bool pass_between, bool pass_above, double attenuation, double margin) {
        const double df     = (attenuation - 8.0) / (2.285 * 2.0 * k_pi * static_cast<double>(Taps));
        const double clear  = margin * df;
        bands        result = {0.0, 0.0};
#if defined(CATENAE_TEST_EMULATED)
        constexpr int grid = 512;
#else
        constexpr int grid = 2048;
#endif
        for (int i = 0; i <= grid; ++i) {
            const double f = 0.5 * static_cast<double>(i) / grid;
            bool         passes;
            if (f <= low - clear) {
                passes = pass_below;
            }
            else if (f >= low + clear && f <= high - clear) {
                passes = pass_between;
            }
            else if (f >= high + clear) {
                passes = pass_above;
            }
            else {
                continue; // a transition
            }
            const double g = gain(d, f);
            if (passes) {
                result.worst_pass = std::max(result.worst_pass, std::fabs(g - 1.0));
            }
            else {
                result.worst_stop = std::max(result.worst_stop, g);
            }
        }
        return result;
    }

    double db(double x) { return 20.0 * std::log10(x); }

    /// Whether two floats are one value, without ==.
    constexpr bool same(float a, float b) { return !(a < b) && !(a > b); }

    // Verifies CAT-FIR-DESIGN-001.
    TEST(fir_design, ALowpassIsUnityAtDcAndHalfAtItsCutoff) {
        const auto d = *design<double, 63>(fir_lowpass{0.1, 60.0});
        EXPECT_NEAR(gain(d, 0.0), 1.0, 1e-14);
        EXPECT_NEAR(gain(d, 0.1), 0.5, 1e-3);
        // Kaiser's fit is approximate: from three quarters of the estimate's transition past the
        // edge the stopband is below the attenuation; from half of it, within 5 dB.
        const bands b = measure(d, 0.1, 0.1, true, false, false, 60.0, 0.75);
        EXPECT_LT(db(b.worst_stop), -60.0);
        EXPECT_LT(b.worst_pass, 2e-3);
        EXPECT_LT(db(measure(d, 0.1, 0.1, true, false, false, 60.0, 0.5).worst_stop), -55.0);
        // Longer, deeper and shallower, an even count.
        const auto long_one = *design<double, 255>(fir_lowpass{0.05, 80.0});
        EXPECT_NEAR(gain(long_one, 0.0), 1.0, 1e-14);
        EXPECT_NEAR(gain(long_one, 0.05), 0.5, 1e-3);
        EXPECT_LT(db(measure(long_one, 0.05, 0.05, true, false, false, 80.0, 0.75).worst_stop), -80.0);
        const auto short_one = *design<double, 31>(fir_lowpass{0.25, 40.0});
        EXPECT_NEAR(gain(short_one, 0.25), 0.5, 2e-3);
        EXPECT_LT(db(measure(short_one, 0.25, 0.25, true, false, false, 40.0, 0.75).worst_stop), -40.0);
        const auto even = *design<double, 64>(fir_lowpass{0.1, 60.0});
        EXPECT_NEAR(gain(even, 0.0), 1.0, 1e-14);
        EXPECT_LT(gain(even, 0.5), 1e-14); // an even count's zero at Nyquist
        EXPECT_LT(db(measure(even, 0.1, 0.1, true, false, false, 60.0, 0.75).worst_stop), -60.0);
    }

    // Verifies CAT-FIR-DESIGN-001.
    TEST(fir_design, AHighpassIsTheLowpassesComplement) {
        const auto hp = *design<double, 63>(fir_highpass{0.2, 60.0});
        const auto lp = *design<double, 63>(fir_lowpass{0.2, 60.0});
        for (std::size_t k = 0; k < 63; ++k) {
            const double delta = k == 31 ? 1.0 : 0.0;
            EXPECT_NEAR(hp.table()[k], delta - lp.table()[k], 1e-16) << k;
        }
        EXPECT_LT(gain(hp, 0.0), 1e-14);
        EXPECT_NEAR(gain(hp, 0.5), 1.0, 2e-3);
        EXPECT_NEAR(gain(hp, 0.2), 0.5, 1e-3);
        const bands b = measure(hp, 0.2, 0.2, false, false, true, 60.0, 0.75);
        EXPECT_LT(db(b.worst_stop), -60.0);
        EXPECT_LT(b.worst_pass, 2e-3);
    }

    // Verifies CAT-FIR-DESIGN-001.
    TEST(fir_design, TheBandsSumTwoLowpassesRipples) {
        // A band-pass is LP(high) - LP(low), so its stopbands carry both ripples: within 6 dB of
        // the attenuation at the battery's designs, and so does the band-stop, delta - BP.
        const auto bp = *design<double, 64>(fir_bandpass{0.1, 0.3, 60.0});
        EXPECT_LT(gain(bp, 0.0), 1e-14);
        EXPECT_LT(gain(bp, 0.5), 1e-14);
        EXPECT_NEAR(gain(bp, 0.2), 1.0, 2e-3);
        EXPECT_NEAR(gain(bp, 0.1), 0.5, 1e-3);
        EXPECT_NEAR(gain(bp, 0.3), 0.5, 1e-3);
        const bands pass = measure(bp, 0.1, 0.3, false, true, false, 60.0, 0.75);
        EXPECT_LT(db(pass.worst_stop), -54.0);
        EXPECT_LT(pass.worst_pass, 2e-3);
        const auto bs = *design<double, 63>(fir_bandstop{0.1, 0.3, 60.0});
        EXPECT_NEAR(gain(bs, 0.0), 1.0, 1e-14);
        EXPECT_NEAR(gain(bs, 0.5), 1.0, 2e-3);
        EXPECT_NEAR(gain(bs, 0.1), 0.5, 1e-3);
        const bands stop = measure(bs, 0.1, 0.3, true, false, true, 60.0, 0.75);
        EXPECT_LT(db(stop.worst_stop), -54.0);
        EXPECT_LT(stop.worst_pass, 2e-3);
    }

    template <typename S, std::size_t Taps, typename Spec>
    void symmetric(const Spec& spec) {
        const auto d = design<S, Taps>(spec);
        ASSERT_TRUE(d.has_value());
        for (std::size_t k = 0; k < Taps; ++k) {
            ASSERT_EQ(std::memcmp(&d->table()[k], &d->table()[Taps - 1 - k], sizeof(d->table()[k])), 0) << k;
        }
    }

    template <typename S>
    void every_design_is_symmetric() {
        symmetric<S, 63>(fir_lowpass{0.1, 60.0});
        symmetric<S, 64>(fir_lowpass{0.13, 50.0});
        symmetric<S, 255>(fir_lowpass{0.05, 80.0});
        symmetric<S, 63>(fir_highpass{0.2, 60.0});
        symmetric<S, 64>(fir_bandpass{0.1, 0.3, 60.0});
        symmetric<S, 63>(fir_bandstop{0.1, 0.3, 60.0});
        symmetric<S, 1>(fir_lowpass{0.1, 60.0});
        symmetric<S, 2>(fir_lowpass{0.1, 60.0});
    }

    // Verifies CAT-FIR-DESIGN-002.
    TEST(fir_design, EveryDesignIsSymmetricBitForBit) {
        every_design_is_symmetric<float>();
        every_design_is_symmetric<double>();
        every_design_is_symmetric<catenae::q15>();
        every_design_is_symmetric<catenae::q31>();
        every_design_is_symmetric<catenae::q4_27>();
    }

    /// DspTap's quantize_row_preserving_sum at b26a7db, written out for the test: largest
    /// remainder, the lowest index on a tie, never a step past a rail.
    template <typename T>
    std::vector<typename catenae::sample_traits<T>::coeff> row_preserving(const std::vector<double>& src) {
        using traits = catenae::sample_traits<T>;
        using c      = typename traits::coeff;
        std::vector<c>      dst(src.size());
        std::vector<double> remainder(src.size());
        double              exact_sum = 0.0;
        std::int64_t        quant_sum = 0;
        for (std::size_t t = 0; t < src.size(); ++t) {
            const double scaled = src[t] * traits::coeff_scale;
            dst[t]              = traits::make_coeff(src[t]);
            remainder[t]        = scaled - static_cast<double>(dst[t]);
            exact_sum += scaled;
            quant_sum += dst[t];
        }
        std::int64_t residual = std::llround(exact_sum) - quant_sum;
        while (residual != 0) {
            const double sgn  = residual > 0 ? 1.0 : -1.0;
            const c      rail = residual > 0 ? std::numeric_limits<c>::max() : std::numeric_limits<c>::min();
            std::size_t  best = src.size();
            for (std::size_t u = 0; u < src.size(); ++u) {
                if (dst[u] != rail && (best == src.size() || sgn * remainder[u] > sgn * remainder[best])) {
                    best = u;
                }
            }
            if (best == src.size()) {
                break;
            }
            dst[best] = static_cast<c>(dst[best] + (residual > 0 ? 1 : -1));
            remainder[best] -= sgn;
            residual -= residual > 0 ? 1 : -1;
        }
        return dst;
    }

    template <typename S, std::size_t Taps, typename Spec>
    void quantized_as_the_card_says(const Spec& spec) {
        using traits = catenae::sample_traits<catenae::storage_t<S>>;
        const auto exact = *design<double, Taps>(spec);
        const auto fixed = *design<S, Taps>(spec);
        // The pairs: the row-sum-preserving quantization of the first half.
        std::vector<double> half(Taps / 2);
        double              whole = 0.0;
        for (std::size_t k = 0; k < Taps / 2; ++k) {
            half[k] = exact.table()[k];
            whole += half[k] * traits::coeff_scale;
        }
        const auto  pairs     = row_preserving<catenae::storage_t<S>>(half);
        std::int64_t pair_sum = 0;
        for (std::size_t k = 0; k < Taps / 2; ++k) {
            ASSERT_EQ(fixed.table()[k], pairs[k]) << Taps << " taps, tap " << k;
            pair_sum += pairs[k];
        }
        std::int64_t sum = 0;
        for (const auto c : fixed.table()) {
            sum += c;
        }
        whole *= 2.0;
        if (Taps % 2 == 1) {
            whole += exact.table()[Taps / 2] * traits::coeff_scale;
            EXPECT_EQ(sum, std::llround(whole)) << Taps << " taps: the centre takes the residual";
            EXPECT_EQ(fixed.table()[Taps / 2], std::llround(whole) - 2 * pair_sum);
            // and it stays within an LSB and a half of its own value
            EXPECT_LE(std::fabs(static_cast<double>(fixed.table()[Taps / 2])
                                - exact.table()[Taps / 2] * traits::coeff_scale),
                      1.5);
        }
        else {
            EXPECT_LE(std::fabs(static_cast<double>(sum) - whole), 1.0) << Taps << " taps";
        }
    }

    // Verifies CAT-FIR-DESIGN-002.
    TEST(fir_design, FixedTapsAreTheRowSumPreservingQuantization) {
        quantized_as_the_card_says<catenae::q15, 63>(fir_lowpass{0.1, 60.0});
        quantized_as_the_card_says<catenae::q15, 64>(fir_lowpass{0.13, 50.0});
        quantized_as_the_card_says<catenae::q15, 255>(fir_lowpass{0.05, 80.0});
        quantized_as_the_card_says<catenae::q15, 63>(fir_bandstop{0.1, 0.3, 60.0});
        quantized_as_the_card_says<catenae::q31, 63>(fir_highpass{0.2, 60.0});
        quantized_as_the_card_says<catenae::q31, 64>(fir_bandpass{0.1, 0.3, 60.0});
        quantized_as_the_card_says<catenae::q31, 255>(fir_lowpass{0.05, 80.0});
        quantized_as_the_card_says<catenae::q4_27, 63>(fir_lowpass{0.1, 60.0});
        // A low-pass's taps sum to unity exactly at an odd count.
        std::int64_t q15_sum = 0;
        const auto   q15_lp  = *design<catenae::q15, 63>(fir_lowpass{0.1, 60.0});
        for (const auto c : q15_lp.table()) {
            q15_sum += c;
        }
        EXPECT_EQ(q15_sum, 16384);
        // float and double: each tap the double design's, rounded once.
        const auto exact = *design<double, 63>(fir_lowpass{0.1, 60.0});
        const auto f     = *design<float, 63>(fir_lowpass{0.1, 60.0});
        for (std::size_t k = 0; k < 63; ++k) {
            EXPECT_EQ(f.table()[k], static_cast<float>(exact.table()[k])) << k;
        }
    }

    template <typename Spec>
    status rejected(const Spec& spec) {
        const auto d = catenae::fir_design<float, 63>::make(spec);
        return d.has_value() ? status{} : d.error();
    }

    // Verifies CAT-FIR-DESIGN-002.
    TEST(fir_design, MakeRejectsWhatItsCardNames) {
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        constexpr double inf = std::numeric_limits<double>::infinity();
        EXPECT_EQ(rejected(fir_lowpass{nan, 60.0}), status::invalid_argument);
        EXPECT_EQ(rejected(fir_lowpass{0.1, inf}), status::invalid_argument);
        EXPECT_EQ(rejected(fir_highpass{-inf, 60.0}), status::invalid_argument);
        EXPECT_EQ(rejected(fir_bandpass{0.1, nan, 60.0}), status::invalid_argument);
        EXPECT_EQ(rejected(fir_bandpass{0.3, 0.1, 60.0}), status::invalid_argument);
        EXPECT_EQ(rejected(fir_bandstop{0.2, 0.2, 60.0}), status::invalid_argument);
        EXPECT_EQ(rejected(fir_lowpass{0.0, 60.0}), status::out_of_range);
        EXPECT_EQ(rejected(fir_lowpass{0.5, 60.0}), status::out_of_range);
        EXPECT_EQ(rejected(fir_lowpass{-0.1, 60.0}), status::out_of_range);
        EXPECT_EQ(rejected(fir_bandpass{0.0, 0.3, 60.0}), status::out_of_range);
        EXPECT_EQ(rejected(fir_bandstop{0.1, 0.5, 60.0}), status::out_of_range);
        EXPECT_EQ(rejected(fir_lowpass{0.1, -1.0}), status::out_of_range);
        EXPECT_EQ(rejected(fir_lowpass{0.1, 200.5}), status::out_of_range);
        // The bounds' insides, and a window of beta 0 (a rectangle) at 0 dB.
        using d63 = catenae::fir_design<float, 63>;
        EXPECT_TRUE(d63::make(fir_lowpass{1e-9, 0.0}).has_value());
        EXPECT_TRUE(d63::make(fir_lowpass{0.5 - 1e-9, 200.0}).has_value());
        EXPECT_TRUE(d63::make(fir_bandstop{1e-9, 0.5 - 1e-9, 21.0}).has_value());
    }

    template <typename D, typename = void>
    struct takes_a_temporary : std::false_type {};
    template <typename D>
    struct takes_a_temporary<D, std::void_t<decltype(D::from_table(std::declval<typename D::table_type>()))>>
        : std::true_type {};

    // Verifies CAT-FIR-DESIGN-003.
    TEST(fir_design, ALongDesignViewsTheCallersTable) {
        static_assert(catenae::fir_design<float, 128>::holds_by_value, "512 bytes of taps");
        static_assert(!catenae::fir_design<float, 129>::holds_by_value, "516");
        static_assert(catenae::fir_design<catenae::q15, 256>::holds_by_value, "512");
        static_assert(!catenae::fir_design<catenae::q15, 257>::holds_by_value, "514");
        static_assert(catenae::fir_design<double, 64>::holds_by_value, "512");
        static_assert(!catenae::fir_design<double, 65>::holds_by_value, "520");
        static_assert(catenae::fir_any_design<float, 126>::holds_by_value, "8 bytes of latency and 504 of taps");
        static_assert(!catenae::fir_any_design<float, 127>::holds_by_value, "516");
        static_assert(sizeof(catenae::fir_design<float, 128>) == 512, "a linear design stores its taps alone");
        static_assert(sizeof(catenae::fir_design<float, 129>) == sizeof(void*), "a view");
        static_assert(catenae::fir_design<float, 300>::workspace_size == 150, "(Taps + 1) / 2");
        static_assert(catenae::fir_design<float, 301>::workspace_size == 151, "(Taps + 1) / 2");
        static_assert(takes_a_temporary<catenae::fir_design<float, 128>>::value, "a value is copied");
        static_assert(!takes_a_temporary<catenae::fir_design<float, 129>>::value, "a view refuses a temporary");
        static_assert(!catenae::holds_view<catenae::fir<2, 128>>::value, "by value");
        static_assert(catenae::holds_view<catenae::fir<2, 129>>::value, "a view");
        static_assert(catenae::holds_view<catenae::fir_q15<1, 257>>::value, "a view");

        using d301 = catenae::fir_design<catenae::q15, 301>;
        static std::array<std::int16_t, 301> table{};
        static std::array<double, 151>       workspace{};
        const auto d = d301::make(fir_lowpass{0.05, 80.0}, catenae::span<std::int16_t, 301>(table),
                                  catenae::span<double, 151>(workspace));
        ASSERT_TRUE(d.has_value());
        EXPECT_EQ(d->data(), table.data());
        EXPECT_EQ(d->table().data(), table.data());
        std::int64_t sum = 0;
        for (const std::int16_t c : table) {
            sum += c;
        }
        EXPECT_EQ(sum, 16384);
        EXPECT_FALSE(d301::make(fir_lowpass{0.6, 80.0}, catenae::span<std::int16_t, 301>(table),
                                catenae::span<double, 151>(workspace))
                         .has_value());
    }

    constexpr std::array<float, 5> k_symmetric     = {0.1f, -0.25f, 0.5f, -0.25f, 0.1f};
    constexpr std::array<float, 4> k_antisymmetric = {0.25f, -0.5f, 0.5f, -0.25f};
    constexpr std::array<float, 3> k_minimum_phase = {0.5f, 0.375f, 0.125f};

    // Verifies CAT-FIR-DESIGN-004.
    TEST(fir_design, FromTableAcceptsWhatItsCardAdmits) {
        constexpr auto sym  = catenae::fir_design<float, 5>::from_table(k_symmetric);
        constexpr auto anti = catenae::fir_design<float, 4>::from_table(k_antisymmetric);
        static_assert(same(sym.table()[1], -0.25f) && same(anti.table()[3], -0.25f), "the table's values");
        static_assert(same(sym.latency().nominal, 2.0f) && same(anti.latency().nominal, 1.5f), "(Taps - 1) / 2");
        constexpr auto any = catenae::fir_any_design<float, 3>::from_table(k_minimum_phase, {0.75f, 0.5f});
        static_assert(same(any.latency().nominal, 0.75f) && same(any.latency().deviation, 0.5f), "the generator's");
        constexpr std::array<std::int16_t, 3> q = {-32768, 0, -32768};
        static_cast<void>(catenae::fir_design<catenae::q15, 3>::from_table(q));
        // A zero centre is its own negation: antisymmetric at an odd count too.
        constexpr std::array<double, 3> hilbert_like = {0.5, 0.0, -0.5};
        static_cast<void>(catenae::fir_design<double, 3>::from_table(hilbert_like));
    }

    // Verifies CAT-FIR-DESIGN-004.
    TEST(fir_design, FromTableRefusesTheRestAtRunTime) {
        using d3 = catenae::fir_design<float, 3>;
        std::array<float, 3> t = {0.25f, 0.5f, 0.26f};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d3::from_table(t)));
        t = {0.25f, std::numeric_limits<float>::quiet_NaN(), 0.25f};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d3::from_table(t)));
        t = {std::numeric_limits<float>::infinity(), 0.5f, std::numeric_limits<float>::infinity()};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d3::from_table(t)));
        t = {0.25f, 0.1f, -0.25f}; // antisymmetric but for a centre that is not zero
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d3::from_table(t)));
        // In q15, -32768 has no negation: {-32768, 0, 32767} is neither.
        std::array<std::int16_t, 3> q = {-32768, 0, 32767};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(catenae::fir_design<catenae::q15, 3>::from_table(q)));
        // Any FIR: its taps finite, its latency finite and not negative.
        using any3                = catenae::fir_any_design<float, 3>;
        std::array<float, 3> free = {0.5f, 0.375f, 0.125f};
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(any3::from_table(free, {0.75f, 0.0f})));
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(any3::from_table(free, {-0.5f, 0.0f})));
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(any3::from_table(free, {0.75f, std::numeric_limits<float>::infinity()})));
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(any3::from_table(free, {std::numeric_limits<float>::quiet_NaN(), 0.0f})));
        free[2] = std::numeric_limits<float>::quiet_NaN();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(any3::from_table(free, {0.75f, 0.0f})));
    }

    // Verifies CAT-FIR-DESIGN-005.
    TEST(fir_design, TheTapsEstimateIsTheCarriedFormula) {
        static_assert(catenae::fir_taps_estimate(60.0, 0.05) == 73, "52 / (2.285 2 pi 0.05) = 72.4, rounded up");
        static_assert(catenae::fir_taps_estimate(80.0, 0.0197) == 255, "the battery's long low-pass");
        static_assert(catenae::fir_taps_estimate(8.0, 0.1) == 4, "at least 4");
        static_assert(catenae::fir_taps_estimate(60.0, 0.0) == 4, "a transition that is not positive");
        static_assert(catenae::fir_taps_estimate(60.0, std::numeric_limits<double>::quiet_NaN()) == 4, "NaN");
        static_assert(catenae::fir_taps_estimate(200.0, 1e-12) == 2147483648u, "at most 2^31");
        for (const double a : {21.0, 40.0, 60.0, 96.0, 120.0}) {
            for (const double df : {0.01, 0.05, 0.1}) {
                const double n = (a - 8.0) / (2.285 * 2.0 * k_pi * df);
                EXPECT_EQ(catenae::fir_taps_estimate(a, df), static_cast<std::size_t>(std::ceil(n))) << a << " " << df;
            }
        }
    }

} // namespace
