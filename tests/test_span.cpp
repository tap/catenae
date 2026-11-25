/// @file test_span.cpp
/// @brief span's contract (include/catenae/span.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/span.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::dynamic_extent;
    using catenae::span;

    constexpr int k_table[] = {1, 2, 3, 4, 5};

    constexpr int sum(span<const int> s) noexcept {
        int total = 0;
        for (const int v : s) {
            total += v;
        }
        return total;
    }

    // Verifies CAT-SPAN-001.
    TEST(span, ExtentIsItsSize) {
        std::array<float, 4> four{};
        span<float, 4>       fixed(four);
        static_assert(decltype(fixed)::extent == 4, "a static extent is the type's");
        EXPECT_EQ(fixed.size(), 4u);
        EXPECT_EQ(fixed.size_bytes(), 4u * sizeof(float));
        EXPECT_EQ(fixed.data(), four.data());

        std::vector<double> seven(7);
        span<double>        dynamic(seven);
        static_assert(decltype(dynamic)::extent == dynamic_extent, "a dynamic extent is dynamic_extent");
        EXPECT_EQ(dynamic.size(), 7u);
        EXPECT_EQ(dynamic.size_bytes(), 7u * sizeof(double));
        EXPECT_FALSE(dynamic.empty());
        EXPECT_TRUE(span<int>().empty());
        EXPECT_EQ(span<int>().data(), nullptr);
        static_assert(sizeof(span<float, 4>) == sizeof(float*), "a static extent stores no size");

        static_assert(sum(span<const int>(k_table)) == 15, "a span is usable in a constant expression");
        static_assert(span<const int>(k_table).size() == 5, "a span's size is a constant expression");
    }

    // Verifies CAT-SPAN-002.
    TEST(span, SubviewsAreStdSpans) {
        int  values[6] = {0, 1, 2, 3, 4, 5};
        span s(values);
        static_assert(std::is_same<decltype(s), span<int, 6>>::value, "an array deduces its extent");
        static_assert(std::is_same<decltype(s.first<2>()), span<int, 2>>::value, "first<N> is static");
        static_assert(std::is_same<decltype(s.last<3>()), span<int, 3>>::value, "last<N> is static");
        static_assert(std::is_same<decltype(s.subspan<1, 2>()), span<int, 2>>::value, "subspan<O, C> is static");
        static_assert(std::is_same<decltype(s.subspan<2>()), span<int, 4>>::value, "subspan<O> of a static span");
        static_assert(std::is_same<decltype(s.first(2)), span<int>>::value, "first(n) is dynamic");
        EXPECT_EQ(s.first<2>().data(), values);
        EXPECT_EQ(s.last<3>().data(), values + 3);
        EXPECT_EQ((s.subspan<1, 2>().data()), values + 1);
        EXPECT_EQ(s.subspan<2>().size(), 4u);
        EXPECT_EQ(s.first(4).size(), 4u);
        EXPECT_EQ(s.last(1).data(), values + 5);
        EXPECT_EQ(s.subspan(3).data(), values + 3);
        EXPECT_EQ(s.subspan(3).size(), 3u);
        EXPECT_EQ(s.subspan(1, 0).size(), 0u);
        EXPECT_EQ(s.front(), 0);
        EXPECT_EQ(s.back(), 5);
        EXPECT_EQ(s[4], 4);

        span<int> d(values, 6);
        static_assert(std::is_same<decltype(d.subspan<2>()), span<int>>::value, "subspan<O> of a dynamic span");
        EXPECT_EQ(d.subspan<2>().size(), 4u);
        EXPECT_EQ(span<int>(values + 1, values + 4).size(), 3u);

        int expected = 5;
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
            EXPECT_EQ(*it, expected--);
        }
        EXPECT_EQ(std::distance(s.begin(), s.end()), 6);

        const auto bytes = catenae::as_bytes(s);
        static_assert(std::is_same<decltype(bytes), const span<const std::byte, 6 * sizeof(int)>>::value,
                      "as_bytes keeps a static extent in bytes");
        EXPECT_EQ(static_cast<const void*>(bytes.data()), static_cast<void*>(values));
        EXPECT_EQ(catenae::as_writable_bytes(d).size(), 6 * sizeof(int));
    }

    // Verifies CAT-SPAN-003.
    TEST(span, ConvertsAsStdSpanDoes) {
        static_assert(std::is_convertible<span<int>, span<const int>>::value, "T to const T, implicitly");
        static_assert(!std::is_convertible<span<const int>, span<int>>::value, "never const T to T");
        static_assert(std::is_convertible<span<int, 4>, span<int>>::value, "static to dynamic, implicitly");
        static_assert(std::is_convertible<span<int, 4>, span<const int, 4>>::value, "static to static const");
        static_assert(!std::is_convertible<span<int>, span<int, 4>>::value, "dynamic to static, never implicitly");
        static_assert(std::is_constructible<span<int, 4>, span<int>>::value, "dynamic to static, explicitly");
        static_assert(!std::is_constructible<span<int, 4>, span<int, 5>>::value, "never between two static extents");
        static_assert(std::is_convertible<std::vector<float>&, span<float>>::value, "a container, implicitly");
        static_assert(std::is_convertible<std::vector<float>&, span<const float>>::value, "a container, as const");
        static_assert(!std::is_convertible<std::vector<float>&, span<float, 4>>::value, "a container to static: no");
        static_assert(std::is_constructible<span<float, 4>, std::vector<float>&>::value,
                      "a container to static, explicitly");
        static_assert(!std::is_constructible<span<double>, std::vector<float>&>::value, "never another element type");
        static_assert(std::is_convertible<const std::array<int, 3>&, span<const int, 3>>::value, "a const array");
        static_assert(!std::is_constructible<span<int, 3>, const std::array<int, 3>&>::value,
                      "a const array is never viewed as writable");
        static_assert(!std::is_constructible<span<int, 4>>::value, "a static extent above 0 has no default");

        std::vector<float>   v(3, 1.0f);
        const span<float>    dynamic = v;
        const span<float, 3> fixed(dynamic);
        EXPECT_EQ(fixed.data(), v.data());
        const span<const float> viewed = fixed;
        EXPECT_EQ(viewed.size(), 3u);
    }

    // Verifies CAT-SPAN-004.
    TEST(span, OutOfRangeIsAnAuditCheck) {
        // Four elements viewed of eight, so that an index past the view is still inside the
        // array: at the boundary level it reads the array, unchecked, and at audit it is caught.
        int       storage[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        span<int> s(storage, 4);
        if constexpr (catenae::detail::k_audit_level) {
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s[4]));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s.first(5)));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s.last(5)));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s.subspan(5)));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s.subspan(2, 3)));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s.first<5>()));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(s.subspan<3, 2>()));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(span<int, 4>(s.first(3))));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(span<int>().front()));
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(span<int>().back()));
        }
        else {
            CATENAE_EXPECT_NO_CHECK(EXPECT_EQ(s[5], 5));
            CATENAE_EXPECT_NO_CHECK(EXPECT_EQ(s.first(6).size(), 6u));
        }
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(s[3]));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(s.subspan(4)));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(s.subspan(4, 0)));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(s.last(4)));
    }

} // namespace
