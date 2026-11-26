/// @file test_fft.cpp
/// @brief The real FFT's contract (include/catenae/fft.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/fft.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "fft_fingerprints.h"

// The fingerprints assume every float and double operation rounds to its own format.
#if defined(FLT_EVAL_METHOD)
static_assert(FLT_EVAL_METHOD == 0, "catenae: evaluation in wider precision (x87) moves the FFT's output bits");
#endif

// The longest length the battery transforms: 65536 hosted, 4096 on the bare-metal legs, whose
// RAM holds no 65536-point double frame.
#ifndef CATENAE_TEST_MAX_FFT_N
#define CATENAE_TEST_MAX_FFT_N 65536
#endif

namespace {

    namespace pins = catenae_test::fft_fingerprints;

    constexpr std::size_t max_n = CATENAE_TEST_MAX_FFT_N;

    /// The fixed input: 24-bit integers from xorshift32 scaled by 2^-23, exact in both formats.
    template <typename T>
    std::vector<T> noise(std::size_t n, std::uint32_t seed) {
        std::vector<T> x(n);
        std::uint32_t  s = seed;
        for (T& v : x) {
            s ^= s << 13u;
            s ^= s >> 17u;
            s ^= s << 5u;
            const auto word = static_cast<std::int32_t>(s);
            v               = static_cast<T>(word / 256) * static_cast<T>(0x1p-23);
        }
        return x;
    }

    /// The samples as the forward reads them: N/2 + 1 entries, {x[2n], x[2n + 1]} in the first N/2.
    template <typename T>
    std::vector<catenae::complex<T>> frame_of(const std::vector<T>& x) {
        std::vector<catenae::complex<T>> a(x.size() / 2u + 1u);
        for (std::size_t i = 0; i < x.size() / 2u; ++i) {
            a[i] = {x[2u * i], x[2u * i + 1u]};
        }
        return a;
    }

    template <typename T>
    catenae::fft_plan<T> plan_of(std::size_t n) {
        // One plan per length, from the constant tables, through a switch over the lengths.
        switch (n) {
#define CATENAE_TEST_CASE(len)                                                                                         \
    case len:                                                                                                          \
        return catenae::fft_plan<T>::template of<len>();
            CATENAE_FFT_LENGTHS(CATENAE_TEST_CASE)
#undef CATENAE_TEST_CASE
        default:
            break;
        }
        ADD_FAILURE() << "no length " << n;
        return catenae::fft_plan<T>::template of<4>();
    }

    /// A DFT in double of real x, each product exact (fma) and the sums compensated (Neumaier),
    /// with exp(-i) twiddles from the exact angle 2 pi (k n mod N) / N: within an ulp or two.
    std::vector<catenae::complex<double>> reference_dft(const std::vector<double>& x) {
        const std::size_t                     n = x.size();
        std::vector<catenae::complex<double>> out(n / 2u + 1u);
        const double                          two_pi = 6.283185307179586476925286766559;
        for (std::size_t k = 0; k <= n / 2u; ++k) {
            double sum[2]  = {0.0, 0.0};
            double comp[2] = {0.0, 0.0};
            for (std::size_t j = 0; j < n; ++j) {
                const double angle = two_pi * static_cast<double>((k * j) % n) / static_cast<double>(n);
                const double w[2]  = {std::cos(angle), -std::sin(angle)};
                for (std::size_t part = 0; part < 2u; ++part) {
                    const double p  = x[j] * w[part];
                    const double pe = std::fma(x[j], w[part], -p); // the product's rounding error
                    for (const double term : {p, pe}) {
                        const double t = sum[part] + term;
                        comp[part] +=
                            std::fabs(sum[part]) >= std::fabs(term) ? (sum[part] - t) + term : (term - t) + sum[part];
                        sum[part] = t;
                    }
                }
            }
            out[k] = {sum[0] + comp[0], sum[1] + comp[1]};
        }
        return out;
    }

    /// The relative 2-norm distance of a from b.
    template <typename A, typename B>
    double relative_error(const std::vector<catenae::complex<A>>& a, const std::vector<catenae::complex<B>>& b,
                          std::size_t count) {
        double num = 0.0;
        double den = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double dr = static_cast<double>(a[i].re) - static_cast<double>(b[i].re);
            const double di = static_cast<double>(a[i].im) - static_cast<double>(b[i].im);
            num += dr * dr + di * di;
            den += static_cast<double>(b[i].re) * static_cast<double>(b[i].re)
                   + static_cast<double>(b[i].im) * static_cast<double>(b[i].im);
        }
        return std::sqrt(num / den);
    }

    template <typename T>
    class fft_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(fft_formats, formats, ); // the empty name generator: Clang asks for the third argument

    // Verifies CAT-FFT-001.
    TYPED_TEST(fft_formats, ForwardIsTheExpMinusDftWithEveryBinStored) {
        using T = TypeParam;
        for (std::size_t n = 4; n <= 256; n *= 2u) {
            const std::vector<T> x = noise<T>(n, 7u);
            auto                 a = frame_of(x);
            plan_of<T>(n).forward(catenae::span<catenae::complex<T>>(a.data(), a.size()));
            std::vector<double> xd(x.begin(), x.end());
            const auto          ref = reference_dft(xd);
            // The floors, measured on x86-64 (printed below) and pinned at twice the worst.
            const double err = relative_error(a, ref, a.size());
            std::printf("[ floor ] %s N=%u forward vs DFT %.3g\n", std::is_same<T, float>::value ? "float" : "double",
                        static_cast<unsigned>(n), err);
            EXPECT_LT(err, (std::is_same<T, float>::value ? 2.0e-7 : 6.1e-16)) << n;
            // DC and Nyquist are real: their imaginary parts are written as exact zeros.
            EXPECT_EQ(std::fpclassify(a[0].im), FP_ZERO) << n;
            EXPECT_EQ(std::fpclassify(a[n / 2u].im), FP_ZERO) << n;
        }
    }

    // Verifies CAT-FFT-001.
    TYPED_TEST(fft_formats, ATonesBinsAreWhereExpMinusPutsThem) {
        using T                 = TypeParam;
        constexpr std::size_t n = 64;
        // x[j] = sin(2 pi 5 j / 64): X[5] = -i N/2 under exp(-i), +i N/2 under exp(+i).
        std::vector<T> x(n);
        for (std::size_t j = 0; j < n; ++j) {
            x[j] = static_cast<T>(std::sin(6.283185307179586 * 5.0 * static_cast<double>(j) / 64.0));
        }
        auto a = frame_of(x);
        catenae::fft_plan<T>::template of<n>().forward(catenae::span<catenae::complex<T>>(a.data(), a.size()));
        EXPECT_NEAR(static_cast<double>(a[5].im), -32.0, 1e-4);
        EXPECT_NEAR(static_cast<double>(a[5].re), 0.0, 1e-4);
        for (std::size_t k = 0; k <= n / 2u; ++k) {
            if (k != 5u) {
                EXPECT_NEAR(static_cast<double>(a[k].re), 0.0, 1e-4) << k;
                EXPECT_NEAR(static_cast<double>(a[k].im), 0.0, 1e-4) << k;
            }
        }
    }

    // Verifies CAT-FFT-002.
    TYPED_TEST(fft_formats, InverseReturnsItsExponentAndRoundTrips) {
        using T = TypeParam;
        for (std::size_t n = 4; n <= max_n; n *= 2u) {
            const std::vector<T> x    = noise<T>(n, 11u);
            auto                 a    = frame_of(x);
            const auto           plan = plan_of<T>(n);
            plan.forward(catenae::span<catenae::complex<T>>(a.data(), a.size()));
            const int e = plan.inverse(catenae::span<catenae::complex<T>>(a.data(), a.size()));
            ASSERT_EQ(e, 1) << n;
            // frame x 2^e = N x, so x = frame x 2^e / N.
            const double scale = std::ldexp(1.0, e) / static_cast<double>(n);
            double       worst = 0.0;
            for (std::size_t i = 0; i < n / 2u; ++i) {
                worst =
                    std::fmax(worst, std::fabs(static_cast<double>(a[i].re) * scale - static_cast<double>(x[2u * i])));
                worst = std::fmax(
                    worst, std::fabs(static_cast<double>(a[i].im) * scale - static_cast<double>(x[2u * i + 1u])));
            }
            EXPECT_LT(worst, (std::is_same<T, float>::value ? 1.0e-6 : 2.0e-15)) << n;
        }
    }

    // Verifies CAT-FFT-002.
    TYPED_TEST(fft_formats, InverseIsTheUnnormalizedExpPlusSum) {
        using T                 = TypeParam;
        constexpr std::size_t n = 32;
        // One bin: X[3] = 1 - 2i (and its conjugate at 29, implied). The inverse is
        // 2 Re((1 - 2i) exp(+i 2 pi 3 j / 32)) = 2 cos(theta) + 4 sin(theta).
        std::vector<catenae::complex<T>> a(n / 2u + 1u, catenae::complex<T>{T(0), T(0)});
        a[3] = {T(1), T(-2)};
        const int e =
            catenae::fft_plan<T>::template of<n>().inverse(catenae::span<catenae::complex<T>>(a.data(), a.size()));
        for (std::size_t j = 0; j < n; ++j) {
            const double theta = 6.283185307179586 * 3.0 * static_cast<double>(j) / 32.0;
            const double want  = 2.0 * std::cos(theta) + 4.0 * std::sin(theta);
            const auto&  z     = a[j / 2u];
            const double got   = static_cast<double>(j % 2u == 0u ? z.re : z.im) * std::ldexp(1.0, e);
            EXPECT_NEAR(got, want, 1e-5) << j;
        }
    }

    // Verifies CAT-FFT-001.
    TEST(fft_floor, FloatTracksDouble) {
        constexpr std::size_t n  = 512;
        const auto            xf = noise<float>(n, 3u);
        const auto            xd = noise<double>(n, 3u);
        auto                  af = frame_of(xf);
        auto                  ad = frame_of(xd);
        catenae::fft_plan<float>::of<n>().forward(catenae::span<catenae::complex<float>>(af.data(), af.size()));
        catenae::fft_plan<double>::of<n>().forward(catenae::span<catenae::complex<double>>(ad.data(), ad.size()));
        const double err = relative_error(af, ad, af.size());
        std::printf("[ floor ] float vs double N=512 forward %.3g\n", err);
        EXPECT_LT(err, 2.25e-7);
    }

    /// FNV-1a-64 over each value's bits, one 32- or 64-bit word per step.
    template <typename T>
    std::uint64_t fold(std::uint64_t h, const std::vector<catenae::complex<T>>& v, std::size_t count) {
        using bits = std::conditional_t<sizeof(T) == 4u, std::uint32_t, std::uint64_t>;
        for (std::size_t i = 0; i < count; ++i) {
            for (const T s : {v[i].re, v[i].im}) {
                bits b = 0;
                std::memcpy(&b, &s, sizeof b);
                h ^= b;
                h *= 0x100000001b3u;
            }
        }
        return h;
    }

    /// The forward's N/2 + 1 bins, then the inverse's N/2 entries, of the fixed input.
    template <typename T>
    std::uint64_t fingerprint(std::size_t n) {
        auto       a    = frame_of(noise<T>(n, pins::seed));
        const auto plan = plan_of<T>(n);
        plan.forward(catenae::span<catenae::complex<T>>(a.data(), a.size()));
        std::uint64_t h = fold(0xcbf29ce484222325u, a, a.size());
        static_cast<void>(plan.inverse(catenae::span<catenae::complex<T>>(a.data(), a.size())));
        return fold(h, a, n / 2u);
    }

    // Verifies CAT-FFT-003.
    TYPED_TEST(fft_formats, OutputBitsAreThePins) {
        using T                = TypeParam;
        const std::uint64_t* p = std::is_same<T, float>::value ? pins::float_pins : pins::double_pins;
        std::size_t          i = 0;
        for (std::size_t n = 4; n <= max_n; n *= 2u, ++i) {
            const std::uint64_t got = fingerprint<T>(n);
            // Plain unsigned: newlib's printf on the bare-metal legs parses neither %zu nor PRIx64.
            std::printf("[ fingerprint ] %s N=%u 0x%08x%08x\n", std::is_same<T, float>::value ? "float" : "double",
                        static_cast<unsigned>(n), static_cast<unsigned>(got >> 32u),
                        static_cast<unsigned>(got & 0xffffffffu));
            EXPECT_EQ(got, p[i]) << n;
        }
        EXPECT_GE(i, 11u) << "the sweep reaches N = 4096 on every leg";
    }

    // Verifies CAT-FFT-004.
    TEST(fft_lengths, APowerOfTwoFrom4To65536) {
        static_assert(catenae::fft_min_size == 4u && catenae::fft_max_size == 65536u, "");
        static_assert(catenae::fft_supports(4) && catenae::fft_supports(65536) && catenae::fft_supports(512), "");
        static_assert(!catenae::fft_supports(0) && !catenae::fft_supports(2) && !catenae::fft_supports(3), "");
        static_assert(!catenae::fft_supports(12) && !catenae::fft_supports(131072) && !catenae::fft_supports(65535),
                      "");
        static_assert(catenae::fft_bins(512) == 257u, "");
        std::vector<float> storage(catenae::fft_workspace_size<float>(65536));
        for (const std::size_t n : {0u, 1u, 2u, 3u, 6u, 12u, 100u, 131072u}) {
            const auto made = catenae::fft_plan<float>::make(n, catenae::span<float>(storage.data(), storage.size()));
            ASSERT_FALSE(made.has_value()) << n;
            EXPECT_EQ(made.error(), catenae::status::out_of_range) << n;
        }
        const auto small = catenae::fft_plan<float>::make(
            1024, catenae::span<float>(storage.data(), catenae::fft_workspace_size<float>(1024) - 1u));
        ASSERT_FALSE(small.has_value());
        EXPECT_EQ(small.error(), catenae::status::size_mismatch);
    }

    // Verifies CAT-FFT-005.
    TYPED_TEST(fft_formats, APlanMadeAtRunTimeHoldsTheConstantTables) {
        using T = TypeParam;
        for (std::size_t n = 4; n <= max_n; n *= 2u) {
            std::vector<T> storage(catenae::fft_workspace_size<T>(n));
            const auto     made = catenae::fft_plan<T>::make(n, catenae::span<T>(storage.data(), storage.size()));
            ASSERT_TRUE(made.has_value()) << n;
            EXPECT_EQ(made->size(), n);
            // The tables, bit for bit: the run-time plan's transform is the constant plan's.
            const auto x = noise<T>(n, 5u);
            auto       a = frame_of(x);
            auto       b = frame_of(x);
            made->forward(catenae::span<catenae::complex<T>>(a.data(), a.size()));
            plan_of<T>(n).forward(catenae::span<catenae::complex<T>>(b.data(), b.size()));
            EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof a[0]), 0) << n;
        }
        // And the table values themselves, at one length of each kind of table.
        std::vector<T> storage(catenae::fft_workspace_size<T>(1024));
        ASSERT_TRUE(catenae::fft_plan<T>::make(1024, catenae::span<T>(storage.data(), storage.size())).has_value());
        const auto& constant = catenae::fft_tables<T, 1024>::values;
        ASSERT_EQ(constant.size(), storage.size());
        EXPECT_EQ(std::memcmp(constant.data(), storage.data(), storage.size() * sizeof(T)), 0);
    }

    // Verifies CAT-FFT-006.
    TEST(fft_plan, IsATriviallyCopyableConstant) {
        static_assert(std::is_trivially_copyable<catenae::fft_plan<float>>::value, "");
        static_assert(std::is_trivially_copyable<catenae::fft_plan<double>>::value, "");
        constexpr auto plan = catenae::fft_plan<float>::of<256>();
        static_assert(plan.size() == 256u, "");
        // Two frames through one plan, interleaved call by call, give what each gives alone.
        const auto x = noise<float>(256, 9u);
        const auto y = noise<float>(256, 10u);
        auto       a = frame_of(x);
        auto       b = frame_of(y);
        auto       c = frame_of(x);
        plan.forward(catenae::span<catenae::complex<float>>(a.data(), a.size()));
        plan.forward(catenae::span<catenae::complex<float>>(b.data(), b.size()));
        const auto copy = plan;
        copy.forward(catenae::span<catenae::complex<float>>(c.data(), c.size()));
        EXPECT_EQ(std::memcmp(a.data(), c.data(), a.size() * sizeof a[0]), 0);
    }

    // Verifies CAT-FFT-007.
    TEST(fft_formats_fixed, TheForwardsGrowthIsAConstantOfTheLength) {
        static_assert(catenae::fft_forward_exponent<float>(512) == 0, "");
        static_assert(catenae::fft_forward_exponent<double>(65536) == 0, "");
        static_assert(catenae::fft_forward_exponent<catenae::q15>(512) == 9, "");
        static_assert(catenae::fft_forward_exponent<catenae::q31>(512) == 10, "");
        static_assert(catenae::fft_forward_exponent<catenae::q15>(4) == 2, "");
        static_assert(catenae::fft_forward_exponent<catenae::q31>(65536) == 17, "");
        SUCCEED();
    }

} // namespace
