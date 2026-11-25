/// @file test_junction_traits.cpp
/// @brief junction_traits' contract (include/catenae/junction_traits.h), on the toy ring.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/junction_traits.h"
#include "catenae_fixtures/edges.h"

namespace {

    using ring   = catenae_fixtures::ring_junction<2, 16>;
    using traits = catenae::junction_traits<ring>;

    // Verifies CAT-JUNCTION-001.
    TEST(junction_traits, ReadsTheDeclaredMembers) {
        static_assert(traits::clocking == catenae::clocks::shared, "shared clocks");
        static_assert(traits::rate_ratio.up == 1 && traits::rate_ratio.down == 1, "1:1");
        static_assert(traits::capacity_frames == 16, "its capacity");
        static_assert(std::is_same<traits::sink_port, catenae::port<float, 2>>::value, "what push takes");
        static_assert(traits::opted_in, "the toy opts in");
        SUCCEED();
    }

    // Verifies CAT-JUNCTION-002 and CAT-JUNCTION-005: the state is plain storage, at rest when
    // all zero, so a static one is ready as it stands; push and pull return the frames moved.
    TEST(junction_traits, ZeroStateIsAtRest) {
        static_assert(catenae::detail::k_is_plain_storage<traits::state>, "plain storage");
        const ring           j{};
        static traits::state shared; // zero, as a static is
        std::array<float, 8> in{1, 2, 3, 4, 5, 6, 7, 8};
        std::array<float, 8> out{};
        EXPECT_EQ(j.push(shared, catenae::span<const float>(in)), 4u);
        EXPECT_EQ(j.pull(shared, catenae::span<float>(out)), 4u);
        EXPECT_EQ(out, in);
        EXPECT_EQ(j.report(shared).fill, 0u);
        static_assert(noexcept(j.push(shared, catenae::span<const float>(in))) && noexcept(j.reset(shared))
                          && noexcept(j.restart(shared)),
                      "the two sides and the consumer's restart cannot throw");
    }

    // Verifies CAT-JUNCTION-004: serves() is the junction's, passed through, at compile time.
    TEST(junction_traits, ServesIsAConstantOfTheType) {
        static_assert(traits::serves(8, 4) && !traits::serves(16, 4), "the ring serves what fits beside a quantum");
        static_assert((traits::capacity_frames & (traits::capacity_frames - 1)) == 0, "a power of two");
        SUCCEED();
    }

} // namespace
