/// @file test_biquad_kernel.cpp
/// @brief The biquad kernel's contract (include/catenae/detail/biquad_kernel.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/detail/biquad_kernel.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T>
    class biquad_kernel_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(biquad_kernel_formats, formats, );

    template <typename S>
    using row = catenae::detail::biquad_row<S>;

    /// K stable sections with every coefficient distinct: each a pole pair at radius r and angle
    /// t, and a zero pair at its own radius and angle.
    template <typename S, std::size_t K>
    std::array<row<S>, K> sections() {
        std::array<row<S>, K> s{};
        for (std::size_t k = 0; k < K; ++k) {
            const double r  = 0.55 + 0.05 * static_cast<double>(k);
            const double t  = 0.3 + 0.37 * static_cast<double>(k);
            const double rz = 0.9 - 0.04 * static_cast<double>(k);
            const double tz = 1.1 + 0.21 * static_cast<double>(k);
            const double g  = 0.4 + 0.1 * static_cast<double>(k);
            s[k] = {static_cast<S>(g), static_cast<S>(-2.0 * g * rz * std::cos(tz)), static_cast<S>(g * rz * rz),
                    static_cast<S>(-2.0 * r * std::cos(t)), static_cast<S>(r * r)};
        }
        return s;
    }

    /// Per channel, the per-frame kernel over the whole block.
    template <typename S, std::size_t K, std::size_t Ch>
    std::vector<S> by_frames(const std::array<row<S>, K>& sec, const std::vector<S>& in) {
        std::vector<S>    out(in.size());
        std::vector<S>    z(Ch * 2 * K, S{0});
        const std::size_t frames = in.size() / Ch;
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t c = 0; c < Ch; ++c) {
                out[f * Ch + c] =
                    catenae::detail::biquad_cascade_frame<S, K>(sec, z.data() + c * 2 * K, in[f * Ch + c]);
            }
        }
        return out;
    }

    /// The block kernel over calls of the given sizes, in place when asked.
    template <typename S, std::size_t K, std::size_t Ch>
    std::vector<S> by_blocks(const std::array<row<S>, K>& sec, const std::vector<S>& in,
                             const std::vector<std::size_t>& sizes, bool in_place) {
        std::vector<S> out = in;
        std::vector<S> z(Ch * 2 * K, S{0});
        std::size_t    at = 0;
        for (const std::size_t n : sizes) {
            const S* from = in_place ? out.data() + at * Ch : in.data() + at * Ch;
            catenae::detail::biquad_cascade_block<S, K, Ch>(sec, z.data(), from, n, out.data() + at * Ch);
            at += n;
        }
        return out;
    }

    template <typename S, std::size_t K, std::size_t Ch>
    void block_is_frame_by_frame() {
        constexpr std::size_t frames = 600;
        catenae_test::signal  noise(7u + static_cast<std::uint32_t>(K * 16 + Ch));
        std::vector<S>        in(frames * Ch);
        for (S& v : in) {
            v = noise.next<S>();
        }
        const auto sec  = sections<S, K>();
        const auto once = by_frames<S, K, Ch>(sec, in);
        for (const std::uint32_t seed : {1u, 2u}) {
            const std::vector<std::size_t> sizes = catenae_test::chunks(frames, 9, seed); // single frames among them
            for (const bool in_place : {false, true}) {
                const auto blocks = by_blocks<S, K, Ch>(sec, in, sizes, in_place);
                ASSERT_EQ(std::memcmp(blocks.data(), once.data(), once.size() * sizeof(S)), 0)
                    << K << " sections, " << Ch << " channels, seed " << seed << (in_place ? ", in place" : "");
            }
        }
    }

    // Verifies CAT-BIQUAD-KERNEL-001.
    TYPED_TEST(biquad_kernel_formats, TheBlockIsTheFrameKernelFrameAfterFrame) {
        block_is_frame_by_frame<TypeParam, 1, 1>();
        block_is_frame_by_frame<TypeParam, 1, 2>();
        block_is_frame_by_frame<TypeParam, 1, 3>();
        block_is_frame_by_frame<TypeParam, 2, 5>();
        block_is_frame_by_frame<TypeParam, 3, 2>();
        block_is_frame_by_frame<TypeParam, 4, 1>();
        block_is_frame_by_frame<TypeParam, 4, 3>();
        block_is_frame_by_frame<TypeParam, 8, 2>();
    }

    /// One section in direct form I, in double: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
    std::vector<double> direct_form_one(const row<double>& c, const std::vector<double>& x) {
        std::vector<double> y(x.size());
        double              x1 = 0.0;
        double              x2 = 0.0;
        double              y1 = 0.0;
        double              y2 = 0.0;
        for (std::size_t n = 0; n < x.size(); ++n) {
            y[n] = c[0] * x[n] + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
            x2   = x1;
            x1   = x[n];
            y2   = y1;
            y1   = y[n];
        }
        return y;
    }

    // Verifies CAT-BIQUAD-KERNEL-002.
    TYPED_TEST(biquad_kernel_formats, TheCascadeIsItsSectionsDifferenceEquations) {
        constexpr std::size_t  K      = 3;
        constexpr std::size_t  frames = 400;
        const auto             sec    = sections<TypeParam, K>();
        catenae_test::signal   noise(11u);
        std::vector<TypeParam> in(frames);
        for (TypeParam& v : in) {
            v = noise.next<TypeParam>();
        }
        // The reference: each section in turn, in direct form I, from the coefficients as TypeParam holds them.
        std::vector<double> want(in.begin(), in.end());
        for (std::size_t k = 0; k < K; ++k) {
            const row<double> c = {static_cast<double>(sec[k][0]), static_cast<double>(sec[k][1]),
                                   static_cast<double>(sec[k][2]), static_cast<double>(sec[k][3]),
                                   static_cast<double>(sec[k][4])};
            want                = direct_form_one(c, want);
        }
        const auto   got   = by_frames<TypeParam, K, 1>(sec, in);
        const double bound = std::is_same<TypeParam, float>::value ? 1e-5 : 1e-13; // the format's rounding, accumulated
        for (std::size_t n = 0; n < frames; ++n) {
            ASSERT_NEAR(static_cast<double>(got[n]), want[n], bound) << "frame " << n;
        }
        // The impulse response of one section is its difference equation's.
        std::vector<TypeParam> impulse(64, TypeParam{0});
        impulse[0]            = TypeParam{1};
        const auto          h = by_frames<TypeParam, 1, 1>(std::array<row<TypeParam>, 1>{{sec[0]}}, impulse);
        std::vector<double> delta(64, 0.0);
        delta[0]         = 1.0;
        const auto h_ref = direct_form_one({static_cast<double>(sec[0][0]), static_cast<double>(sec[0][1]),
                                            static_cast<double>(sec[0][2]), static_cast<double>(sec[0][3]),
                                            static_cast<double>(sec[0][4])},
                                           delta);
        for (std::size_t n = 0; n < h.size(); ++n) {
            ASSERT_NEAR(static_cast<double>(h[n]), h_ref[n], bound) << "impulse " << n;
        }
    }

} // namespace
