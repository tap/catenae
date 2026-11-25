/// @file test_harness.cpp
/// @brief The generic tests on the fixture stages, and on broken ones that each must catch.
// Copyright 2026 Timothy Place and the catenae contributors
//
// From C1 every stage and chain runs catenae_test/harness.h. Here it proves itself: the four
// fixtures pass every test with their numbers exact, and for each test a stage broken in the
// one way that test exists to find fails it, while passing the others where it can.
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/stage_traits.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/harness.h"

namespace {

    using catenae::ratio;
    using catenae_fixtures::block_stage;
    using catenae_fixtures::down2_first;
    using catenae_fixtures::down2_last;
    using catenae_fixtures::up2_toy;

    /// A 1:1 running sum, its sum the state: `Forgets` leaves the sum out of reset.
    template <bool Forgets>
    class running_sum {
      public:
        using input_port  = catenae::port<float, 1>;
        using output_port = catenae::port<float, 1>;
        static constexpr ratio       rate_ratio{1, 1};
        static constexpr std::size_t input_group = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum    = 1;
        static constexpr bool        is_in_place       = false;
        static constexpr bool        has_exact_latency = false;

        struct state {
            float         sum;
            std::uint32_t frames;
        };
        using workspace = catenae::no_workspace;

        void reset(state& st) const noexcept {
            if (!Forgets) {
                st.sum = 0.0f;
            }
            st.frames = 0;
        }
        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                st.sum += in[i];
                out[i] = st.sum;
            }
            st.frames += static_cast<std::uint32_t>(in.size());
            return in.size();
        }
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

    /// 1:1, adding the number of calls so far to each frame: its output depends on the chunking.
    class call_counter : public running_sum<false> {
      public:
        static constexpr bool rests_at_zero = true;
        struct state {
            std::uint32_t calls;
        };
        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                out[i] = in[i] + static_cast<float>(st.calls);
            }
            ++st.calls;
            return in.size();
        }
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
    };

    // Broken numbers on a correct block stage of 4: each derived type changes one.
    struct claims_group_2 : block_stage<1, 4> {
        static constexpr std::size_t input_group = 2;
    };
    struct understates_shortfall : block_stage<1, 4> {
        static constexpr ratio max_shortfall{2, 1};
    };
    struct overstates_shortfall : block_stage<1, 4> {
        static constexpr ratio max_shortfall{4, 1};
    };
    struct claims_quantum_3 : block_stage<1, 4> {
        static constexpr std::size_t output_quantum = 3;
    };
    struct claims_quantum_2 : block_stage<1, 4> {
        static constexpr std::size_t output_quantum = 2;
    };
    struct miscounts : block_stage<1, 4> {
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            return (st.count + n + 1) / 4 * 4;
        }
    };
    struct needs_too_much : block_stage<1, 4> {
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            return k == 0 ? 0 : (k + 3) / 4 * 4 - st.count + 4;
        }
    };
    struct bounds_too_low : block_stage<1, 4> {
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n / 4 * 4; }
    };
    // A 2:1 that writes on a pair's last frame but claims to lead: the trail goes unbounded.
    struct claims_to_lead : down2_last<1> {
        static constexpr ratio max_shortfall{0, 1};
        static constexpr ratio max_lead{1, 2};
    };

} // namespace

namespace catenae {
    template <bool Forgets>
    struct is_stage<running_sum<Forgets>> : std::true_type {};
} // namespace catenae

namespace {

    template <typename Stage>
    void expect_all_pass(const Stage& stage) {
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
    }

    // Verifies CAT-STAGE-009, CAT-STAGE-010, CAT-STAGE-011 and CAT-STAGE-012.
    TEST(harness, FixturesPassWithExactNumbers) {
        expect_all_pass(block_stage<1, 4>{});
        expect_all_pass(block_stage<2, 3>{});
        expect_all_pass(block_stage<1, 12>{});
        expect_all_pass(up2_toy<2>{});
        expect_all_pass(down2_first<1>{});
        expect_all_pass(down2_last<2>{});
        expect_all_pass(running_sum<false>{});
    }

    // Verifies CAT-STAGE-011.
    TEST(harness, ResetTestCatchesAForgottenMember) {
        EXPECT_FALSE(catenae_test::reset_ignores_storage(running_sum<true>{}));
        EXPECT_TRUE(catenae_test::counts_are_exact(running_sum<true>{}));
    }

    // Verifies CAT-STAGE-009.
    TEST(harness, CountTestCatchesEachWrongCount) {
        EXPECT_FALSE(catenae_test::counts_are_exact(miscounts{}));
        EXPECT_FALSE(catenae_test::counts_are_exact(needs_too_much{}));
        EXPECT_FALSE(catenae_test::counts_are_exact(bounds_too_low{}));
    }

    // Verifies CAT-STAGE-010.
    TEST(harness, ArrivalTestCatchesEachWrongNumber) {
        EXPECT_FALSE(catenae_test::arrival_numbers_hold(claims_group_2{}, false));
        EXPECT_FALSE(catenae_test::arrival_numbers_hold(understates_shortfall{}, false));
        EXPECT_FALSE(catenae_test::arrival_numbers_hold(claims_quantum_3{}, false));
        EXPECT_FALSE(catenae_test::arrival_numbers_hold(claims_to_lead{}, false));
        // A bound that holds but is loose passes, and fails only where exactness is asked.
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(overstates_shortfall{}, false));
        EXPECT_FALSE(catenae_test::arrival_numbers_hold(overstates_shortfall{}, true));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(claims_quantum_2{}, false));
        EXPECT_FALSE(catenae_test::arrival_numbers_hold(claims_quantum_2{}, true));
    }

    // Verifies CAT-STAGE-012.
    TEST(harness, ChunkingTestCatchesCallDependentOutput) {
        EXPECT_FALSE(catenae_test::chunking_is_bit_exact(call_counter{}));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(call_counter{}));
        EXPECT_TRUE(catenae_test::counts_are_exact(call_counter{}));
    }

} // namespace
