/// @file test_table_view.cpp
/// @brief table_view's and holds_view's contract (include/catenae/table_view.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/gain.h"
#include "catenae/parallel.h"
#include "catenae/steady.h"
#include "catenae/table_view.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/check_trap.h"

namespace {

    using catenae::holds_view;
    using catenae::table_view;
    using catenae_fixtures::table_gain;
    using view_t = table_view<float, 4>;

    constexpr std::array<float, 4> k_rom = {0.5f, 0.25f, -1.0f, 2.0f};

    // Verifies CAT-TABLE-VIEW-001.
    TEST(table_view, IsMadeOnlyFromStorageThatOutlivesIt) {
        static_assert(std::is_convertible<const std::array<float, 4>&, view_t>::value, "an array that lives on");
        static_assert(std::is_convertible<const float(&)[4], view_t>::value, "a built-in array that lives on");
        static_assert(!std::is_constructible<view_t, std::array<float, 4>>::value, "a temporary array: refused");
        static_assert(!std::is_constructible<view_t, const std::array<float, 4>&&>::value, "an expiring one too");
        static_assert(!std::is_constructible<view_t, const float(&&)[4]>::value, "a temporary built-in array");
        static_assert(!std::is_constructible<view_t, std::array<float, 3>&>::value, "another length");
        // Storage the caller wrote, which a temporary cannot give: explicitly.
        static_assert(std::is_constructible<view_t, catenae::span<float, 4>>::value, "written storage");
        static_assert(!std::is_convertible<catenae::span<float, 4>, view_t>::value, "explicitly");
        // A span of const values binds a temporary (as std::span does), so it is not accepted.
        static_assert(!std::is_constructible<view_t, catenae::span<const float, 4>>::value, "no span of const");
        // So a stage that takes a table_view refuses a temporary table by its parameter's type.
        static_assert(std::is_constructible<table_gain<2>, const std::array<float, 2>&>::value, "a stage's table");
        static_assert(!std::is_constructible<table_gain<2>, std::array<float, 2>>::value, "not a temporary one");
        std::array<float, 4> written{};
        const view_t         over(catenae::span<float, 4>{written});
        written[2] = 3.0f;
        EXPECT_EQ(over[2], 3.0f); // read in place, not copied
    }

    // Verifies CAT-TABLE-VIEW-002.
    TEST(table_view, IsAPointerReadInPlace) {
        static_assert(std::is_trivially_copyable<view_t>::value, "trivially copyable");
        static_assert(sizeof(view_t) == sizeof(const float*), "one pointer");
        constexpr view_t rom(k_rom);
        static_assert(rom.size() == 4 && view_t::extent == 4, "its length");
        constexpr float second = rom[1]; // read in a constant expression
        EXPECT_EQ(second, 0.25f);
        static_assert(rom.data() == k_rom.data(), "the table's own storage");
        static_assert(rom.values().size() == 4, "as a span");
        EXPECT_EQ(rom.values()[3], 2.0f);
        if constexpr (catenae::detail::k_audit_level) {
            CATENAE_EXPECT_CHECK(catenae::check::index_out_of_range, static_cast<void>(rom[4]));
        }
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(rom[3]));
    }

    // Verifies CAT-TABLE-VIEW-003.
    TEST(table_view, HoldingAViewIsSaidAndComposed) {
        static_assert(!holds_view<float>::value && !holds_view<catenae::gain<2>>::value, "false unless specialised");
        static_assert(holds_view<view_t>::value && holds_view<table_gain<2>>::value, "a view, and its stage");
        using strip_t = catenae::chain<catenae::gain<2>, table_gain<2>>;
        static_assert(holds_view<strip_t>::value, "a chain with such a stage");
        static_assert(!holds_view<catenae::chain<catenae::gain<2>, catenae::gain<2>>>::value, "one without");
        static_assert(holds_view<catenae::blocked_chain<32, table_gain<2>>>::value, "a blocked chain");
        static_assert(holds_view<catenae::chain<catenae::gain<2>, strip_t>>::value, "a chain in a chain");
        static_assert(holds_view<catenae::steady<table_gain<2>>>::value, "steady<>");
        static_assert(holds_view<catenae::parallel<catenae::gain<2>, table_gain<2>>>::value, "parallel<>, either arm");
        // The chain runs through the caller's table.
        static constexpr std::array<float, 2> levels = {0.5f, -2.0f};
        const auto                            c = catenae::chain(catenae::gain<2>{1.0f, 0}) | table_gain<2>{levels};
        static catenae::memory_of<std::remove_const_t<decltype(c)>> memory{};
        c.reset(memory.state);
        const std::array<float, 4> in = {1.0f, 1.0f, 3.0f, 0.25f};
        std::array<float, 4>       out{};
        EXPECT_EQ(c.process(memory, in, out), 2u);
        EXPECT_EQ(out[0], 0.5f);
        EXPECT_EQ(out[1], -2.0f);
        EXPECT_EQ(out[2], 1.5f);
        EXPECT_EQ(out[3], -0.5f);
    }

} // namespace
