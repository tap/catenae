/// @file test_mix.cpp
/// @brief mix's contract (include/catenae/mix.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/mix.h"
#include "catenae/table_view.h"
#include "catenae_test/harness.h"

namespace {

    template <typename M>
    std::vector<float> run_mix(const M& m, typename M::state& st, const std::vector<float>& in) {
        typename M::workspace ws{};
        std::vector<float>    out(in.size() / M::input_port::channels * M::output_port::channels);
        static_cast<void>(m.run(st, ws, catenae::span<const float>(in), catenae::span<float>(out)));
        return out;
    }

    // Verifies CAT-MIX-001.
    TEST(mix, EachOutputIsItsRowsWeightedSum) {
        // A stereo pair to mono at one half each.
        const catenae::mix<2, 1>  mono{{0.5f, 0.5f}, 0};
        catenae::mix<2, 1>::state s1{};
        catenae::stage_traits<catenae::mix<2, 1>>::reset(mono, s1);
        EXPECT_EQ(run_mix(mono, s1, {1.0f, 0.5f, -0.25f, 0.25f}), (std::vector<float>{0.75f, 0.0f}));
        // A mono input to two outputs, one inverted.
        const catenae::mix<1, 2>  wide{{1.0f, -1.0f}, 0};
        catenae::mix<1, 2>::state s2{};
        catenae::stage_traits<catenae::mix<1, 2>>::reset(wide, s2);
        EXPECT_EQ(run_mix(wide, s2, {0.5f, -0.125f}), (std::vector<float>{0.5f, -0.5f, -0.125f, 0.125f}));
        // Three inputs to two outputs, every coefficient distinct: the sum over i ascending,
        // in float, bit for bit.
        const std::array<float, 6> m = {0.1f, 0.2f, 0.3f, -0.7f, 1.1f, 0.05f};
        const catenae::mix<3, 2>   three{m, 0};
        catenae::mix<3, 2>::state  s3{};
        catenae::stage_traits<catenae::mix<3, 2>>::reset(three, s3);
        const std::vector<float> in  = {0.9f, -0.3f, 0.7f, 0.11f, 0.13f, -0.17f};
        const std::vector<float> out = run_mix(three, s3, in);
        for (std::size_t f = 0; f < 2; ++f) {
            for (std::size_t o = 0; o < 2; ++o) {
                float sum = 0.0f;
                for (std::size_t i = 0; i < 3; ++i) {
                    sum += m[o * 3 + i] * in[f * 3 + i];
                }
                EXPECT_EQ(out[f * 2 + o], sum) << f << ", " << o;
            }
        }
        EXPECT_EQ(three.at(1, 2), 0.05f);
        EXPECT_TRUE(catenae_test::reset_ignores_storage(three));
        EXPECT_TRUE(catenae_test::counts_are_exact(three));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(three, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(three));
    }

    // Verifies CAT-MIX-002.
    TEST(mix, ANewMatrixIsReachedOverTheRamp) {
        using m_t = catenae::mix<2, 2>;
        m_t        m{{1.0f, 0.0f, 0.0f, 1.0f}, 8};
        m_t::state st{};
        catenae::stage_traits<m_t>::reset(m, st);
        m.set_matrix({0.0f, 1.0f, 1.0f, 0.0f}); // swap the channels, over 8 frames
        // Input (1, 0) every frame: output 0 is m[0][0], output 1 is m[1][0].
        std::vector<float> in(2 * 12);
        for (std::size_t f = 0; f < 12; ++f) {
            in[2 * f] = 1.0f;
        }
        const std::vector<float> out  = run_mix(m, st, in);
        const float              down = (0.0f - 1.0f) / 8.0f; // m[0][0]'s step
        const float              up   = (1.0f - 0.0f) / 8.0f; // m[1][0]'s step
        for (std::uint32_t k = 1; k <= 8; ++k) {
            EXPECT_EQ(out[2 * (k - 1)], 0.0f - down * static_cast<float>(8 - k)) << k;
            EXPECT_EQ(out[2 * (k - 1) + 1], 1.0f - up * static_cast<float>(8 - k)) << k;
        }
        EXPECT_EQ(out[2 * 7], 0.0f); // frame R applies the goal exactly
        EXPECT_EQ(out[2 * 7 + 1], 1.0f);
        EXPECT_EQ(out[2 * 11 + 1], 1.0f);
        // A ramp of no frames jumps.
        m_t jump{{1.0f, 0.0f, 0.0f, 1.0f}, 0};
        catenae::stage_traits<m_t>::reset(jump, st);
        jump.set_matrix({0.0f, 1.0f, 1.0f, 0.0f});
        EXPECT_EQ(run_mix(jump, st, {1.0f, 0.0f}), (std::vector<float>{0.0f, 1.0f}));
        // A ramp across calls is the same in any chunking: one call of 100 against calls of 1 to 9.
        m_t            r{{0.25f, 0.5f, 0.75f, 1.0f}, 37};
        m_t::state     a{};
        m_t::state     b{};
        m_t::workspace ws{};
        catenae::stage_traits<m_t>::reset(r, a);
        catenae::stage_traits<m_t>::reset(r, b);
        r.set_matrix({-1.0f, 0.5f, 0.125f, 2.0f});
        std::vector<float> ramp_in(2 * 100);
        for (std::size_t i = 0; i < ramp_in.size(); ++i) {
            ramp_in[i] = static_cast<float>(i % 13) * 0.0625f - 0.4f;
        }
        std::vector<float> whole(ramp_in.size());
        std::vector<float> pieces(ramp_in.size());
        static_cast<void>(r.run(a, ws, catenae::span<const float>(ramp_in), catenae::span<float>(whole)));
        std::size_t at = 0;
        for (const std::size_t n : catenae_test::chunks(100, 9, 3)) {
            static_cast<void>(r.run(b, ws, catenae::span<const float>(ramp_in.data() + 2 * at, 2 * n),
                                    catenae::span<float>(pieces.data() + 2 * at, 2 * n)));
            at += n;
        }
        EXPECT_EQ(std::memcmp(whole.data(), pieces.data(), whole.size() * sizeof(float)), 0);
    }

    // Verifies CAT-MIX-003.
    TEST(mix, ALargeMatrixIsHeldByView) {
        static_assert(catenae::mix<8, 8>::holds_by_value && sizeof(catenae::mix<8, 8>) <= 512, "8 x 8 by value");
        static_assert(!catenae::holds_view<catenae::mix<8, 8>>::value, "and it holds no view");
        using big_t = catenae::mix<16, 16>; // 1,024 bytes of float
        static_assert(!big_t::holds_by_value && catenae::holds_view<big_t>::value, "16 x 16 by view");
        static_assert(sizeof(big_t) <= 16, "a view and a ramp length");
        static_assert(catenae::holds_view<catenae::chain<big_t>>::value, "and so does its chain");
        static_assert(std::is_constructible<big_t, const big_t::matrix_type&, std::uint32_t>::value, "a table");
        static_assert(!std::is_constructible<big_t, big_t::matrix_type, std::uint32_t>::value, "not a temporary");
        static big_t::matrix_type table{}; // the identity, in the caller's storage
        for (std::size_t k = 0; k < 16; ++k) {
            table[k * 16 + k] = 1.0f;
        }
        const big_t        big{table, 0};
        big_t::state       st{};
        std::vector<float> in(16 * 3);
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = static_cast<float>(i) * 0.01f;
        }
        catenae::stage_traits<big_t>::reset(big, st);
        EXPECT_EQ(run_mix(big, st, in), in);
        // The caller's table is read in place: a change to it ramps in at the next call.
        table[0] = 0.5f;
        EXPECT_EQ(run_mix(big, st, in)[0], 0.0f); // input 0 of frame 0 is 0
        EXPECT_EQ(run_mix(big, st, in)[16], in[16] * 0.5f);
    }

} // namespace
