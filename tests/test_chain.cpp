/// @file test_chain.cpp
/// @brief The chain's contract (include/catenae/chain.h, docs/contracts/chain.md).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/convert.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/interleave.h"
#include "catenae/map.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

namespace {

    using catenae::chain;
    using catenae::ratio;
    using catenae::stage_traits;
    using catenae::operator|;
    using catenae_fixtures::block_stage;
    using catenae_fixtures::down2_first;
    using catenae_fixtures::down2_last;
    using catenae_fixtures::up2_toy;

    template <typename C>
    using traits_of = stage_traits<std::remove_cv_t<C>>;

    struct halve {
        constexpr float operator()(float x) const noexcept { return x * 0.5f; }
    };

    constexpr bool same_terms(ratio a, ratio b) noexcept {
        return a.up == b.up && a.down == b.down;
    }

    /// Runs a chain over `in` in calls of `call` frames (its last call shorter), from reset.
    template <typename C>
    std::vector<float> run_in_calls(const C& c, const std::vector<float>& in, std::size_t call) {
        constexpr std::size_t in_ch  = C::input_port::values_per_frame;
        constexpr std::size_t out_ch = C::output_port::values_per_frame;
        auto                  m      = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> out;
        const std::size_t  frames = in.size() / in_ch;
        for (std::size_t at = 0; at < frames; at += call) {
            const std::size_t  n = std::min(call, frames - at);
            std::vector<float> part(C::max_outputs_for(n) * out_ch);
            const std::size_t  made = c.process(*m, catenae::span<const float>(in.data() + at * in_ch, n * in_ch),
                                                catenae::span<float>(part));
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(made * out_ch));
        }
        return out;
    }

    std::vector<float> ramp_input(std::size_t values) {
        std::vector<float> in(values);
        for (std::size_t i = 0; i < values; ++i) {
            in[i] = static_cast<float>(i % 97) * 0.01f - 0.4f;
        }
        return in;
    }

    // Verifies CAT-CHAIN-001.
    TEST(chain, PipeFlattensAndKeepsABlockedChainWhole) {
        constexpr auto a  = up2_toy<1>{} | down2_first<1>{};
        constexpr auto b  = block_stage<1, 4>{} | down2_last<1>{};
        constexpr auto ab = a | b;
        static_assert(std::is_same<std::remove_cv_t<decltype(ab)>,
                                   chain<up2_toy<1>, down2_first<1>, block_stage<1, 4>, down2_last<1>>>::value,
                      "(a | b) | (c | d) is chain<a, b, c, d>");
        static_assert(std::is_same<std::remove_cv_t<decltype(chain(a))>, std::remove_cv_t<decltype(a)>>::value,
                      "chain of a chain is that chain");
        static_assert(std::is_same<decltype(chain(up2_toy<1>{})), chain<up2_toy<1>>>::value,
                      "chain(s) is a chain of one");
        constexpr auto blocked = catenae::with_block<16>(a) | b;
        static_assert(
            std::is_same<
                std::remove_cv_t<decltype(blocked)>,
                chain<catenae::blocked_chain<16, up2_toy<1>, down2_first<1>>, block_stage<1, 4>, down2_last<1>>>::value,
            "a blocked chain is one stage to |");
        static_assert(catenae::is_chain<std::remove_cv_t<decltype(blocked)>>::value
                          && catenae::is_stage<std::remove_cv_t<decltype(blocked)>>::value,
                      "a chain is a stage and a chain");
        static_assert(traits_of<decltype(ab)>::opted_in, "the traits read a chain");
        SUCCEED();
    }

    // Verifies CAT-CHAIN-002 and CAT-STAGE-004.
    TEST(chain, ArrivalNumbersCompose) {
        using lead_only  = decltype(down2_first<1>{} | up2_toy<1>{});
        using trail_only = decltype(down2_last<1>{} | up2_toy<1>{});
        static_assert(lead_only::input_group == 2 && same_terms(lead_only::max_lead, ratio{1, 1})
                          && same_terms(lead_only::max_shortfall, ratio{0, 1}),
                      "down by 2 writing on a pair's first frame, then up by 2: group 2, a frame ahead");
        static_assert(trail_only::input_group == 2 && same_terms(trail_only::max_shortfall, ratio{1, 1})
                          && same_terms(trail_only::max_lead, ratio{0, 1}),
                      "writing on its last: a frame behind");
        using up_down = decltype(up2_toy<1>{} | down2_first<1>{});
        static_assert(up_down::input_group == 1 && up_down::is_in_place == false, "up then down: group 1");
        static_assert(up_down::is_frame_synchronous && !lead_only::is_frame_synchronous
                          && stage_traits<lead_only>::is_frame_synchronous == lead_only::is_frame_synchronous,
                      "a chain's own flag is derived from its composed group, as the traits derive it");
        static_assert(same_terms(up_down::rate_ratio, ratio{1, 1}), "1:1");
        using b96_128  = decltype(block_stage<1, 96>{} | block_stage<1, 128>{});
        using b480_512 = decltype(block_stage<1, 480>{} | block_stage<1, 512>{});
        using b7_5     = decltype(block_stage<1, 7>{} | block_stage<1, 5>{});
        static_assert(same_terms(b96_128::max_shortfall, ratio{191, 1}) && b96_128::input_group == 384,
                      "96 before 128 trails by 191");
        static_assert(same_terms(b480_512::max_shortfall, ratio{959, 1}), "480 before 512 by 959");
        static_assert(same_terms(b7_5::max_shortfall, ratio{10, 1}) && b7_5::input_group == 35, "7 before 5 by 10");
        static_assert(b96_128::output_quantum == 128, "the quantum leaving the last stage");
        // A nested chain is composed through: the same numbers as the flat one.
        using nested = chain<chain<block_stage<1, 96>>, block_stage<1, 128>>;
        static_assert(same_terms(nested::max_shortfall, b96_128::max_shortfall)
                          && nested::input_group == b96_128::input_group,
                      "a nested chain is not a leaf");
        using decimate4 = decltype(down2_first<1>{} | down2_first<1>{});
        static_assert(same_terms(decimate4::rate_ratio, ratio{1, 4}) && decimate4::input_group == 4, "by 4");
        SUCCEED();
    }

    // Verifies CAT-CHAIN-003.
    TEST(chain, ChainsPassTheGenericTests) {
        constexpr auto lead_only = down2_first<2>{} | up2_toy<2>{};
        constexpr auto blocks    = block_stage<1, 7>{} | block_stage<1, 5>{};
        constexpr auto mixed     = up2_toy<1>{} | block_stage<1, 3>{} | down2_last<1>{};
        constexpr auto stages    = catenae::gain<2>{0.5f, 8} | catenae::delay<2, 5>{} | catenae::map<2, halve>{halve{}};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(lead_only));
        EXPECT_TRUE(catenae_test::counts_are_exact(lead_only));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(lead_only, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(lead_only));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(blocks));
        EXPECT_TRUE(catenae_test::counts_are_exact(blocks));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(blocks, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(blocks));
        EXPECT_TRUE(catenae_test::counts_are_exact(mixed));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(mixed, false));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(mixed));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stages));
        EXPECT_TRUE(catenae_test::counts_are_exact(stages));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stages, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stages));
    }

    // Verifies CAT-CHAIN-004.
    TEST(chain, ProcessChecksTheCallersBuffers) {
        constexpr auto c = catenae::gain<2>{1.0f, 0} | block_stage<2, 4>{};
        auto           m = std::make_unique<catenae::memory_of<decltype(c)>>();
        c.reset(m->state);
        std::array<float, 16> in{};
        std::array<float, 16> out{};
        CATENAE_EXPECT_CHECK(
            catenae::check::input_not_whole_frames,
            static_cast<void>(c.process(*m, catenae::span<const float>(in.data(), 7), catenae::span<float>(out))));
        CATENAE_EXPECT_CHECK(catenae::check::output_too_small,
                             static_cast<void>(c.process(*m, catenae::span<const float>(in.data(), 8),
                                                         catenae::span<float>(out.data(), 6))));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(
            c.process(*m, catenae::span<const float>(in.data(), 8), catenae::span<float>(out.data(), 8))));
        // Three frames in write nothing yet, so an output of no room is enough.
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(
            c.process(*m, catenae::span<const float>(in.data(), 6), catenae::span<float>(out.data(), 0))));
        auto never_reset = std::make_unique<catenae::memory_of<decltype(c)>>();
        if constexpr (catenae::detail::k_audit_level) {
            CATENAE_EXPECT_CHECK(catenae::check::state_not_reset,
                                 static_cast<void>(c.process(*never_reset, catenae::span<const float>(in.data(), 8),
                                                             catenae::span<float>(out))));
        }
        // The trusted run checks nothing.
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(
            c.run(m->state, m->workspace, catenae::span<const float>(in.data(), 8), catenae::span<float>(out))));
    }

    // Verifies CAT-CHAIN-005.
    TEST(chain, InPlaceStagesShareOneBuffer) {
        using all_in_place = decltype(catenae::gain<2>{1.0f, 0} | catenae::delay<2, 3>{} | catenae::gain<2>{2.0f, 0});
        static_assert(all_in_place::is_in_place, "a chain of in-place stages is in place");
        static_assert(std::is_same<decltype(all_in_place::workspace::buffers), catenae::detail::pack<>>::value,
                      "and holds no buffer: the first writes the caller's output, the rest over it");
        using burst_then_run = decltype(block_stage<2, 4>{} | catenae::gain<2>{1.0f, 0} | catenae::delay<2, 3>{});
        static_assert(std::is_same<decltype(burst_then_run::workspace::buffers), catenae::detail::pack<>>::value,
                      "a trailing run of in-place stages writes over the caller's output");
        using run_between =
            decltype(block_stage<2, 4>{} | catenae::gain<2>{1.0f, 0} | catenae::gain<2>{1.0f, 0} | block_stage<2, 4>{});
        static_assert(std::is_same<decltype(run_between::workspace::buffers),
                                   catenae::detail::pack<std::array<float, 64 * 2>>>::value,
                      "a run of in-place stages between two others shares the buffer its input is in");
        // In place, the caller's input and output the same values: the same output.
        constexpr all_in_place   c{catenae::gain<2>{0.5f, 0}, catenae::delay<2, 3>{}, catenae::gain<2>{2.0f, 0}};
        const std::vector<float> in       = ramp_input(2 * 200);
        const std::vector<float> separate = run_in_calls(c, in, 64);
        std::vector<float>       same     = in;
        auto                     m        = std::make_unique<catenae::memory_of<all_in_place>>();
        c.reset(m->state);
        const std::size_t made = c.process(*m, catenae::span<const float>(same), catenae::span<float>(same));
        EXPECT_EQ(made, 200u);
        EXPECT_EQ(std::memcmp(same.data(), separate.data(), same.size() * sizeof(float)), 0);
        // The run between shares, and writes what four separate buffers would.
        constexpr run_between r{block_stage<2, 4>{}, catenae::gain<2>{0.25f, 0}, catenae::gain<2>{4.0f, 0},
                                block_stage<2, 4>{}};
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(r));
        const std::vector<float> through = run_in_calls(r, in, 64);
        ASSERT_EQ(through.size(), in.size());
        EXPECT_EQ(std::memcmp(through.data(), in.data(), in.size() * sizeof(float)), 0);
    }

    // Verifies CAT-CHAIN-006.
    TEST(chain, FlushIsResumableZeroPadding) {
        constexpr auto c = block_stage<1, 4>{} | catenae::delay<1, 5>{} | up2_toy<1>{} | block_stage<1, 3>{};
        using chain_t    = std::remove_cv_t<decltype(c)>;
        const std::size_t        tail = c.practical_tail_frames();
        const std::vector<float> in   = ramp_input(37);
        // The reference: the input padded with the tail's zeros, run straight through.
        std::vector<float> padded = in;
        padded.resize(in.size() + tail, 0.0f);
        const std::vector<float> straight = run_in_calls(c, padded, 1000);
        // The same input, then a flush into an output of one burst at a time.
        auto m = std::make_unique<catenae::memory_of<chain_t>>();
        c.reset(m->state);
        std::vector<float> got(chain_t::max_outputs_for(in.size()));
        std::size_t        made = c.process(*m, catenae::span<const float>(in), catenae::span<float>(got));
        got.resize(made);
        EXPECT_EQ(c.flush_remaining(m->state), tail);
        std::vector<float> burst(chain_t::max_outputs_for(1));
        std::size_t        calls = 0;
        while (c.flush_remaining(m->state) > 0) {
            const std::size_t n = c.flush(*m, catenae::span<float>(burst));
            got.insert(got.end(), burst.begin(), burst.begin() + static_cast<std::ptrdiff_t>(n));
            ASSERT_LT(++calls, 10000u);
        }
        ASSERT_EQ(got.size(), straight.size());
        EXPECT_EQ(std::memcmp(got.data(), straight.data(), got.size() * sizeof(float)), 0);
        EXPECT_GT(calls, 1u);
        // After the tail, more zeros write only silence: no residue.
        std::vector<float>       more(chain_t::max_outputs_for(64));
        const std::vector<float> zeros(64, 0.0f);
        const std::size_t        extra = c.process(*m, catenae::span<const float>(zeros), catenae::span<float>(more));
        for (std::size_t i = 0; i < extra; ++i) {
            EXPECT_EQ(std::memcmp(&more[i], &zeros[0], sizeof(float)), 0) << i;
        }
        // A process after a partial flush carries on as after that many zeros, and reset clears.
        c.reset(m->state);
        std::vector<float> partial(chain_t::max_outputs_for(in.size()));
        made = c.process(*m, catenae::span<const float>(in), catenae::span<float>(partial));
        partial.resize(made);
        const std::size_t first = c.flush(*m, catenae::span<float>(burst));
        partial.insert(partial.end(), burst.begin(), burst.begin() + static_cast<std::ptrdiff_t>(first));
        const std::size_t fed = tail - c.flush_remaining(m->state);
        EXPECT_GT(fed, 0u);
        const std::vector<float> after = ramp_input(11);
        std::vector<float>       rest(chain_t::max_outputs_for(after.size()));
        made = c.process(*m, catenae::span<const float>(after), catenae::span<float>(rest));
        partial.insert(partial.end(), rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(made));
        EXPECT_EQ(c.flush_remaining(m->state), tail);
        std::vector<float> zero_padded = in;
        zero_padded.resize(in.size() + fed, 0.0f);
        zero_padded.insert(zero_padded.end(), after.begin(), after.end());
        const std::vector<float> expect = run_in_calls(c, zero_padded, 1000);
        ASSERT_EQ(partial.size(), expect.size());
        EXPECT_EQ(std::memcmp(partial.data(), expect.data(), partial.size() * sizeof(float)), 0);
        c.reset(m->state);
        EXPECT_EQ(c.flush_remaining(m->state), tail);
        CATENAE_EXPECT_CHECK(catenae::check::flush_output_too_small,
                             static_cast<void>(c.flush(*m, catenae::span<float>(burst.data(), 0))));
    }

    // Verifies CAT-CHAIN-007.
    TEST(chain, StagesAndTheirStatesByPosition) {
        auto c = catenae::gain<2>{1.0f, 4} | catenae::delay<2, 2>{};
        auto m = std::make_unique<catenae::memory_of<decltype(c)>>();
        c.reset(m->state);
        EXPECT_FLOAT_EQ(c.stage<0>().level(), 1.0f);
        c.stage<0>().set_level(0.5f);
        std::array<float, 16> in{};
        in.fill(1.0f);
        std::array<float, 16> out{};
        static_cast<void>(c.process(*m, in, out));
        const auto& g = decltype(c)::stage_state<0>(m->state);
        EXPECT_FLOAT_EQ(g.current, 0.5f);
        EXPECT_EQ(g.remaining, 0u);
        EXPECT_FLOAT_EQ(out[4], 1.0f - 0.5f / 4.0f); // frame 2 out is frame 0 in, the ramp's first
        EXPECT_EQ(decltype(c)::stage_state<1>(m->state).write, 0u);
    }

    // Verifies CAT-CHAIN-014.
    TEST(chain, ADefaultChainWhenEveryStageHasOne) {
        using c_t = chain<catenae::delay<2, 3>, up2_toy<2>, catenae::delay<2, 5>>;
        constexpr c_t c{}; // constexpr: the stages' defaults are
        static_assert(std::is_default_constructible<catenae::blocked_chain<32, catenae::delay<2, 3>>>::value,
                      "a blocked chain too");
        static_assert(!std::is_default_constructible<chain<catenae::gain<2>, catenae::delay<2, 3>>>::value,
                      "a gain has no default level, so its chain has no default");
        // The chain of the stages' default configurations.
        constexpr auto           spelled = catenae::delay<2, 3>{} | up2_toy<2>{} | catenae::delay<2, 5>{};
        const std::vector<float> in      = ramp_input(2 * 200);
        const std::vector<float> a       = run_in_calls(c, in, 64);
        const std::vector<float> b       = run_in_calls(spelled, in, 64);
        ASSERT_EQ(a.size(), b.size());
        EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)), 0);
    }

    // Verifies CAT-CHAIN-008.
    TEST(chain, LatencyComposesAtTheOutputRate) {
        constexpr auto c = catenae::delay<1, 3>{} | up2_toy<1>{} | catenae::delay<1, 5>{};
        using c_t        = std::remove_cv_t<decltype(c)>;
        static_assert(c_t::has_exact_latency && same_terms(c_t::exact_latency, ratio{23, 2}),
                      "3 frames at the input rate are 6 at the output, then 1/2, then 5");
        EXPECT_FLOAT_EQ(c.latency().nominal, 11.5f);
        EXPECT_FLOAT_EQ(c.latency().deviation, 0.0f);
        using mixed = decltype(catenae::delay<1, 3>{} | catenae_fixtures::block_stage<1, 2>{});
        static_assert(mixed::has_exact_latency, "block stages here state an exact latency of 0");
    }

    // Verifies CAT-CHAIN-009.
    TEST(chain, TailComposesBackward) {
        constexpr auto c = catenae::delay<1, 3>{} | up2_toy<1>{} | catenae::delay<1, 5>{};
        // delay 5: 5; up2: 0 + ceil(5 / 2) = 3; delay 3: 3 + 3 = 6.
        EXPECT_EQ(c.tail_frames(), 6u);
        EXPECT_EQ(c.practical_tail_frames(), 6u);
        EXPECT_FLOAT_EQ(c.flush_residue(), 0.0f);
        constexpr auto blocks = block_stage<1, 4>{} | catenae::delay<1, 2>{} | block_stage<1, 3>{};
        // block 3: 2; delay 2: 2 + 2 = 4; block 4: 3 + (4 + 3, its shortfall) = 10.
        EXPECT_EQ(blocks.tail_frames(), 10u);
        // A random input then that many zeros: whatever comes after is silence.
        using b_t = std::remove_cv_t<decltype(blocks)>;
        for (std::size_t length = 1; length < 30; ++length) {
            auto m = std::make_unique<catenae::memory_of<b_t>>();
            blocks.reset(m->state);
            std::vector<float> in = ramp_input(length);
            in.resize(length + blocks.tail_frames(), 0.0f);
            std::vector<float> out(b_t::max_outputs_for(in.size()));
            static_cast<void>(blocks.process(*m, catenae::span<const float>(in), catenae::span<float>(out)));
            const std::vector<float> zeros(48, 0.0f);
            std::vector<float>       after(b_t::max_outputs_for(48));
            const std::size_t made = blocks.process(*m, catenae::span<const float>(zeros), catenae::span<float>(after));
            for (std::size_t i = 0; i < made; ++i) {
                ASSERT_EQ(std::memcmp(&after[i], &zeros[0], sizeof(float)), 0) << length << " " << i;
            }
        }
    }

    // Verifies CAT-CHAIN-010.
    TEST(chain, ABlockedChainWorksInItsBlock) {
        constexpr auto c       = block_stage<2, 4>{} | catenae::gain<2>{0.5f, 3} | up2_toy<2>{};
        constexpr auto blocked = catenae::with_block<7>(c);
        using blocked_t        = std::remove_cv_t<decltype(blocked)>;
        static_assert(blocked_t::block == 7 && std::remove_cv_t<decltype(c)>::block == 64, "its own block");
        static_assert(sizeof(blocked_t::workspace) < sizeof(std::remove_cv_t<decltype(c)>::workspace),
                      "buffers sized from the block");
        const std::vector<float> in = ramp_input(2 * 301);
        EXPECT_EQ(run_in_calls(blocked, in, 300), run_in_calls(c, in, 300));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(blocked));
        constexpr auto rebased = catenae::with_block<64>(blocked);
        static_assert(std::remove_cv_t<decltype(rebased)>::block == 64, "and back");
    }

    struct plain_memory {
        using strip = decltype(catenae::gain<2>{1.0f, 96} | catenae::delay<2, 32>{});
        static catenae::memory_of<strip> s_memory;
    };
    CATENAE_CONSTINIT catenae::memory_of<plain_memory::strip> plain_memory::s_memory{};

    // Verifies CAT-CHAIN-011.
    TEST(chain, MemoryIsPlainStorage) {
        using strip = plain_memory::strip;
        static_assert(catenae::detail::k_is_plain_storage<strip::state>, "a chain's state is plain storage");
        static_assert(catenae::detail::k_is_plain_storage<strip::workspace>, "its workspace is too");
        static_assert(std::is_trivially_copyable<strip>::value, "its configuration is a plain value");
        EXPECT_EQ(plain_memory::s_memory.state.ready, 0u); // zero: not yet reset
    }

    // Verifies CAT-CHAIN-012.
    TEST(chain, PlanarPortsPassThrough) {
        constexpr auto split                = catenae::deinterleave<3>{} | chain(catenae::interleave<3>{});
        constexpr auto to_plane             = chain(catenae::deinterleave<3>{});
        using plane_t                       = std::remove_cv_t<decltype(to_plane)>;
        const std::vector<float> in         = ramp_input(3 * 150);
        const std::vector<float> round_trip = run_in_calls(split, in, 70);
        ASSERT_EQ(round_trip.size(), in.size());
        EXPECT_EQ(std::memcmp(round_trip.data(), in.data(), in.size() * sizeof(float)), 0);
        static_assert(std::is_same<decltype(std::remove_cv_t<decltype(split)>::workspace::buffers),
                                   catenae::detail::pack<std::array<float, 64 * 3>>>::value,
                      "a planar buffer between the two, its channels end to end");
        auto m = std::make_unique<catenae::memory_of<plane_t>>();
        to_plane.reset(m->state);
        std::array<std::array<float, 150>, 3> planes{};
        std::array<catenae::span<float>, 3>   out{planes[0], planes[1], planes[2]};
        EXPECT_EQ(to_plane.process(*m, catenae::span<const float>(in), out), 150u);
        EXPECT_FLOAT_EQ(planes[2][149], in[3 * 149 + 2]);
        EXPECT_FLOAT_EQ(planes[1][0], in[1]);
        std::array<catenae::span<float>, 3> uneven{planes[0], planes[1], catenae::span<float>(planes[2].data(), 10)};
        CATENAE_EXPECT_CHECK(catenae::check::output_too_small,
                             static_cast<void>(to_plane.process(*m, catenae::span<const float>(in), uneven)));
    }

} // namespace
