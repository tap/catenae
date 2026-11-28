/// @file analyzer_support.h
/// @brief What the analyzers' batteries share: every result an analyzer completes, taken under
/// any chunking, and the formats they are typed over.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TESTS_ANALYZER_SUPPORT_H
#define CATENAE_TESTS_ANALYZER_SUPPORT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/analyzer_traits.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae_test/harness.h"

namespace analyzer_test {

    /// The formats a meter's battery runs in.
    using formats = ::testing::Types<float, double, catenae::q15, catenae::q31, catenae::q4_27>;

    /// Every result `analyzer` completes over `in` from reset, in calls of 1 to `largest` frames
    /// (`largest` at most a hop, so no call completes two); in one call when `largest` is 0
    /// and the input is a whole number of hops, the last result alone.
    template <typename A>
    std::vector<typename A::result_type> results_of(const A&                                               analyzer,
                                                    const std::vector<typename A::input_port::value_type>& in,
                                                    std::size_t largest, std::uint32_t seed) {
        using traits                    = catenae::analyzer_traits<A>;
        using value_type                = typename A::input_port::value_type;
        constexpr std::size_t per_frame = A::input_port::values_per_frame;
        auto                  st        = std::make_unique<typename traits::state>();
        std::memset(st.get(), 0x5A, sizeof(typename traits::state)); // reset must not read it
        if constexpr (traits::rests_at_zero) {
            std::memset(st.get(), 0, sizeof(typename traits::state));
        }
        else {
            analyzer.reset(*st);
        }
        std::vector<typename A::result_type> out;
        const std::size_t                    frames = in.size() / per_frame;
        const std::vector<std::size_t>       sizes =
            largest == 0 ? std::vector<std::size_t>{frames} : catenae_test::chunks(frames, largest, seed);
        std::size_t   at   = 0;
        std::uint32_t seen = 0;
        for (const std::size_t n : sizes) {
            analyzer.analyze(*st, catenae::span<const value_type>(in.data() + at * per_frame, n * per_frame));
            at += n;
            const std::uint32_t ready = analyzer.results_ready(*st);
            if (ready != seen) {
                EXPECT_TRUE(largest == 0 || ready - seen == 1u) << "a call of at most a hop completed two results";
                out.push_back(analyzer.result(*st));
                seen = ready;
            }
        }
        return out;
    }

    /// Whether two results are the same bits.
    template <typename R>
    bool same_bits(const R& a, const R& b) {
        return std::memcmp(&a, &b, sizeof(R)) == 0;
    }

    /// Whether two runs' results are the same bits, one by one.
    template <typename R>
    ::testing::AssertionResult same_results(const std::vector<R>& a, const std::vector<R>& b) {
        if (a.size() != b.size()) {
            return ::testing::AssertionFailure() << a.size() << " results against " << b.size();
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!same_bits(a[i], b[i])) {
                return ::testing::AssertionFailure() << "result " << i << " differs";
            }
        }
        return ::testing::AssertionSuccess();
    }

    /// Noise in a format's own values, frame after frame.
    template <typename S>
    std::vector<catenae::storage_t<S>> noise(std::size_t values, std::uint32_t seed) {
        catenae_test::signal               s(seed);
        std::vector<catenae::storage_t<S>> v(values);
        for (auto& x : v) {
            x = s.next<catenae::storage_t<S>>();
        }
        return v;
    }

    /// Floor of a / 2^s, for any sign: the test's own, not the library's shift.
    inline std::int64_t floor_shift(std::int64_t a, int s) {
        const std::int64_t d = std::int64_t{1} << s;
        std::int64_t       q = a / d;
        if (a % d != 0 && a < 0) {
            --q;
        }
        return q;
    }

    /// a x b as a meter's term, written out: in the format's own type when it floats, exact for
    /// Q15, and for a 32-bit format rounded half up by `shift` bits.
    template <typename S>
    auto product(catenae::storage_t<S> a, catenae::storage_t<S> b, int shift) {
        if constexpr (catenae::format_traits<S>::is_fixed_point) {
            const std::int64_t p = static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b);
            return shift == 0 ? p : floor_shift(p + (std::int64_t{1} << (shift - 1)), shift);
        }
        else {
            return a * b;
        }
    }

} // namespace analyzer_test

#endif // CATENAE_TESTS_ANALYZER_SUPPORT_H
