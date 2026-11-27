/// @file test_planar_section.cpp
/// @brief planar_section's contract (include/catenae/planar_section.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "block_region.h"
#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/fft_fir.h"
#include "catenae/fir.h"
#include "catenae/gain.h"
#include "catenae/planar_section.h"
#include "catenae/resample.h"
#include "catenae/steady.h"
#include "catenae_test/harness.h"
#include "fft_fir_tables.h"
#include "fir_tables.h"

namespace {

    std::vector<float> noise(std::size_t values, std::uint32_t seed) {
        catenae_test::signal s(seed);
        std::vector<float>   v(values);
        for (float& x : v) {
            x = s.next<float>();
        }
        return v;
    }

    /// A configuration run from reset over `in`, in calls of `call` frames; what it wrote.
    template <typename C>
    std::vector<float> run_in(const C& c, const std::vector<float>& in, std::size_t call) {
        using traits                 = catenae::stage_traits<C>;
        constexpr std::size_t in_ch  = traits::input_port::values_per_frame;
        constexpr std::size_t out_ch = traits::output_port::values_per_frame;
        auto                  st     = std::make_unique<typename traits::state>();
        auto                  ws     = std::make_unique<typename traits::workspace>();
        traits::reset(c, *st);
        std::vector<float> out;
        const std::size_t  frames = in.size() / in_ch;
        for (std::size_t at = 0; at < frames; at += call) {
            const std::size_t  n = std::min(call, frames - at);
            std::vector<float> part(C::max_outputs_for(n) * out_ch);
            const std::size_t  w = c.run(*st, *ws, catenae::span<const float>(in.data() + at * in_ch, n * in_ch),
                                         catenae::span<float>(part));
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(w * out_ch));
        }
        return out;
    }

    /// Channel c of interleaved values.
    std::vector<float> channel(const std::vector<float>& v, std::size_t ch, std::size_t c) {
        std::vector<float> one(v.size() / ch);
        for (std::size_t f = 0; f < one.size(); ++f) {
            one[f] = v[f * ch + c];
        }
        return one;
    }

    bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }

    /// Each channel of the section's output is the mono chain over that channel alone.
    template <std::size_t Ch, typename... Stages>
    ::testing::AssertionResult each_channel_alone(const catenae::planar_section<Ch, Stages...>& section,
                                                  std::size_t frames, std::size_t call) {
        const std::vector<float> in  = noise(frames * Ch, 7u + static_cast<std::uint32_t>(call));
        const std::vector<float> out = run_in(section, in, call);
        for (std::size_t c = 0; c < Ch; ++c) {
            const std::vector<float> alone = run_in(section.mono(), channel(in, Ch, c), call);
            if (!same_bits(channel(out, Ch, c), alone)) {
                return ::testing::AssertionFailure() << "channel " << c << " in calls of " << call;
            }
        }
        return ::testing::AssertionSuccess();
    }

    // Verifies CAT-PLANAR-001.
    TEST(planar_section, EachChannelIsTheMonoChainAlone) {
        const catenae::planar_section<3, catenae::gain<1>, catenae::delay<1, 5>> strip(catenae::gain<1>{0.5f, 32},
                                                                                       catenae::delay<1, 5>{});
        const catenae::planar_section<2, catenae::up2<1>>                        up{catenae::up2<1>{}};
        const catenae::planar_section<2, example::block_region<16>>              blocks{example::block_region<16>{}};
        for (const std::size_t call : {std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{100}}) {
            EXPECT_TRUE(each_channel_alone(strip, 500, call));
            EXPECT_TRUE(each_channel_alone(up, 500, call));
            EXPECT_TRUE(each_channel_alone(blocks, 500, call));
        }
        // The generic tests: reset over any storage, the counts, the arrival numbers, any chunking.
        EXPECT_TRUE(catenae_test::reset_ignores_storage(strip));
        EXPECT_TRUE(catenae_test::counts_are_exact(strip));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(strip, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(strip));
        EXPECT_TRUE(catenae_test::counts_are_exact(up));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(up, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(up));
        EXPECT_TRUE(catenae_test::counts_are_exact(blocks));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(blocks, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(blocks));
    }

    // Verifies CAT-PLANAR-001.
    TEST(planar_section, ItIsTheInterleavedStageWhereItsKernelIsTheSame) {
        // Each stage's arithmetic is per channel, so its Ch-channel form and Ch mono forms agree
        // bit for bit, as the layout benchmark's pairs do.
        const std::vector<float> in = noise(256 * 6 * 2, 11);
        EXPECT_TRUE(same_bits(
            run_in(catenae::planar_section<2, catenae::fir<1, 63>>{catenae::fir<1, 63>{example::fir_lowpass}}, in, 64),
            run_in(catenae::fir<2, 63>{example::fir_lowpass}, in, 64)));
        EXPECT_TRUE(same_bits(run_in(catenae::planar_section<2, catenae::up2<1>>{catenae::up2<1>{}}, in, 37),
                              run_in(catenae::up2<2>{}, in, 37)));
        EXPECT_TRUE(same_bits(run_in(catenae::planar_section<2, catenae::fft_fir<1, 255>>{catenae::fft_fir<1, 255>{
                                         example::long_lowpass_fft}},
                                     in, 64),
                              run_in(catenae::fft_fir<2, 255>{example::long_lowpass_fft}, in, 64)));
        // In place, where the mono chain is, the same bits.
        const catenae::planar_section<2, catenae::gain<1>> halve{catenae::gain<1>{0.5f, 0}};
        static_assert(catenae::stage_traits<decltype(halve)>::is_in_place, "a gain is in place");
        std::vector<float> buffer = in;
        auto               st     = std::make_unique<decltype(halve)::state>();
        auto               ws     = std::make_unique<decltype(halve)::workspace>();
        halve.reset(*st);
        static_cast<void>(halve.run(*st, *ws, catenae::span<const float>(buffer), catenae::span<float>(buffer)));
        EXPECT_TRUE(same_bits(buffer, run_in(halve, in, in.size())));
    }

    // Verifies CAT-PLANAR-002.
    TEST(planar_section, ItsNumbersAreTheMonoChains) {
        using up     = catenae::stage_traits<catenae::planar_section<2, catenae::up2<1>>>;
        using up_one = catenae::stage_traits<catenae::chain<catenae::up2<1>>>;
        static_assert(up::rate_ratio == up_one::rate_ratio && up::input_group == up_one::input_group
                          && up::max_shortfall == up_one::max_shortfall && up::max_lead == up_one::max_lead
                          && up::output_quantum == up_one::output_quantum,
                      "a rate changer's arrival");
        static_assert(up::has_exact_latency && up::exact_latency == up_one::exact_latency, "its latency");
        static_assert(std::is_same<up::input_port, catenae::port<float, 2>>::value
                          && std::is_same<up::output_port, catenae::port<float, 2>>::value,
                      "two channels interleaved");
        using blocks = catenae::stage_traits<catenae::planar_section<4, catenae::fft_fir<1, 255>>>;
        static_assert(blocks::input_group == 256 && blocks::output_quantum == 256 && !blocks::is_frame_synchronous,
                      "a block stage's");
        static_assert(blocks::exact_latency.up == 127 && !blocks::is_in_place, "and its latency");
        static_assert(catenae::holds_view<catenae::planar_section<4, catenae::fft_fir<1, 255>>>::value,
                      "a view when a stage holds one");
        static_assert(!catenae::holds_view<catenae::planar_section<4, catenae::gain<1>>>::value, "none otherwise");
        using wrapped = catenae::stage_traits<catenae::steady<catenae::planar_section<2, catenae::fft_fir<1, 255>>>>;
        static_assert(wrapped::is_frame_synchronous && wrapped::exact_latency.up == 382, "in steady<>, as alone");
        const catenae::planar_section<2, catenae::gain<1>, catenae::delay<1, 9>> strip(catenae::gain<1>{0.5f, 0},
                                                                                       catenae::delay<1, 9>{});
        EXPECT_EQ(strip.tail_frames(), strip.mono().tail_frames());
        EXPECT_EQ(strip.practical_tail_frames(), 9u);
        EXPECT_FLOAT_EQ(strip.latency().nominal, 9.0f);
        EXPECT_FLOAT_EQ(strip.flush_residue(), 0.0f);
        // planar<Ch>() makes one from a mono chain.
        constexpr auto made = catenae::planar<3>(catenae::gain<1>{0.25f, 0} | catenae::delay<1, 2>{});
        static_assert(std::is_same<std::remove_cv_t<decltype(made)>,
                                   catenae::planar_section<3, catenae::gain<1>, catenae::delay<1, 2>>>::value,
                      "the chain's stages");
        // In a chain, as any stage.
        const auto in_a_chain = catenae::chain(made) | catenae::gain<3>{2.0f, 0};
        EXPECT_FLOAT_EQ(in_a_chain.latency().nominal, 2.0f);
    }

} // namespace
