/// @file test_version.cpp
/// @brief The version's contract (include/catenae/version.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/version.h"

#ifndef CATENAE_TEST_PROJECT_VERSION_MAJOR
#error "the build passes the project version as CATENAE_TEST_PROJECT_VERSION_*"
#endif

namespace {

    // Verifies CAT-VERSION-001.
    TEST(version, HeaderIsTheBuildsVersion) {
        EXPECT_EQ(CATENAE_VERSION_MAJOR, CATENAE_TEST_PROJECT_VERSION_MAJOR);
        EXPECT_EQ(CATENAE_VERSION_MINOR, CATENAE_TEST_PROJECT_VERSION_MINOR);
        EXPECT_EQ(CATENAE_VERSION_PATCH, CATENAE_TEST_PROJECT_VERSION_PATCH);
    }

    // Verifies CAT-VERSION-002.
    TEST(version, PackedIsMajorMinorPatch) {
        static_assert(std::is_unsigned<decltype(CATENAE_VERSION)>::value, "CATENAE_VERSION is unsigned");
        constexpr std::uint32_t packed = CATENAE_VERSION;
        static_assert(packed == CATENAE_VERSION_MAJOR * 65536u + CATENAE_VERSION_MINOR * 256u + CATENAE_VERSION_PATCH,
                      "CATENAE_VERSION packs major, minor and patch");
        EXPECT_EQ(packed / 65536u, static_cast<std::uint32_t>(CATENAE_VERSION_MAJOR));
        EXPECT_EQ((packed / 256u) % 256u, static_cast<std::uint32_t>(CATENAE_VERSION_MINOR));
        EXPECT_EQ(packed % 256u, static_cast<std::uint32_t>(CATENAE_VERSION_PATCH));
    }

    // Verifies CAT-VERSION-003.
    TEST(version, CompiledCoreIsTheseHeaders) {
        EXPECT_EQ(catenae::version(), CATENAE_VERSION);
    }

} // namespace
