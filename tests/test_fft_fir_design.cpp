/// @file test_fft_fir_design.cpp
/// @brief fft_fir_design's contract (include/catenae/fft_fir_design.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/fft.h"
#include "catenae/fft_fir_design.h"
#include "catenae/fir_design.h"
#include "catenae_test/check_trap.h"
#include "fft_fir_tables.h"

namespace {

    template <typename T>
    class fft_fir_design_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(fft_fir_design_formats, formats, );

    using catenae::fir_lowpass;
    using catenae::fir_phase;

    /// An FIR design by whichever make() its size takes, into storage of its own.
    template <typename S, std::size_t Taps, fir_phase Phase = fir_phase::linear>
    catenae::fir_design<S, Taps, Phase> taps_of(const fir_lowpass& spec) {
        using d = catenae::fir_design<S, Taps, Phase>;
        if constexpr (d::holds_by_value) {
            const auto made = d::make(spec);
            EXPECT_TRUE(made.has_value());
            return *made;
        }
        else {
            static std::array<typename d::coefficient_type, Taps> table{};
            static std::array<double, d::workspace_size>          workspace{};
            const auto made = d::make(spec, catenae::span<typename d::coefficient_type, Taps>(table),
                                      catenae::span<double, d::workspace_size>(workspace));
            EXPECT_TRUE(made.has_value());
            return *made;
        }
    }

    /// The storage one spectrum is made into, and the design that views it.
    template <typename D>
    struct spectrum {
        std::vector<typename D::bin_type>     table = std::vector<typename D::bin_type>(D::bins);
        std::vector<catenae::complex<double>> work  = std::vector<catenae::complex<double>>(D::workspace_size);

        catenae::expected<D, catenae::status> make(const typename D::taps_type& taps) {
            return D::make(taps, catenae::span<typename D::bin_type, D::bins>(table.data(), D::bins),
                           catenae::span<catenae::complex<double>, D::workspace_size>(work.data(), D::workspace_size));
        }
    };

    /// x in V, rounded once where V is narrower.
    template <typename V>
    V as(double x) {
        if constexpr (std::is_same<V, double>::value) {
            return x;
        }
        else {
            return static_cast<V>(x);
        }
    }

    /// Whether two values are one, bit for bit.
    template <typename V>
    bool same_bits(V a, V b) {
        return std::memcmp(&a, &b, sizeof a) == 0;
    }

    // Verifies CAT-FFT-FIR-DESIGN-001.
    TYPED_TEST(fft_fir_design_formats, TheSpectrumIsTheTapsForwardTimesTwoOverN) {
        using v                    = catenae::storage_t<TypeParam>;
        constexpr std::size_t taps = 63;
        using d                    = catenae::fft_fir_design<TypeParam, taps, 64>;
        const auto  h              = taps_of<TypeParam, taps>(fir_lowpass{0.1, 60.0});
        spectrum<d> store;
        const auto  made = store.make(h);
        ASSERT_TRUE(made.has_value());
        // The same in the test: the taps widened, zero-padded to 128, the double forward, 1/64.
        std::array<catenae::complex<double>, d::bins> want{};
        for (std::size_t k = 0; k < taps; ++k) {
            (k % 2 == 0 ? want[k / 2].re : want[k / 2].im) = static_cast<double>(h.table()[k]);
        }
        catenae::fft_plan<double>::of<128>().forward(want);
        for (std::size_t k = 0; k < d::bins; ++k) {
            EXPECT_TRUE(same_bits(made->table()[k].re, as<v>(want[k].re / 64.0))) << "bin " << k;
            EXPECT_TRUE(same_bits(made->table()[k].im, as<v>(want[k].im / 64.0))) << "bin " << k;
        }
        // DC and Nyquist are real; the latency is the taps design's.
        EXPECT_TRUE(same_bits(made->table()[0].im, v{0}));
        EXPECT_TRUE(same_bits(made->table()[64].im, v{0}));
        EXPECT_FLOAT_EQ(made->latency().nominal, 31.0f);
        EXPECT_FLOAT_EQ(made->latency().deviation, 0.0f);
        // An FIR of any taps keeps its design's latency.
        using any_d        = catenae::fft_fir_any_design<TypeParam, taps, 64>;
        const auto      ha = taps_of<TypeParam, taps, fir_phase::any>(fir_lowpass{0.1, 60.0});
        spectrum<any_d> any_store;
        const auto      any = any_store.make(ha);
        ASSERT_TRUE(any.has_value());
        EXPECT_FLOAT_EQ(any->latency().nominal, ha.latency().nominal);
        EXPECT_TRUE(catenae::holds_view<d>::value);
    }

    // Verifies CAT-FFT-FIR-DESIGN-001.
    TEST(fft_fir_design, MakeRefusesASpectrumThatDoesNotFit) {
        // 17 taps at FLT_MAX in blocks of 16: DC is 17/16 of FLT_MAX, past float.
        using d = catenae::fft_fir_design<float, 17, 16>;
        std::array<float, 17> big{};
        big.fill(std::numeric_limits<float>::max());
        const auto  h = catenae::fir_design<float, 17>::from_table(big);
        spectrum<d> store;
        const auto  made = store.make(h);
        ASSERT_FALSE(made.has_value());
        EXPECT_EQ(made.error(), catenae::status::out_of_range);
    }

    // Verifies CAT-FFT-FIR-DESIGN-001.
    TEST(fft_fir_design, ATableDesignIsWhatMakeGives) {
        // examples/fft_fir_tables.h, constants the generator printed from make() on the hosted
        // Linux leg: bit for bit there, and within a float ulp a tap, so within 2^-21 of the
        // largest bin, wherever another libm rounds the double Kaiser design otherwise.
        const auto h = taps_of<float, 255>(fir_lowpass{0.1, 80.0});
        for (std::size_t k = 0; k < 255; ++k) {
            const float t = example::long_lowpass.table()[k];
            const float m = h.table()[k];
            EXPECT_TRUE(t >= std::nextafter(m, -1.0f) && t <= std::nextafter(m, 1.0f)) << "tap " << k;
        }
        using d = catenae::fft_fir_design<float, 255, 256>;
        spectrum<d> store;
        const auto  made = store.make(h);
        ASSERT_TRUE(made.has_value());
        float largest = 0.0f;
        for (const catenae::complex<float>& b : example::long_lowpass_fft.table()) {
            largest = std::max({largest, std::fabs(b.re), std::fabs(b.im)});
        }
        const float within = largest * 0x1p-21f;
        for (std::size_t k = 0; k < d::bins; ++k) {
            EXPECT_LE(std::fabs(example::long_lowpass_fft.table()[k].re - made->table()[k].re), within) << "bin " << k;
            EXPECT_LE(std::fabs(example::long_lowpass_fft.table()[k].im - made->table()[k].im), within) << "bin " << k;
        }
        EXPECT_FLOAT_EQ(example::long_lowpass_fft.latency().nominal, 127.0f);
    }

    // Verifies CAT-FFT-FIR-DESIGN-002.
    TEST(fft_fir_design, FromTableAcceptsItsOwnBitForBit) {
        using d            = catenae::fft_fir_design<float, 255, 256>;
        const d     again  = d::from_table(example::long_lowpass_spectrum);
        constexpr d in_rom = d::from_table(example::long_lowpass_spectrum); // a constant: checked as it compiles
        EXPECT_EQ(again.data(), example::long_lowpass_spectrum.data());     // a view, not a copy
        EXPECT_EQ(in_rom.data(), example::long_lowpass_spectrum.data());
        static_assert(sizeof(d) == sizeof(void*), "a linear-phase design is its view alone");
    }

    // Verifies CAT-FFT-FIR-DESIGN-002.
    TEST(fft_fir_design, FromTableRefusesWhatIsNoRealFiltersSpectrum) {
        using d  = catenae::fft_fir_design<float, 3, 2>;
        using ad = catenae::fft_fir_any_design<float, 3, 2>;
        static std::array<catenae::complex<float>, 3> t{};
        t = {{{0.5f, 0.0f}, {0.25f, -0.25f}, {0.0f, 0.0f}}};
        EXPECT_FLOAT_EQ(d::from_table(t).table()[1].im, -0.25f); // a real filter's: accepted
        t[0].im = 1e-30f;                                        // DC is not real
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(t)));
        t[0].im = 0.0f;
        t[2].im = -1e-30f; // nor Nyquist
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(t)));
        t[2].im = 0.0f;
        t[1].re = std::numeric_limits<float>::quiet_NaN(); // a part not finite
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(t)));
        t[1].re = std::numeric_limits<float>::infinity();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(d::from_table(t)));
        t[1].re = 0.25f;
        EXPECT_FLOAT_EQ(ad::from_table(t, {1.0f, 0.5f}).latency().deviation, 0.5f);
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(ad::from_table(t, {-1.0f, 0.0f})));
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected,
                             static_cast<void>(ad::from_table(t, {std::numeric_limits<float>::infinity(), 0.0f})));
    }

    // Verifies CAT-FFT-FIR-DESIGN-003.
    TEST(fft_fir_design, TheBlockIsAPowerOfTwoAtLeastTapsLessOne) {
        static_assert(catenae::fft_fir_block_for(1) == 16 && catenae::fft_fir_block_for(17) == 16, "at least 16");
        static_assert(catenae::fft_fir_block_for(18) == 32 && catenae::fft_fir_block_for(255) == 256, "taps - 1");
        static_assert(catenae::fft_fir_block_for(257) == 256 && catenae::fft_fir_block_for(258) == 512, "or more");
        static_assert(catenae::detail::fft_fir_block_ok(3, 2) && catenae::detail::fft_fir_block_ok(32769, 32768),
                      "2 to 32,768");
        static_assert(!catenae::detail::fft_fir_block_ok(4, 2) && !catenae::detail::fft_fir_block_ok(1, 1)
                          && !catenae::detail::fft_fir_block_ok(1, 48) && !catenae::detail::fft_fir_block_ok(1, 65536),
                      "a power of two from 2 to 32,768, at least taps - 1");
        static_assert(catenae::max_fft_fir_block == 32768, "the longest transform with tables, halved");
        using d = catenae::fft_fir_design<double, 33, 32>;
        static_assert(d::length == 64 && d::bins == 33 && d::workspace_size == 33, "N = 2 Block, Block + 1 bins");
        SUCCEED();
    }

} // namespace
