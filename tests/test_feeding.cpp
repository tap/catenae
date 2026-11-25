/// @file test_feeding.cpp
/// @brief feeding's and fed_by's contracts (include/catenae/feeding.h, include/catenae/fed_by.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/delay.h"
#include "catenae/fed_by.h"
#include "catenae/feeding.h"
#include "catenae/gain.h"
#include "catenae/handoff.h"
#include "catenae_fixtures/edges.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

// A named namespace: a junction of the battery's own, which records each pull's size.
namespace feeding_fixture {

    /// The fixture ring, recording the size of every pull it is asked for.
    template <std::size_t Ch, std::size_t Capacity>
    struct recording_ring : catenae_fixtures::ring_junction<Ch, Capacity> {
        using base = catenae_fixtures::ring_junction<Ch, Capacity>;
        static std::vector<std::size_t>& pulls() {
            static std::vector<std::size_t> log;
            return log;
        }
        std::size_t pull(typename base::state& st, catenae::span<float> out) const noexcept {
            pulls().push_back(out.size() / Ch);
            return base::pull(st, out);
        }
    };

} // namespace feeding_fixture

namespace catenae {
    template <std::size_t Ch, std::size_t Capacity>
    struct is_junction<feeding_fixture::recording_ring<Ch, Capacity>> : std::true_type {};
} // namespace catenae

namespace {

    using catenae::operator|;
    using catenae_fixtures::block_stage;
    using catenae_fixtures::ring_junction;
    using catenae_fixtures::up2_toy;
    using feeding_fixture::recording_ring;

    std::vector<float> ramp_input(std::size_t values) {
        std::vector<float> in(values);
        for (std::size_t i = 0; i < values; ++i) {
            in[i] = static_cast<float>(i % 101) * 0.01f - 0.5f;
        }
        return in;
    }

    /// A chain run alone over `in` in one call, from reset.
    template <typename C>
    std::vector<float> alone(const C& c, const std::vector<float>& in) {
        constexpr std::size_t in_ch  = C::input_port::values_per_frame;
        constexpr std::size_t out_ch = C::output_port::values_per_frame;
        auto                  m      = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> out(C::max_outputs_for(in.size() / in_ch) * out_ch);
        const std::size_t  made = c.process(*m, catenae::span<const float>(in), catenae::span<float>(out));
        out.resize(made * out_ch);
        return out;
    }

    // Verifies CAT-FEEDING-001.
    TEST(feeding, PushesWhatTheChainWrites) {
        constexpr auto chain = catenae::gain<2>{0.5f, 0} | catenae::delay<2, 3>{};
        using chain_t        = std::remove_cv_t<decltype(chain)>;
        const ring_junction<2, 1024> ring{};
        auto                         ring_state = std::make_unique<ring_junction<2, 1024>::state>();
        auto                         st         = std::make_unique<catenae::feeding_state<chain_t>>();
        auto                         ws         = std::make_unique<catenae::feeding_workspace<chain_t>>();
        chain.reset(*st);
        const auto in = ramp_input(2 * 200);
        // 200 frames: three blocks of 64 and one of 8, each pushed as it is written.
        EXPECT_EQ(catenae::feeding(ring, *ring_state, chain, *st, *ws, catenae::span<const float>(in)), 200u);
        std::vector<float> pulled(2 * 200);
        EXPECT_EQ(ring.pull(*ring_state, catenae::span<float>(pulled)), 200u);
        EXPECT_TRUE(catenae_test::bit_equal(pulled, alone(chain, in)));

        // A chain that writes in blocks: what it holds back reaches the junction later, in order.
        constexpr auto blocks = catenae::chain(block_stage<2, 48>{});
        using blocks_t        = std::remove_cv_t<decltype(blocks)>;
        auto ring_state2      = std::make_unique<ring_junction<2, 1024>::state>();
        auto st2              = std::make_unique<catenae::feeding_state<blocks_t>>();
        auto ws2              = std::make_unique<catenae::feeding_workspace<blocks_t>>();
        blocks.reset(*st2);
        EXPECT_EQ(catenae::feeding(ring, *ring_state2, blocks, *st2, *ws2, catenae::span<const float>(in)), 192u);
        std::vector<float> held(2 * 192);
        EXPECT_EQ(ring.pull(*ring_state2, catenae::span<float>(held)), 192u);
        auto owed = alone(blocks, in);
        EXPECT_TRUE(catenae_test::bit_equal(held, owed));
    }

    /// Requests of the sizes given, through fed_by from a ring that holds `in`: the frames
    /// delivered, end to end, and the pulls each request made.
    template <std::size_t MaxRequest, typename C>
    std::vector<float> pull_in_requests(const C& c, const std::vector<float>& in, const std::vector<std::size_t>& sizes,
                                        std::vector<std::vector<std::size_t>>& pulls_made) {
        using chain_t = std::remove_cv_t<C>;
        using ring_t  = recording_ring<1, 4096>;
        const ring_t ring{};
        auto         ring_state = std::make_unique<ring_t::state>();
        static_cast<void>(ring.push(*ring_state, catenae::span<const float>(in)));
        auto st = std::make_unique<catenae::fed_by_state<chain_t>>();
        auto ws = std::make_unique<catenae::fed_by_workspace<chain_t, MaxRequest>>();
        reset(c, *st);
        std::vector<float> out;
        for (const std::size_t n : sizes) {
            ring_t::pulls().clear();
            std::vector<float> part(n);
            EXPECT_EQ(catenae::fed_by(ring, *ring_state, c, *st, *ws, catenae::span<float>(part)), n);
            out.insert(out.end(), part.begin(), part.end());
            pulls_made.push_back(ring_t::pulls());
        }
        return out;
    }

    // Verifies CAT-FED-BY-001.
    TEST(fed_by, DeliversExactlyWhatWasAsked) {
        constexpr auto                        up4   = up2_toy<1>{} | up2_toy<1>{};
        const auto                            in    = ramp_input(1000);
        const std::vector<std::size_t>        sizes = {37, 102, 1, 64, 255, 3, 100, 256, 17};
        std::vector<std::vector<std::size_t>> pulls;
        const auto                            out  = pull_in_requests<256>(up4, in, sizes, pulls);
        auto                                  owed = alone(up4, in);
        owed.resize(out.size());
        EXPECT_TRUE(catenae_test::bit_equal(out, owed));
    }

    // Verifies CAT-FED-BY-002.
    TEST(fed_by, MakesAtMostOnePullARequest) {
        std::vector<std::vector<std::size_t>> pulls;
        static_cast<void>(
            pull_in_requests<64>(catenae::chain(block_stage<1, 128>{}), ramp_input(1024), {64, 64, 64, 64}, pulls));
        const std::vector<std::vector<std::size_t>> behind_a_block = {{128}, {}, {128}, {}};
        EXPECT_EQ(pulls, behind_a_block);

        pulls.clear();
        constexpr auto up4 = up2_toy<1>{} | up2_toy<1>{};
        static_cast<void>(pull_in_requests<1024>(up4, ramp_input(1024), {102, 102, 102, 102, 700}, pulls));
        const std::vector<std::vector<std::size_t>> up_by_four = {{26}, {25}, {26}, {25}, {175}};
        EXPECT_EQ(pulls, up_by_four);
    }

    // Verifies CAT-FED-BY-003.
    TEST(fed_by, TheLargestPullStarvedAndTheBoundary) {
        constexpr auto block = catenae::chain(block_stage<1, 128>{});
        constexpr auto up4   = up2_toy<1>{} | up2_toy<1>{};
        constexpr auto level = catenae::chain(catenae::gain<1>{0.5f, 0});
        static_assert(catenae::fed_by_workspace<decltype(block), 64>::largest_pull == 191, "64 + 127, at 1:1");
        static_assert(catenae::fed_by_workspace<decltype(up4), 700>::largest_pull == 175, "700 / 4");
        static_assert(catenae::fed_by_workspace<decltype(level), 256>::largest_pull == 256, "256, frame by frame");

        // From an empty handoff every input frame is silence, and `starved` counts it.
        using link_t = catenae::handoff<float, 1, 1024, 320, 64>;
        const link_t link{};
        auto         link_state = std::make_unique<link_t::state>();
        auto         st         = std::make_unique<catenae::fed_by_state<decltype(level)>>();
        auto         ws         = std::make_unique<catenae::fed_by_workspace<decltype(level), 256>>();
        reset(level, *st);
        std::vector<float> out(256);
        EXPECT_EQ(catenae::fed_by(link, *link_state, level, *st, *ws, catenae::span<float>(out)), 256u);
        EXPECT_EQ(st->starved, 256u);
        std::vector<float> too_many(257);
        CATENAE_EXPECT_CHECK(
            catenae::check::request_too_large,
            static_cast<void>(catenae::fed_by(link, *link_state, level, *st, *ws, catenae::span<float>(too_many))));
    }

} // namespace
