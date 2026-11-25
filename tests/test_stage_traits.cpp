/// @file test_stage_traits.cpp
/// @brief stage_traits' contract (include/catenae/stage_traits.h), read on the fixture stages.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/stage_traits.h"
#include "catenae_fixtures/stages.h"

namespace {

    using catenae::ratio;
    using catenae::stage_traits;
    using catenae_fixtures::block_stage;
    using catenae_fixtures::down2_first;
    using catenae_fixtures::down2_last;
    using catenae_fixtures::up2_toy;

    constexpr bool same_terms(ratio a, ratio b) noexcept {
        return a.up == b.up && a.down == b.down;
    }

    /// A 1:1 gain in place, whose rest state is its configuration's level: a stage that writes
    /// reset, as PLAN.md section 9.1's does.
    class gain {
      public:
        using input_port  = catenae::port<float, 2>;
        using output_port = catenae::port<float, 2>;
        static constexpr ratio       rate_ratio{1, 1};
        static constexpr std::size_t input_group = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum    = 1;
        static constexpr bool        is_in_place       = true;
        static constexpr bool        has_exact_latency = true;
        static constexpr ratio       exact_latency{0, 1};

        struct state {
            float current; // the level now, moving toward the configuration's
        };
        using workspace = catenae::no_workspace;

        explicit gain(float level) noexcept
            : m_level(level) {}

        void        reset(state& st) const noexcept { st.current = m_level; }
        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                out[i] = in[i] * st.current;
            }
            return in.size() / 2;
        }
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }

      private:
        float m_level;
    };

    // Verifies CAT-STAGE-001: the traits read every declared number.
    TEST(stage_traits, ReadsTheDeclaredNumbers) {
        using block = stage_traits<block_stage<2, 4>>;
        static_assert(same_terms(block::rate_ratio, ratio{1, 1}) && block::input_group == 4, "a block stage");
        static_assert(same_terms(block::max_shortfall, ratio{3, 1}) && same_terms(block::max_lead, ratio{0, 1}),
                      "it trails by hop - 1");
        static_assert(block::output_quantum == 4 && !block::is_in_place, "it writes whole blocks");
        static_assert(block::has_exact_latency && same_terms(block::exact_latency, ratio{0, 1}), "no delay");
        static_assert(std::is_same<block::input_port, catenae::port<float, 2>>::value, "its ports");
        static_assert(std::is_same<block::workspace, catenae::no_workspace>::value, "its workspace");

        using up = stage_traits<up2_toy<1>>;
        static_assert(same_terms(up::rate_ratio, ratio{2, 1}) && up::output_quantum == 2, "a toy 1:2");
        static_assert(same_terms(up::exact_latency, ratio{1, 2}), "half an output frame of delay");

        using first = stage_traits<down2_first<1>>;
        using last  = stage_traits<down2_last<1>>;
        static_assert(same_terms(first::rate_ratio, ratio{1, 2}) && first::input_group == 2, "a toy 2:1");
        static_assert(same_terms(first::max_lead, ratio{1, 2}) && same_terms(first::max_shortfall, ratio{0, 1}),
                      "writing on a pair's first frame leads");
        static_assert(same_terms(last::max_shortfall, ratio{1, 2}) && same_terms(last::max_lead, ratio{0, 1}),
                      "writing on its last trails");
        static_assert(stage_traits<gain>::is_in_place, "a gain runs in place");
        SUCCEED();
    }

    // Verifies CAT-STAGE-004.
    TEST(stage_traits, FrameSynchronousIsDerived) {
        static_assert(stage_traits<up2_toy<2>>::is_frame_synchronous, "input_group 1");
        static_assert(stage_traits<gain>::is_frame_synchronous, "input_group 1");
        static_assert(!stage_traits<down2_first<2>>::is_frame_synchronous, "input_group 2");
        static_assert(!stage_traits<block_stage<1, 8>>::is_frame_synchronous, "input_group 8");
        SUCCEED();
    }

    // Verifies CAT-STAGE-002.
    TEST(stage_traits, StateAndWorkspaceArePlainStorage) {
        static_assert(catenae::detail::k_is_plain_storage<block_stage<2, 4>::state>, "a block's state");
        static_assert(catenae::detail::k_is_plain_storage<gain::state>, "a gain's state");
        static_assert(catenae::detail::k_is_plain_storage<catenae::no_workspace>, "no workspace");
        static_assert(std::is_empty<catenae::no_workspace>::value, "no_workspace is empty");
        SUCCEED();
    }

    // Verifies CAT-STAGE-003: a stage that rests at zero is zeroed in place, and one that writes
    // reset is reset by it, over whatever the storage held.
    TEST(stage_traits, ResetReachesTheRestState) {
        using block = block_stage<2, 4>;
        static_assert(stage_traits<block>::rests_at_zero && !stage_traits<gain>::rests_at_zero, "one way each");
        block::state st;
        std::memset(&st, 0xA5, sizeof st);
        stage_traits<block>::reset(block{}, st);
        const block::state zero{};
        EXPECT_EQ(std::memcmp(&st, &zero, sizeof st), 0);

        gain::state g;
        std::memset(&g, 0xA5, sizeof g);
        stage_traits<gain>::reset(gain{0.25f}, g);
        EXPECT_FLOAT_EQ(g.current, 0.25f);
    }

    // Verifies CAT-STAGE-005: a stage declared in place runs with its output over its input.
    TEST(stage_traits, InPlaceStageRunsOverItsInput) {
        const gain      g{0.5f};
        gain::state     st;
        gain::workspace none{};
        stage_traits<gain>::reset(g, st);
        float frames[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        EXPECT_EQ(gain::outputs_for(st, 2), 2u);
        EXPECT_EQ(gain::frames_needed(st, 2), 2u);
        EXPECT_EQ(g.run(st, none, catenae::span<const float>(frames), catenae::span<float>(frames)), 2u);
        EXPECT_FLOAT_EQ(frames[0], 0.5f);
        EXPECT_FLOAT_EQ(frames[3], 2.0f);
        EXPECT_FLOAT_EQ(g.latency().nominal, 0.0f);
        EXPECT_FLOAT_EQ(g.latency().deviation, 0.0f);
        EXPECT_EQ(g.tail_frames(), 0u);
        EXPECT_EQ(g.practical_tail_frames(), 0u);
        EXPECT_FLOAT_EQ(g.flush_residue(), 0.0f);
    }

    /// A block stage whose configuration is a table of exactly 512 bytes: the largest allowed.
    struct block_with_table : block_stage<1, 4> {
        float table[128];
    };

    // Verifies CAT-STAGE-007 and CAT-STAGE-008.
    TEST(stage_traits, ConfigurationIsSmallAndOptsIn) {
        EXPECT_EQ(catenae::max_configuration_bytes, 512u);
        static_assert(sizeof(block_with_table) == 512 && stage_traits<block_with_table>::input_group == 4,
                      "a configuration of exactly 512 bytes is a stage (one of 516 is not: tests/compile_fail)");
        static_assert(std::is_trivially_copyable<gain>::value && sizeof(gain) <= catenae::max_configuration_bytes,
                      "a gain's configuration");
        static_assert(stage_traits<block_stage<1, 4>>::opted_in && stage_traits<down2_last<1>>::opted_in,
                      "the fixtures opt in");
        static_assert(!stage_traits<gain>::opted_in, "a stage whose author did not specialise is_stage");
        static_assert(!catenae::is_chain<gain>::value, "nothing is a chain until C1 says so");
        SUCCEED();
    }

} // namespace
