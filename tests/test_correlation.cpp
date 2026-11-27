/// @file test_correlation.cpp
/// @brief The correlators' contracts (include/catenae/autocorrelation.h, cross_correlation.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "analyzer_support.h"
#include "catenae/autocorrelation.h"
#include "catenae/cross_correlation.h"
#include "catenae/tap.h"
#include "catenae_test/harness.h"

namespace {

    using analyzer_test::formats;

    template <typename S>
    class correlation : public ::testing::Test {};
    TYPED_TEST_SUITE(correlation, formats);

    constexpr std::size_t k_hop    = 32;
    constexpr std::size_t k_window = 96; // three hops
    constexpr std::size_t k_lag    = 5;

    /// x[f] of channel c, 0 before the first frame.
    template <typename V>
    V at(const std::vector<V>& in, std::size_t ch, std::size_t c, std::ptrdiff_t f) {
        return f < 0 ? V{} : in[static_cast<std::size_t>(f) * ch + c];
    }

    template <typename V, std::size_t N>
    std::array<V, N> add(std::array<V, N> a, const std::array<V, N>& b) {
        for (std::size_t i = 0; i < N; ++i) {
            a[i] += b[i];
        }
        return a;
    }
    template <typename V, std::size_t N, std::size_t M>
    std::array<std::array<V, M>, N> add(std::array<std::array<V, M>, N> a, const std::array<std::array<V, M>, N>& b) {
        for (std::size_t i = 0; i < N; ++i) {
            a[i] = add(a[i], b[i]);
        }
        return a;
    }
    template <typename V, std::size_t L>
    catenae::cross_sums<V, L> add(catenae::cross_sums<V, L> a, const catenae::cross_sums<V, L>& b) {
        a.lags = add(a.lags, b.lags);
        a.energy_a += b.energy_a;
        a.energy_b += b.energy_b;
        return a;
    }

    /// The window's sums from per-hop sums: oldest first from 0, hops before reset being 0.
    template <typename T>
    std::vector<T> windows_of(const std::vector<T>& per_hop, std::size_t hops, T zero) {
        std::vector<T> out;
        for (std::size_t h = 0; h < per_hop.size(); ++h) {
            T w = zero;
            for (std::size_t back = hops; back-- > 0;) {
                w = h >= back ? add(w, per_hop[h - back]) : add(w, zero);
            }
            out.push_back(w);
        }
        return out;
    }

    // Verifies CAT-AUTOCORRELATION-001.
    TYPED_TEST(correlation, AutocorrelationIsTheWindowsLaggedSums) {
        using S                                 = TypeParam;
        constexpr std::size_t ch                = 2;
        using A                                 = catenae::basic_autocorrelation<S, ch, k_lag, k_hop, k_window>;
        using sum                               = typename A::sum_type;
        const auto                           in = analyzer_test::noise<S>(ch * (k_hop * 14 + 3), 41);
        std::vector<typename A::result_type> per_hop;
        for (std::size_t end = k_hop; end * ch <= in.size(); end += k_hop) {
            typename A::result_type r{};
            for (std::size_t f = end - k_hop; f < end; ++f) {
                for (std::size_t c = 0; c < ch; ++c) {
                    for (std::size_t k = 0; k <= k_lag; ++k) {
                        const auto n = static_cast<std::ptrdiff_t>(f);
                        r[c][k] +=
                            analyzer_test::product<S>(at(in, ch, c, n), at(in, ch, c, n - std::ptrdiff_t(k)), A::shift);
                    }
                }
            }
            per_hop.push_back(r);
        }
        const auto expected = windows_of(per_hop, k_window / k_hop, typename A::result_type{});
        static_assert(std::is_same<sum, decltype(analyzer_test::product<S>({}, {}, 0))>::value, "the sum's type");
        for (const std::size_t largest : {std::size_t{1}, std::size_t{9}, k_hop}) {
            EXPECT_TRUE(analyzer_test::same_results(analyzer_test::results_of(A{}, in, largest, 5), expected))
                << "calls up to " << largest;
        }
        // As a tap's analyzer, the same, and the audio passes through.
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(catenae::tap<ch, A>{}));
    }

    // Verifies CAT-CROSS-CORRELATION-001.
    TYPED_TEST(correlation, CrossCorrelationIsTheWindowsLaggedSums) {
        using S                                 = TypeParam;
        constexpr std::size_t ch                = 3;
        using X                                 = catenae::basic_cross_correlation<S, ch, 2, 0, k_lag, k_hop, k_window>;
        const auto                           in = analyzer_test::noise<S>(ch * (k_hop * 11 + 20), 43);
        std::vector<typename X::result_type> per_hop;
        for (std::size_t end = k_hop; end * ch <= in.size(); end += k_hop) {
            typename X::result_type r{};
            for (std::size_t f = end - k_hop; f < end; ++f) {
                const auto n = static_cast<std::ptrdiff_t>(f);
                r.energy_a += analyzer_test::product<S>(at(in, ch, 2, n), at(in, ch, 2, n), X::shift);
                r.energy_b += analyzer_test::product<S>(at(in, ch, 0, n), at(in, ch, 0, n), X::shift);
                r.lags[k_lag] += analyzer_test::product<S>(at(in, ch, 2, n), at(in, ch, 0, n), X::shift);
                for (std::size_t k = 1; k <= k_lag; ++k) {
                    const auto d = static_cast<std::ptrdiff_t>(k);
                    r.lags[k_lag + k] += analyzer_test::product<S>(at(in, ch, 2, n - d), at(in, ch, 0, n), X::shift);
                    r.lags[k_lag - k] += analyzer_test::product<S>(at(in, ch, 2, n), at(in, ch, 0, n - d), X::shift);
                }
            }
            per_hop.push_back(r);
        }
        const auto expected = windows_of(per_hop, k_window / k_hop, typename X::result_type{});
        for (const std::size_t largest : {std::size_t{1}, std::size_t{11}, k_hop}) {
            EXPECT_TRUE(analyzer_test::same_results(analyzer_test::results_of(X{}, in, largest, 3), expected))
                << "calls up to " << largest;
        }
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(catenae::tap<ch, X>{}));
    }

    // Verifies CAT-AUTOCORRELATION-002, CAT-CROSS-CORRELATION-002.
    TYPED_TEST(correlation, NormalizedReadsLagAndSign) {
        using S = TypeParam;
        using V = catenae::storage_t<S>;
        // A sine of period 40 frames: its autocorrelation at lag 40 is near 1, at 20 near -1.
        using A               = catenae::basic_autocorrelation<S, 1, 40, 400>;
        constexpr double full = catenae::format_traits<S>::is_fixed_point
                                    ? double(std::int64_t{1} << catenae::format_traits<S>::fraction_bits) * 0.5
                                    : 0.5;
        std::vector<V>   sine(1200);
        for (std::size_t i = 0; i < sine.size(); ++i) {
            const double v = full * std::sin(6.283185307179586 * double(i) / 40.0);
            if constexpr (std::is_floating_point<V>::value) {
                sine[i] = static_cast<V>(v);
            }
            else {
                sine[i] = static_cast<V>(std::lround(v));
            }
        }
        const auto r = analyzer_test::results_of(A{}, sine, 0, 0);
        ASSERT_EQ(r.size(), 1u);
        EXPECT_NEAR(static_cast<double>(A::normalized(r[0], 0, 40)), 1.0, 0.01);
        EXPECT_NEAR(static_cast<double>(A::normalized(r[0], 0, 20)), -1.0, 0.01);
        EXPECT_NEAR(static_cast<double>(A::normalized(r[0], 0, 0)), 1.0, 1e-6);
        // b is a delayed by 3 frames: r peaks at k = 3; c is -a: r at 0 is -1.
        using X                  = catenae::basic_cross_correlation<S, 3, 0, 1, 4, 256>;
        using Y                  = catenae::basic_cross_correlation<S, 3, 0, 2, 4, 256>;
        const std::vector<V> src = analyzer_test::noise<S>(600, 47);
        std::vector<V>       in(3 * 512);
        for (std::size_t f = 0; f < 512; ++f) {
            V a = src[f + 3];
            if constexpr (!std::is_floating_point<V>::value) {
                a = static_cast<V>(a / 2); // so its negation is exact
            }
            in[f * 3]     = a;
            in[f * 3 + 1] = f >= 3 ? (std::is_floating_point<V>::value ? src[f] : static_cast<V>(src[f] / 2)) : V{};
            in[f * 3 + 2] = static_cast<V>(-a);
        }
        const auto x = analyzer_test::results_of(X{}, in, 0, 0);
        const auto y = analyzer_test::results_of(Y{}, in, 0, 0);
        ASSERT_EQ(x.size(), 1u);
        ASSERT_EQ(y.size(), 1u);
        std::ptrdiff_t best = -4;
        for (std::ptrdiff_t k = -4; k <= 4; ++k) {
            best = X::normalized(x[0], k) > X::normalized(x[0], best) ? k : best;
        }
        EXPECT_EQ(best, 3);
        EXPECT_NEAR(static_cast<double>(X::normalized(x[0], 3)), 1.0, 0.02);
        EXPECT_NEAR(static_cast<double>(Y::normalized(y[0], 0)), -1.0, 1e-5);
        // Silence reads 0.
        const std::vector<V> quiet(3 * 256, V{});
        const auto           q = analyzer_test::results_of(X{}, quiet, 0, 0);
        EXPECT_NEAR(static_cast<double>(X::normalized(q[0], 0)), 0.0, 0.0);
        EXPECT_NEAR(
            static_cast<double>(A::normalized(analyzer_test::results_of(A{}, std::vector<V>(400), 0, 0)[0], 0, 3)), 0.0,
            0.0);
    }

} // namespace
