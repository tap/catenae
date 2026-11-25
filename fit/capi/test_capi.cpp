/// @file test_capi.cpp
/// @brief The fit milestone's first C ABI manifest (PLAN.md section 7.5): what its entries
/// export, what they check, and that a C caller gets what the chain gives.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "capi/catenae_c.h"
#include "capi/catenae_c_consumer.h"
#include "catenae/compiler.h"
#include "strip.h"

namespace {

    constexpr std::size_t k_frames = 64;

    TEST(fit_capi, TheManifestCountsItsEntriesAndFunctions) {
        // Two entries of nine functions, two setters, and the two counts.
        EXPECT_EQ(catenae_c_entries(), 2u);
        EXPECT_EQ(catenae_c_functions(), 22u);
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

} // namespace
