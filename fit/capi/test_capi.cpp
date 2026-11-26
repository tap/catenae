/// @file test_capi.cpp
/// @brief The fit milestone's first C ABI manifest (PLAN.md section 7.5): what its entries
/// export, what they check, and that a C caller gets what the chain gives.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "async/junction.h"
#include "bridge/bridge.h"
#include "capi/catenae_c.h"
#include "capi/catenae_c_consumer.h"
#include "catenae/compiler.h"
#include "rational/chain.h"
#include "strip.h"

namespace {

    constexpr std::size_t k_frames = 64;

    TEST(fit_capi, TheManifestCountsItsEntriesAndFunctions) {
        // Four chain entries of nine functions, one junction of ten, one design of three, one
        // maker, two setters, and the two counts.
        EXPECT_EQ(catenae_c_entries(), 5u);
        EXPECT_EQ(catenae_c_functions(), 54u);
    }

    TEST(fit_capi, EachTargetsSizesFitTheManifestsCapacities) {
        EXPECT_LE(catenae_c_strip_configuration_size(), std::size_t{catenae_c_strip_configuration_capacity});
        EXPECT_LE(catenae_c_strip_state_size(), std::size_t{catenae_c_strip_state_capacity});
        EXPECT_LE(catenae_c_strip_workspace_size(), std::size_t{catenae_c_strip_workspace_capacity});
        EXPECT_LE(catenae_c_strip_state_align(), std::size_t{catenae_c_strip_state_alignment});
        EXPECT_EQ(catenae_c_strip_state_size(), sizeof(catenae::state_of<example::strip_t>));
        EXPECT_EQ(catenae_c_strip_workspace_size(), sizeof(catenae::workspace_of<example::strip_t>));
        EXPECT_LE(catenae_c_strip_mono_state_size(), std::size_t{catenae_c_strip_mono_state_capacity});
    }

    TEST(fit_capi, AWrongSizeIsAStatusNotTheHandler) {
        CATENAE_C_CONFIGURATION_STORAGE(strip, configuration);
        CATENAE_C_STATE_STORAGE(strip, state);
        CATENAE_C_WORKSPACE_STORAGE(strip, workspace);
        const std::size_t size = catenae_c_strip_configuration_size();
        ASSERT_EQ(catenae_c_strip_configuration_copy(configuration, sizeof configuration), CATENAE_C_OK);
        // A configuration of another size, as a header built for another target would pass.
        EXPECT_EQ(catenae_c_strip_init(configuration, size + 4, state, sizeof state), CATENAE_C_WRONG_SIZE);
        EXPECT_EQ(catenae_c_strip_init(configuration, size, state, catenae_c_strip_state_size() - 1),
                  CATENAE_C_WRONG_SIZE);
        EXPECT_EQ(catenae_c_strip_init(nullptr, size, state, sizeof state), CATENAE_C_NULL);
        EXPECT_EQ(catenae_c_strip_init(configuration, size, state + 1, sizeof state - 1), CATENAE_C_MISALIGNED);
        EXPECT_EQ(catenae_c_strip_configuration_copy(configuration, size - 1), CATENAE_C_WRONG_SIZE);
        ASSERT_EQ(catenae_c_strip_init(configuration, size, state, sizeof state), CATENAE_C_OK);
        std::array<float, 2 * k_frames> in{};
        std::array<float, 2 * k_frames> out{};
        std::size_t                     written = 0;
        EXPECT_EQ(catenae_c_strip_process(configuration, size, state, sizeof state, workspace, sizeof workspace,
                                          in.data(), in.size() - 1, out.data(), out.size(), &written),
                  CATENAE_C_NOT_WHOLE_FRAMES);
        EXPECT_EQ(catenae_c_strip_process(configuration, size, state, sizeof state, workspace, sizeof workspace,
                                          in.data(), in.size(), out.data(), out.size() - 2, &written),
                  CATENAE_C_OUTPUT_TOO_SMALL);
        EXPECT_EQ(catenae_c_strip_set_level(configuration, size - 1, 0.5f), CATENAE_C_WRONG_SIZE);
    }

    TEST(fit_capi, ACCallerGetsWhatTheChainGives) {
        std::array<float, 2 * k_frames> in{};
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = static_cast<float>(i % 17) * 0.0625f - 0.5f;
        }
        // Through C: c_consumer.c, compiled as C11.
        std::array<float, 2 * k_frames> through_c{};
        std::size_t                     written = 0;
        ASSERT_EQ(catenae_c_consumer_run(0.5f, in.data(), in.size(), through_c.data(), through_c.size(), &written),
                  CATENAE_C_OK);
        EXPECT_EQ(written, in.size());
        // In C++: the same strip, its level set the same way.
        auto                                        strip = example::strip;
        static catenae::memory_of<example::strip_t> memory{};
        strip.stage<0>().set_level(0.5f);
        strip.reset(memory.state);
        std::array<float, 2 * k_frames> direct{};
        static_cast<void>(strip.process(memory, in, direct));
        EXPECT_EQ(std::memcmp(through_c.data(), direct.data(), sizeof direct), 0);
    }

    /// A ramp in [-0.5, 0.5).
    template <std::size_t N>
    std::array<float, N> ramp() {
        std::array<float, N> in{};
        for (std::size_t i = 0; i < N; ++i) {
            in[i] = static_cast<float>(i % 23) / 23.0f - 0.5f;
        }
        return in;
    }

    TEST(fit_capi, TheRationalConverterThroughCIsTheChain) {
        CATENAE_C_CONFIGURATION_STORAGE(rational_3_2, configuration);
        CATENAE_C_STATE_STORAGE(rational_3_2, state);
        CATENAE_C_WORKSPACE_STORAGE(rational_3_2, workspace);
        const std::size_t size = catenae_c_rational_3_2_configuration_size();
        ASSERT_EQ(catenae_c_rational_3_2_configuration_copy(configuration, sizeof configuration), CATENAE_C_OK);
        ASSERT_EQ(catenae_c_rational_3_2_init(configuration, size, state, sizeof state), CATENAE_C_OK);
        const auto                in = ramp<2 * 64>();
        std::array<float, 2 * 96> out{};
        std::size_t               written = 0;
        ASSERT_EQ(catenae_c_rational_3_2_process(configuration, size, state, sizeof state, workspace, sizeof workspace,
                                                 in.data(), in.size(), out.data(), out.size(), &written),
                  CATENAE_C_OK);
        using converter_t = catenae_fit::rational::converter<float, 2, catenae::ratio_t<3, 2>>;
        constexpr auto                         chain = converter_t{};
        static catenae::memory_of<converter_t> memory{};
        chain.reset(memory.state);
        std::array<float, 2 * 96> direct{};
        const std::size_t made = chain.process(memory, catenae::span<const float>(in), catenae::span<float>(direct));
        EXPECT_EQ(written, made * 2);
        EXPECT_EQ(std::memcmp(out.data(), direct.data(), sizeof direct), 0);
    }

    TEST(fit_capi, ABridgeDesignIsMadeIntoTheCallersStorage) {
        CATENAE_C_TABLE_STORAGE(bridge_48k, table);
        CATENAE_C_SCRATCH_STORAGE(bridge_48k, scratch);
        CATENAE_C_CONFIGURATION_STORAGE(bridge_48k, configuration);
        CATENAE_C_STATE_STORAGE(bridge_48k, state);
        CATENAE_C_WORKSPACE_STORAGE(bridge_48k, workspace);
        const std::size_t size = catenae_c_bridge_48k_configuration_size();
        EXPECT_LE(catenae_c_bridge_48k_table_size(), sizeof table);
        // What make() cannot meet is a status; short storage is the wrong size.
        EXPECT_EQ(catenae_c_bridge_48k_make(table, sizeof table, scratch, sizeof scratch, configuration,
                                            sizeof configuration, 24000.0, 40.0),
                  CATENAE_C_REJECTED);
        EXPECT_EQ(catenae_c_bridge_48k_make(table, catenae_c_bridge_48k_table_size() - 4, scratch, sizeof scratch,
                                            configuration, sizeof configuration, 10000.0, 40.0),
                  CATENAE_C_WRONG_SIZE);
        ASSERT_EQ(catenae_c_bridge_48k_make(table, sizeof table, scratch, sizeof scratch, configuration,
                                            sizeof configuration, 10000.0, 40.0),
                  CATENAE_C_OK);
        ASSERT_EQ(catenae_c_bridge_48k_init(configuration, size, state, sizeof state), CATENAE_C_OK);
        const auto                 in = ramp<2 * 147>();
        std::array<float, 2 * 160> out{};
        std::size_t                written = 0;
        ASSERT_EQ(catenae_c_bridge_48k_process(configuration, size, state, sizeof state, workspace, sizeof workspace,
                                               in.data(), in.size(), out.data(), out.size(), &written),
                  CATENAE_C_OK);
        EXPECT_EQ(written, 2u * 160u);
        // The shipped configuration runs its placeholder table: a different filter, the same count.
        ASSERT_EQ(catenae_c_bridge_48k_configuration_copy(configuration, sizeof configuration), CATENAE_C_OK);
        ASSERT_EQ(catenae_c_bridge_48k_init(configuration, size, state, sizeof state), CATENAE_C_OK);
        std::array<float, 2 * 160> placeholder{};
        ASSERT_EQ(catenae_c_bridge_48k_process(configuration, size, state, sizeof state, workspace, sizeof workspace,
                                               in.data(), in.size(), placeholder.data(), placeholder.size(), &written),
                  CATENAE_C_OK);
        EXPECT_EQ(written, 2u * 160u);
        EXPECT_NE(std::memcmp(out.data(), placeholder.data(), sizeof out), 0);
    }

    TEST(fit_capi, TheAsyncJunctionPushesAndPullsThroughC) {
        using junction_t = catenae_fit::async::junction<2, 1024, 64>;
        CATENAE_C_CONFIGURATION_STORAGE(async_2, configuration);
        CATENAE_C_STATE_STORAGE(async_2, state);
        CATENAE_C_REPORT_STORAGE(async_2, report);
        const std::size_t size = catenae_c_async_2_configuration_size();
        // A target the ring cannot hold is refused; one it can is made.
        EXPECT_EQ(catenae_c_async_2_make(configuration, sizeof configuration, 100000u), CATENAE_C_REJECTED);
        ASSERT_EQ(catenae_c_async_2_make(configuration, sizeof configuration, 96u), CATENAE_C_OK);
        ASSERT_EQ(catenae_c_async_2_init(configuration, size, state, sizeof state), CATENAE_C_OK);
        const auto                in = ramp<2 * 64>();
        std::array<float, 2 * 64> out{};
        std::size_t               moved = 0;
        std::size_t               real  = 0;
        std::size_t               total = 0;
        for (int block = 0; block < 64; ++block) {
            ASSERT_EQ(catenae_c_async_2_push(configuration, size, state, sizeof state, in.data(), in.size(), &moved),
                      CATENAE_C_OK);
            ASSERT_EQ(catenae_c_async_2_pull(configuration, size, state, sizeof state, out.data(), out.size(), &real),
                      CATENAE_C_OK);
            total += real;
        }
        EXPECT_GT(total, 0u); // it filled, primed and paid out
        // A push above the largest is a status, not the handler.
        std::array<float, 2 * 65> too_many{};
        EXPECT_EQ(
            catenae_c_async_2_push(configuration, size, state, sizeof state, too_many.data(), too_many.size(), &moved),
            CATENAE_C_WRONG_SIZE);
        ASSERT_EQ(catenae_c_async_2_report_size(), sizeof(junction_t::report_type));
        ASSERT_EQ(catenae_c_async_2_report(configuration, size, state, sizeof state, report, sizeof report),
                  CATENAE_C_OK);
        junction_t::report_type seen{};
        std::memcpy(&seen, report, sizeof seen);
        EXPECT_EQ(seen.underruns, 0u);
        EXPECT_EQ(seen.target_frames, 96u);
    }

} // namespace
