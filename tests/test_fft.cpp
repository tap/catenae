/// @file test_fft.cpp
/// @brief The real FFT's contract (include/catenae/fft.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
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
#include "catenae_test/check_trap.h"
#include "fft_fingerprints.h"

// The fingerprints assume every float and double operation rounds to its own format.
#if defined(FLT_EVAL_METHOD)
static_assert(FLT_EVAL_METHOD == 0, "catenae: evaluation in wider precision (x87) moves the FFT's output bits");
#endif

// The longest length the battery transforms: 2^20 hosted, 4096 on the bare-metal legs, whose RAM
// holds no 65536-point double frame.
#ifndef CATENAE_TEST_MAX_FFT_N
#define CATENAE_TEST_MAX_FFT_N 1048576
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

    /// A plan of any length and the storage it may view: the constant tables to 65536, a plan made
    /// at run time beyond.
    template <typename T>
    struct held_plan {
        std::vector<T>       storage;
        catenae::fft_plan<T> plan;
    };

    template <typename T>
    held_plan<T> plan_of(std::size_t n) {
        held_plan<T> held{{}, catenae::fft_plan<T>(catenae::fft_plan<T>::template of<4>())};
        switch (n) {
#define CATENAE_TEST_CASE(len)                                                                                         \
    case len:                                                                                                          \
        held.plan = catenae::fft_plan<T>(catenae::fft_plan<T>::template of<len>());                                    \
        return held;
            CATENAE_FFT_LENGTHS(CATENAE_TEST_CASE)
#undef CATENAE_TEST_CASE
        default:
            break;
        }
        held.storage.resize(catenae::fft_workspace_size<T>(n));
        const auto made = catenae::fft_plan<T>::make(n, catenae::span<T>(held.storage.data(), held.storage.size()));
        if (!made.has_value()) {
            ADD_FAILURE() << "no plan of length " << n;
            return held;
        }
        held.plan = *made;
        return held;
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
        using sample = TypeParam;
        for (std::size_t n = 4; n <= 256; n *= 2u) {
            const std::vector<sample> x = noise<sample>(n, 7u);
            auto                      a = frame_of(x);
            plan_of<sample>(n).plan.forward(catenae::span<catenae::complex<sample>>(a.data(), a.size()));
            std::vector<double> xd(x.begin(), x.end());
            const auto          ref = reference_dft(xd);
            // The floors, measured on x86-64 (printed below) and pinned at twice the worst.
            const double err = relative_error(a, ref, a.size());
            std::printf("[ floor ] %s N=%u forward vs DFT %.3g\n",
                        std::is_same<sample, float>::value ? "float" : "double", static_cast<unsigned>(n), err);
            EXPECT_LT(err, (std::is_same<sample, float>::value ? 2.0e-7 : 6.1e-16)) << n;
            // DC and Nyquist are real: their imaginary parts are written as exact zeros.
            EXPECT_EQ(std::fpclassify(a[0].im), FP_ZERO) << n;
            EXPECT_EQ(std::fpclassify(a[n / 2u].im), FP_ZERO) << n;
        }
    }

    // Verifies CAT-FFT-001.
    TYPED_TEST(fft_formats, ATonesBinsAreWhereExpMinusPutsThem) {
        using sample            = TypeParam;
        constexpr std::size_t n = 64;
        // x[j] = sin(2 pi 5 j / 64): X[5] = -i N/2 under exp(-i), +i N/2 under exp(+i).
        std::vector<sample> x(n);
        for (std::size_t j = 0; j < n; ++j) {
            x[j] = static_cast<sample>(std::sin(6.283185307179586 * 5.0 * static_cast<double>(j) / 64.0));
        }
        auto a = frame_of(x);
        catenae::fft_plan<sample>::template of<n>().forward(
            catenae::span<catenae::complex<sample>>(a.data(), a.size()));
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
        using sample = TypeParam;
        for (std::size_t n = 4; n <= max_n; n *= 2u) {
            const std::vector<sample> x    = noise<sample>(n, 11u);
            auto                      a    = frame_of(x);
            const auto                held = plan_of<sample>(n);
            const auto&               plan = held.plan;
            plan.forward(catenae::span<catenae::complex<sample>>(a.data(), a.size()));
            const int e = plan.inverse(catenae::span<catenae::complex<sample>>(a.data(), a.size()));
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
            EXPECT_LT(worst, (std::is_same<sample, float>::value ? 1.0e-6 : 2.0e-15)) << n;
        }
    }

    // Verifies CAT-FFT-002.
    TYPED_TEST(fft_formats, InverseIsTheUnnormalizedExpPlusSum) {
        using sample            = TypeParam;
        constexpr std::size_t n = 32;
        // One bin: X[3] = 1 - 2i (and its conjugate at 29, implied). The inverse is
        // 2 Re((1 - 2i) exp(+i 2 pi 3 j / 32)) = 2 cos(theta) + 4 sin(theta).
        std::vector<catenae::complex<sample>> a(n / 2u + 1u, catenae::complex<sample>{sample(0), sample(0)});
        a[3]        = {sample(1), sample(-2)};
        const int e = catenae::fft_plan<sample>::template of<n>().inverse(
            catenae::span<catenae::complex<sample>>(a.data(), a.size()));
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

    /// Bin k of the DFT of real x, in double, as reference_dft computes it: within an ulp or two.
    catenae::complex<double> reference_bin(const std::vector<double>& x, std::size_t k) {
        const std::size_t n       = x.size();
        const double      two_pi  = 6.283185307179586476925286766559;
        double            sum[2]  = {0.0, 0.0};
        double            comp[2] = {0.0, 0.0};
        for (std::size_t j = 0; j < n; ++j) {
            const double angle = two_pi * static_cast<double>((k * j) % n) / static_cast<double>(n);
            const double w[2]  = {std::cos(angle), -std::sin(angle)};
            for (std::size_t part = 0; part < 2u; ++part) {
                const double p  = x[j] * w[part];
                const double pe = std::fma(x[j], w[part], -p);
                for (const double term : {p, pe}) {
                    const double t = sum[part] + term;
                    comp[part] +=
                        std::fabs(sum[part]) >= std::fabs(term) ? (sum[part] - t) + term : (term - t) + sum[part];
                    sum[part] = t;
                }
            }
        }
        return {sum[0] + comp[0], sum[1] + comp[1]};
    }

    // Verifies CAT-FFT-001.
    TEST(fft_floor, LongLengthsTrackTheDftAndDouble) {
        // 2^17 to 2^20, plans made at run time; hosted legs only (CATENAE_TEST_MAX_FFT_N).
        for (std::size_t n = 131072; n <= max_n; n *= 2u) {
            const auto xf = noise<float>(n, 3u);
            const auto xd = noise<double>(n, 3u);
            auto       af = frame_of(xf);
            auto       ad = frame_of(xd);
            plan_of<float>(n).plan.forward(catenae::span<catenae::complex<float>>(af.data(), af.size()));
            plan_of<double>(n).plan.forward(catenae::span<catenae::complex<double>>(ad.data(), ad.size()));
            const double float_err = relative_error(af, ad, af.size());
            // Sixteen bins of the double forward against the compensated DFT, spread over the band.
            std::vector<catenae::complex<double>> got;
            std::vector<catenae::complex<double>> want;
            for (std::size_t i = 0; i < 16u; ++i) {
                const std::size_t k = (i * (n / 2u)) / 15u;
                got.push_back(ad[k]);
                want.push_back(reference_bin(xd, k));
            }
            const double double_err = relative_error(got, want, got.size());
            std::printf("[ floor ] N=%u float vs double %.3g, double vs DFT (16 bins) %.3g\n", static_cast<unsigned>(n),
                        float_err, double_err);
            // The floors measured on x86-64 (GCC 13 and Clang 18, the same bits), pinned at twice.
            EXPECT_LT(float_err, 3.2e-7) << n;
            EXPECT_LT(double_err, 6.7e-16) << n;
        }
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
        auto        a    = frame_of(noise<T>(n, pins::seed));
        const auto  held = plan_of<T>(n);
        const auto& plan = held.plan;
        plan.forward(catenae::span<catenae::complex<T>>(a.data(), a.size()));
        std::uint64_t h = fold(0xcbf29ce484222325u, a, a.size());
        static_cast<void>(plan.inverse(catenae::span<catenae::complex<T>>(a.data(), a.size())));
        return fold(h, a, n / 2u);
    }

    // Verifies CAT-FFT-003.
    TYPED_TEST(fft_formats, OutputBitsAreThePins) {
        using sample           = TypeParam;
        const std::uint64_t* p = std::is_same<sample, float>::value ? pins::float_pins : pins::double_pins;
        std::size_t          i = 0;
        for (std::size_t n = 4; n <= max_n; n *= 2u, ++i) {
            const std::uint64_t got = fingerprint<sample>(n);
            // Plain unsigned: newlib's printf on the bare-metal legs parses neither %zu nor PRIx64.
            std::printf("[ fingerprint ] %s N=%u 0x%08x%08x\n", std::is_same<sample, float>::value ? "float" : "double",
                        static_cast<unsigned>(n), static_cast<unsigned>(got >> 32u),
                        static_cast<unsigned>(got & 0xffffffffu));
            EXPECT_EQ(got, p[i]) << n;
        }
        EXPECT_GE(i, 11u) << "the sweep reaches N = 4096 on every leg";
        EXPECT_TRUE(max_n < 1048576u || i == 19u) << "the hosted sweep reaches N = 2^20";
    }

    // Verifies CAT-FFT-004.
    TEST(fft_lengths, MakeTakes4To2Pow20AndTheTables4To65536) {
        static_assert(catenae::fft_min_size == 4u && catenae::fft_max_size == 1048576u, "");
        static_assert(catenae::fft_max_table_size == 65536u, "");
        static_assert(catenae::fft_supports(4) && catenae::fft_supports(65536) && catenae::fft_supports(1048576), "");
        static_assert(!catenae::fft_supports(0) && !catenae::fft_supports(2) && !catenae::fft_supports(3), "");
        static_assert(!catenae::fft_supports(12) && !catenae::fft_supports(2097152) && !catenae::fft_supports(65535),
                      "");
        static_assert(catenae::fft_has_tables(4) && catenae::fft_has_tables(65536), "");
        static_assert(!catenae::fft_has_tables(131072) && !catenae::fft_has_tables(12), "");
        static_assert(catenae::fft_bins(512) == 257u, "");
        std::vector<float> storage(catenae::fft_workspace_size<float>(65536));
        for (const std::size_t n : {0u, 1u, 2u, 3u, 6u, 12u, 100u, 2097152u}) {
            const auto made = catenae::fft_plan<float>::make(n, catenae::span<float>(storage.data(), storage.size()));
            ASSERT_FALSE(made.has_value()) << n;
            EXPECT_EQ(made.error(), catenae::status::out_of_range) << n;
        }
        const auto small = catenae::fft_plan<float>::make(
            1024, catenae::span<float>(storage.data(), catenae::fft_workspace_size<float>(1024) - 1u));
        ASSERT_FALSE(small.has_value());
        EXPECT_EQ(small.error(), catenae::status::size_mismatch);
        // Above the tables, make() still builds a plan, given room.
        const auto big = catenae::fft_plan<float>::make(131072, catenae::span<float>(storage.data(), storage.size()));
        ASSERT_FALSE(big.has_value());
        EXPECT_EQ(big.error(), catenae::status::size_mismatch);
        std::vector<float> room(catenae::fft_workspace_size<float>(1048576));
        const auto top = catenae::fft_plan<float>::make(1048576, catenae::span<float>(room.data(), room.size()));
        ASSERT_TRUE(top.has_value());
        EXPECT_EQ(top->size(), 1048576u);
    }

    // Verifies CAT-FFT-005.
    TYPED_TEST(fft_formats, APlanMadeAtRunTimeHoldsTheConstantTables) {
        using sample = TypeParam;
        for (std::size_t n = 4; n <= max_n && n <= catenae::fft_max_table_size; n *= 2u) {
            std::vector<sample> storage(catenae::fft_workspace_size<sample>(n));
            const auto made = catenae::fft_plan<sample>::make(n, catenae::span<sample>(storage.data(), storage.size()));
            ASSERT_TRUE(made.has_value()) << n;
            EXPECT_EQ(made->size(), n);
            // The tables, bit for bit: the run-time plan's transform is the constant plan's.
            const auto x = noise<sample>(n, 5u);
            auto       a = frame_of(x);
            auto       b = frame_of(x);
            made->forward(catenae::span<catenae::complex<sample>>(a.data(), a.size()));
            plan_of<sample>(n).plan.forward(catenae::span<catenae::complex<sample>>(b.data(), b.size()));
            EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof a[0]), 0) << n;
        }
        // And the table values themselves, at one length of each kind of table.
        std::vector<sample> storage(catenae::fft_workspace_size<sample>(1024));
        ASSERT_TRUE(
            catenae::fft_plan<sample>::make(1024, catenae::span<sample>(storage.data(), storage.size())).has_value());
        const auto& constant = catenae::fft_tables<sample, 1024>::values;
        ASSERT_EQ(constant.size(), storage.size());
        EXPECT_EQ(std::memcmp(constant.data(), storage.data(), storage.size() * sizeof(sample)), 0);
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

    // Verifies CAT-FFT-008.
    TYPED_TEST(fft_formats, AFixedPlanTakesFramesOfStaticExtent) {
        using sample                = TypeParam;
        using cx                    = catenae::complex<sample>;
        constexpr std::size_t n     = 256;
        constexpr auto        fixed = catenae::fft_plan<sample>::template of<n>();
        static_assert(decltype(fixed)::size() == n && decltype(fixed)::bins == n / 2u + 1u, "");
        const auto x = noise<sample>(n, 21u);
        // The same bits through a std::array, an array, a static span and the plan for any length.
        const auto                  v = frame_of(x);
        std::array<cx, n / 2u + 1u> by_array{};
        cx                          by_c_array[n / 2u + 1u]{};
        std::array<cx, n / 2u + 1u> by_span{};
        std::vector<cx>             by_plan(v);
        for (std::size_t i = 0; i < v.size(); ++i) {
            by_array[i]   = v[i];
            by_c_array[i] = v[i];
            by_span[i]    = v[i];
        }
        fixed.forward(by_array);
        fixed.forward(by_c_array);
        fixed.forward(catenae::span<cx, n / 2u + 1u>(by_span));
        const catenae::fft_plan<sample> any(fixed);
        EXPECT_EQ(any.size(), n);
        any.forward(catenae::span<cx>(by_plan.data(), by_plan.size()));
        EXPECT_EQ(std::memcmp(by_array.data(), by_plan.data(), sizeof by_array), 0);
        EXPECT_EQ(std::memcmp(by_c_array, by_plan.data(), sizeof by_c_array), 0);
        EXPECT_EQ(std::memcmp(by_span.data(), by_plan.data(), sizeof by_span), 0);
        EXPECT_EQ(fixed.inverse(by_array), 1);
        EXPECT_EQ(any.inverse(catenae::span<cx>(by_plan.data(), by_plan.size())), 1);
        EXPECT_EQ(std::memcmp(by_array.data(), by_plan.data(), sizeof by_array), 0);
    }

    // Verifies CAT-FFT-009.
    TYPED_TEST(fft_formats, AFrameOfAnotherSizeFailsTheBoundaryCheck) {
        using sample = TypeParam;
        using cx     = catenae::complex<sample>;
        std::vector<cx>       frame(130, cx{sample(1), sample(2)});
        const std::vector<cx> before = frame;
        const auto            plan   = catenae::fft_plan<sample>(catenae::fft_plan<sample>::template of<256>());
        // 129 bins is right for 256; one short and one over are not, and nothing is written.
        CATENAE_EXPECT_CHECK(catenae::check::fft_frame_size, plan.forward(catenae::span<cx>(frame.data(), 128)));
        CATENAE_EXPECT_CHECK(catenae::check::fft_frame_size, plan.forward(catenae::span<cx>(frame.data(), 130)));
        CATENAE_EXPECT_CHECK(catenae::check::fft_frame_size,
                             static_cast<void>(plan.inverse(catenae::span<cx>(frame.data(), 128))));
        EXPECT_EQ(std::memcmp(frame.data(), before.data(), frame.size() * sizeof(cx)), 0);
        // The fixed plan given a dynamic span checks it the same way, and a plan made at run time too.
        constexpr auto fixed = catenae::fft_plan<sample>::template of<256>();
        CATENAE_EXPECT_CHECK(catenae::check::fft_frame_size, fixed.forward(catenae::span<cx>(frame.data(), 130)));
        std::vector<sample> storage(catenae::fft_workspace_size<sample>(256));
        const auto made = catenae::fft_plan<sample>::make(256, catenae::span<sample>(storage.data(), storage.size()));
        ASSERT_TRUE(made.has_value());
        CATENAE_EXPECT_CHECK(catenae::check::fft_frame_size, made->forward(catenae::span<cx>(frame.data(), 1)));
        CATENAE_EXPECT_NO_CHECK(made->forward(catenae::span<cx>(frame.data(), 129)));
    }

} // namespace
