/// @file test_mailbox.cpp
/// @brief mailbox's contract (include/catenae/mailbox.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstdint>
#include <type_traits>

#if defined(CATENAE_TEST_THREADS)
#include <atomic>
#include <memory>
#include <thread>
#endif

#include <gtest/gtest.h>

#include "catenae/compiler.h"
#include "catenae/mailbox.h"

namespace {

    // A static mailbox is built by its constant constructor: no start-up code.
    CATENAE_CONSTINIT catenae::mailbox<float> s_level;

    /// A value with no default constructor, as a design is: only placement new can begin its life
    /// in a slot.
    struct design {
        explicit constexpr design(float cutoff_in, std::uint32_t generation_in) noexcept
            : cutoff(cutoff_in)
            , generation(generation_in) {}
        float         cutoff;
        std::uint32_t generation;
    };
    static_assert(!std::is_default_constructible<design>::value && std::is_trivially_copyable<design>::value,
                  "a design: trivially copyable, with no default constructor");

    // Verifies CAT-MAILBOX-001.
    TEST(mailbox, TheNewestWinsAndNobodyWaits) {
        catenae::mailbox<float> box;
        float                   got   = 0.0f;
        int                     calls = 0;
        const auto              apply = [&](const float& v) noexcept {
            got = v;
            ++calls;
        };
        EXPECT_FALSE(box.take(apply)); // it starts empty
        box.post(1.0f);
        box.post(2.0f); // replaces the value never taken
        EXPECT_TRUE(box.take(apply));
        EXPECT_EQ(got, 2.0f);
        EXPECT_FALSE(box.take(apply)); // nothing new
        EXPECT_EQ(calls, 1);
        box.post(3.0f);
        EXPECT_TRUE(box.take(apply));
        EXPECT_EQ(got, 3.0f);
        box.post(4.0f);
        box.reset(); // empty again
        EXPECT_FALSE(box.take(apply));
        EXPECT_EQ(calls, 2);
        s_level.post(0.5f);
        EXPECT_TRUE(s_level.take(apply));
        EXPECT_EQ(got, 0.5f);
    }

    // Verifies CAT-MAILBOX-001: every arrangement of the three slots, many times round.
    TEST(mailbox, EveryPostIsTakenOnceAndInTurn) {
        catenae::mailbox<std::uint32_t> box;
        std::uint32_t                   last = 0;
        for (std::uint32_t round = 1; round <= 300; ++round) {
            // Post one, two or three values, then take: only the newest arrives.
            const std::uint32_t posts = round % 3 + 1;
            for (std::uint32_t i = 0; i < posts; ++i) {
                box.post(round * 10 + i);
            }
            ASSERT_TRUE(box.take([&](const std::uint32_t& v) noexcept { last = v; }));
            ASSERT_EQ(last, round * 10 + posts - 1);
            ASSERT_FALSE(box.take([&](const std::uint32_t& v) noexcept { last = v; }));
        }
    }

    // Verifies CAT-MAILBOX-003: a value with no default constructor is posted and taken whole.
    TEST(mailbox, APostConstructsTheValue) {
        catenae::mailbox<design> box;
        box.post(design{0.25f, 7});
        float         cutoff     = 0.0f;
        std::uint32_t generation = 0;
        EXPECT_TRUE(box.take([&](const design& d) noexcept {
            cutoff     = d.cutoff;
            generation = d.generation;
        }));
        EXPECT_EQ(cutoff, 0.25f);
        EXPECT_EQ(generation, 7u);
    }

#if defined(CATENAE_TEST_THREADS)
    /// 64 bytes, every word the same: a torn value has two different words.
    struct reading {
        std::array<std::uint32_t, 16> words;
    };

    // Verifies CAT-MAILBOX-002: two million 64-byte values between two threads, none torn and
    // none out of order, under the thread sanitizer's leg as well as natively.
    TEST(mailbox, TwoThreadsNoneTornNoneOutOfOrder) {
        auto                    box   = std::make_unique<catenae::mailbox<reading>>();
        constexpr std::uint32_t posts = 2000000;
        std::atomic<bool>       done{false};
        std::thread             poster([&] {
            for (std::uint32_t n = 1; n <= posts; ++n) {
                reading r{};
                r.words.fill(n);
                box->post(r);
            }
            done.store(true);
        });
        std::uint32_t           last  = 0;
        std::uint32_t           torn  = 0;
        std::uint32_t           order = 0;
        std::uint32_t           taken = 0;
        const auto              check = [&](const reading& r) noexcept {
            for (const std::uint32_t w : r.words) {
                torn += w == r.words[0] ? 0u : 1u;
            }
            order += r.words[0] > last ? 0u : 1u;
            last = r.words[0];
            ++taken;
        };
        while (!done.load()) {
            static_cast<void>(box->take(check));
        }
        static_cast<void>(box->take(check)); // the last post, if it arrived after the last look
        poster.join();
        EXPECT_GT(taken, 0u);
        EXPECT_EQ(last, posts);
        EXPECT_EQ(torn, 0u);
        EXPECT_EQ(order, 0u);
    }
#endif

} // namespace
