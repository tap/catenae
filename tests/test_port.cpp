/// @file test_port.cpp
/// @brief The ports' contract and the joint check (include/catenae/port.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/port.h"

namespace {

    using catenae::layout;
    using catenae::port;
    using catenae::signal_kind;
    using catenae::span;
    using catenae::spectrum;

    /// A wire format as unpack and pack will name one: what the test needs of it.
    struct s24_le {
        static constexpr std::size_t bytes_per_sample = 3;
    };

    // Verifies CAT-PORT-001.
    TEST(port, ValueTypeIsTheFormatsStorage) {
        static_assert(std::is_same<port<float, 2>::value_type, float>::value, "float is stored as float");
        static_assert(std::is_same<port<catenae::q31, 2>::value_type, std::int32_t>::value, "q31 as int32_t");
        static_assert(std::is_same<port<catenae::q15, 1>::value_type, std::int16_t>::value, "q15 as int16_t");
        static_assert(std::is_same<port<catenae::q31, 2>::format, catenae::q31>::value, "the tag is the port's");
        static_assert(port<double, 6>::values_per_frame == 6 && port<double, 6>::channels == 6, "Ch values a frame");
        static_assert(port<float, 2>::kind == signal_kind::audio, "an audio port");
        static_assert(port<float, 2>::arrangement == layout::interleaved, "interleaved by default");
        static_assert(std::is_same<port<float, 2>, port<float, 2, layout::interleaved>>::value, "one type");

        using bins = spectrum<float, 2, 129>;
        static_assert(std::is_same<bins::value_type, catenae::complex<float>>::value, "complex coefficients");
        static_assert(bins::values_per_frame == 2 * 129 && bins::bins == 129, "Ch x Bins values a frame");
        static_assert(bins::kind == signal_kind::spectrum, "a spectrum");
        static_assert(std::is_same<spectrum<catenae::q15, 1, 4>::value_type, catenae::complex<std::int16_t>>::value,
                      "a fixed-point spectrum holds complex storage");

        using wire = catenae::wire_port<s24_le, 2>;
        static_assert(std::is_same<wire::value_type, std::byte>::value, "bytes");
        static_assert(wire::values_per_frame == 6 && wire::bytes_per_sample == 3, "Ch x bytes_per_sample a frame");
        static_assert(wire::kind == signal_kind::wire, "a wire port");

        static_assert(catenae::is_port<port<float, 1>>::value && catenae::is_port<bins>::value
                          && catenae::is_port<wire>::value,
                      "the three kinds are ports");
        static_assert(!catenae::is_port<float>::value && !catenae::is_port<s24_le>::value, "nothing else is");
        SUCCEED();
    }

    // Verifies CAT-PORT-002.
    TEST(port, ViewsFollowTheLayout) {
        static_assert(std::is_same<port<float, 2>::input_view, span<const float>>::value, "one span in");
        static_assert(std::is_same<port<float, 2>::output_view, span<float>>::value, "one span out");
        using planar = port<float, 3, layout::planar>;
        static_assert(std::is_same<planar::input_view, std::array<span<const float>, 3>>::value, "Ch spans in");
        static_assert(std::is_same<planar::output_view, std::array<span<float>, 3>>::value, "Ch spans out");
        static_assert(std::is_same<port<catenae::q31, 2>::input_view, span<const std::int32_t>>::value,
                      "a fixed-point port is viewed as its storage");
        static_assert(std::is_same<spectrum<float, 2, 9>::output_view, span<catenae::complex<float>>>::value,
                      "a spectrum as one span of complex values");
        static_assert(std::is_same<catenae::wire_port<s24_le, 2>::input_view, span<const std::byte>>::value,
                      "a wire port as one span of bytes");
        SUCCEED();
    }

    // Verifies CAT-PORT-003: the same port joins. Each way two can differ is a case of C1's
    // compile-fail battery (PLAN.md section 10), with the chain that forms the joints.
    TEST(port, SamePortsJoin) {
        static_assert(catenae::joint<0, port<float, 2>, port<float, 2>>::value, "a joint of one type");
        static_assert(catenae::joint<3, spectrum<float, 2, 9>, spectrum<float, 2, 9>>::index == 3,
                      "a joint names its place in the chain");
        static_assert(
            catenae::joint<1, port<catenae::q31, 1, layout::planar>, port<catenae::q31, 1, layout::planar>>::value,
            "a planar fixed-point joint");
        SUCCEED();
    }

} // namespace
