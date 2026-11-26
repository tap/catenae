/// @file test_fir_kernel.cpp
/// @brief The FIR kernel's contract (include/catenae/detail/fir_kernel.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/detail/fir_kernel.h"
#include "catenae/sample_traits.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T>
    class fir_kernel_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(fir_kernel_formats, formats, );

    template <typename S>
    using coeff = typename catenae::sample_traits<S>::coeff;

    /// Taps with every value distinct, inside the coefficient format.
    template <typename S, std::size_t Taps>
    std::array<coeff<S>, Taps> taps(std::uint32_t seed) {
        catenae_test::signal       s(seed);
        std::array<coeff<S>, Taps> h{};
        for (coeff<S>& c : h) {
            c = catenae::sample_traits<S>::make_coeff(0.9 * static_cast<double>(s.next<float>()));
        }
        return h;
    }

    /// The card's sum, written out: s[k mod 4] in ascending k, then (s0 + s1) + (s2 + s3) for a
    /// floating format; the traits' mac chain in ascending k and finalize for a fixed one.
    template <typename S, std::size_t Taps>
    S reference_dot(const std::array<coeff<S>, Taps>& h, const S* window) {
        using traits = catenae::sample_traits<S>;
        if constexpr (traits::is_fixed_point) {
            typename traits::accum acc{};
            for (std::size_t k = 0; k < Taps; ++k) {
                acc = traits::mac(acc, window[Taps - 1 - k], h[k]);
            }
            return traits::finalize(acc);
        }
        else {
            std::array<S, 4> s{};
            for (std::size_t k = 0; k < Taps; ++k) {
                s[k % 4] = s[k % 4] + h[k] * window[Taps - 1 - k];
            }
            return (s[0] + s[1]) + (s[2] + s[3]);
        }
    }

    template <typename S, std::size_t Taps>
    void dot_is_the_cards_sum() {
        const auto           h = taps<S, Taps>(3u + Taps);
        std::vector<S>       window(Taps);
        catenae_test::signal noise(17u + Taps);
        for (std::uint32_t trial = 0; trial < 20; ++trial) {
            for (S& x : window) {
                x = noise.next<S>();
            }
            const S got  = catenae::detail::fir_dot<S, Taps>(h.data(), window.data());
            const S want = reference_dot<S, Taps>(h, window.data());
            ASSERT_EQ(std::memcmp(&got, &want, sizeof(S)), 0) << Taps << " taps, trial " << trial;
        }
    }

    // Verifies CAT-FIR-KERNEL-001.
    TYPED_TEST(fir_kernel_formats, TheDotIsTheCardsSum) {
        dot_is_the_cards_sum<TypeParam, 1>();
        dot_is_the_cards_sum<TypeParam, 2>();
        dot_is_the_cards_sum<TypeParam, 3>();
        dot_is_the_cards_sum<TypeParam, 4>();
        dot_is_the_cards_sum<TypeParam, 5>();
        dot_is_the_cards_sum<TypeParam, 7>();
        dot_is_the_cards_sum<TypeParam, 63>();
        dot_is_the_cards_sum<TypeParam, 64>();
    }

    // Verifies CAT-FIR-KERNEL-001.
    TEST(fir_kernel, AFloatSumIsNotOneAscendingSum) {
        // The order is the design (D36): the four sums round differently from one running sum,
        // which a window of 1 then 2^-24 eight times shows. One sum loses every small product;
        // the partial sums keep the six that never meet the 1.
        std::array<float, 9> h{};
        h.fill(1.0f);
        std::array<float, 9> x{};
        x.fill(0x1p-24f);
        x[8]                = 1.0f; // the newest, met by h[0], in s0
        const float y       = catenae::detail::fir_dot<float, 9>(h.data(), x.data());
        float       one_sum = 0.0f;
        for (std::size_t k = 0; k < 9; ++k) {
            one_sum += h[k] * x[8 - k];
        }
        EXPECT_EQ(one_sum, 1.0f);
        EXPECT_EQ(y, 1.0f + 0x1.8p-22f); // s1, s2 and s3 hold 2^-23 each when they meet s0's 1
    }

    /// Per channel, the window of the last Taps inputs through fir_dot, frame after frame.
    template <typename S, std::size_t Taps, std::size_t Ch>
    std::vector<S> by_frames(const std::array<coeff<S>, Taps>& h, const std::vector<S>& in) {
        std::vector<S>    out(in.size());
        const std::size_t frames = in.size() / Ch;
        for (std::size_t c = 0; c < Ch; ++c) {
            std::vector<S> window(Taps, S{0});
            for (std::size_t f = 0; f < frames; ++f) {
                for (std::size_t j = 0; j + 1 < Taps; ++j) {
                    window[j] = window[j + 1];
                }
                window[Taps - 1] = in[f * Ch + c];
                out[f * Ch + c]  = catenae::detail::fir_dot<S, Taps>(h.data(), window.data());
            }
        }
        return out;
    }

    /// The block kernel over calls of the given sizes, in place when asked.
    template <typename S, std::size_t Taps, std::size_t Ch>
    std::vector<S> by_blocks(const std::array<coeff<S>, Taps>& h, const std::vector<S>& in,
                             const std::vector<std::size_t>& sizes, bool in_place) {
        std::vector<S> out = in;
        std::vector<S> lines(Ch * 2 * Taps, S{0});
        std::uint32_t  write = 0;
        std::size_t    at    = 0;
        for (const std::size_t n : sizes) {
            const S* from = in_place ? out.data() + at * Ch : in.data() + at * Ch;
            catenae::detail::fir_block<S, Taps, Ch>(h.data(), lines.data(), write, from, n, out.data() + at * Ch);
            at += n;
            EXPECT_LT(write, Taps);
            EXPECT_EQ(write, at % Taps);
        }
        return out;
    }

    template <typename S, std::size_t Taps, std::size_t Ch>
    void block_is_frame_by_frame() {
        constexpr std::size_t frames = 300;
        catenae_test::signal  noise(7u + static_cast<std::uint32_t>(Taps * 16 + Ch));
        std::vector<S>        in(frames * Ch);
        for (S& v : in) {
            v = noise.next<S>();
        }
        const auto h    = taps<S, Taps>(5u + Taps);
        const auto once = by_frames<S, Taps, Ch>(h, in);
        for (const std::uint32_t seed : {1u, 2u}) {
            const std::vector<std::size_t> sizes = catenae_test::chunks(frames, 2 * Taps + 3, seed);
            for (const bool in_place : {false, true}) {
                const auto blocks = by_blocks<S, Taps, Ch>(h, in, sizes, in_place);
                ASSERT_EQ(std::memcmp(blocks.data(), once.data(), once.size() * sizeof(S)), 0)
                    << Taps << " taps, " << Ch << " channels, seed " << seed << (in_place ? ", in place" : "");
            }
        }
    }

    // Verifies CAT-FIR-KERNEL-002.
    TYPED_TEST(fir_kernel_formats, TheBlockIsTheFrameKernelFrameAfterFrame) {
        block_is_frame_by_frame<TypeParam, 1, 1>();
        block_is_frame_by_frame<TypeParam, 1, 2>();
        block_is_frame_by_frame<TypeParam, 2, 3>();
        block_is_frame_by_frame<TypeParam, 5, 2>();
        block_is_frame_by_frame<TypeParam, 8, 1>();
        block_is_frame_by_frame<TypeParam, 31, 3>();
    }

} // namespace
