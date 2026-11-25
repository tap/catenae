/// @file test_compiler.cpp
/// @brief The compiler-extension header's contract (include/catenae/compiler.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstdint>
#include <type_traits>

#if defined(CATENAE_TEST_THREADS)
#include <thread>
#endif

#include <gtest/gtest.h>

#include "catenae/compiler.h"

namespace {

    /// Plain storage around a shared word, as a junction's state holds one.
    struct shared_words {
        std::uint32_t published; // the producer's word: the round it has written
        std::uint32_t acked;     // the consumer's word: the round it has read
        std::uint32_t payload[64];
    };

    // A static of plain storage, written with {}, is constant-initialized: in .bss, with no
    // start-up code. One that is not does not compile (tests/compile_fail/constinit_dynamic.cpp).
    CATENAE_CONSTINIT shared_words g_words{};

    // Verifies CAT-COMPILER-001.
    TEST(compiler, ConstinitAcceptsPlainStorage) {
        static_assert(std::is_trivially_default_constructible<shared_words>::value, "plain storage");
        EXPECT_EQ(g_words.published, 0u);
        EXPECT_EQ(g_words.payload[63], 0u);
    }

    // Verifies CAT-COMPILER-002.
    TEST(compiler, AtomicOperationsActOnAPlainWord) {
        using namespace catenae::detail;
        std::uint32_t word = 0;
        atomic_store_relaxed(word, 5);
        EXPECT_EQ(atomic_load_relaxed(word), 5u);
        atomic_store_release(word, 6);
        EXPECT_EQ(atomic_load_acquire(word), 6u);
        EXPECT_EQ(atomic_exchange_acq_rel(word, 9), 6u);
        EXPECT_EQ(word, 9u);
        EXPECT_EQ(atomic_fetch_add_relaxed(word, 4294967295u), 9u); // wraps, as unsigned arithmetic
        EXPECT_EQ(word, 8u);
    }

#if defined(CATENAE_TEST_THREADS)
    // Verifies CAT-COMPILER-002: release and acquire order the payload before the word, across
    // two threads, under the thread sanitizer's leg as well as natively.
    TEST(compiler, ReleaseAndAcquirePublishAcrossThreads) {
        shared_words            words{};
        constexpr std::uint32_t rounds = 2000;
        std::thread             producer([&words] {
            for (std::uint32_t round = 1; round <= rounds; ++round) {
                while (catenae::detail::atomic_load_acquire(words.acked) != round - 1) {
                }
                for (std::uint32_t& v : words.payload) {
                    v = round;
                }
                catenae::detail::atomic_store_release(words.published, round);
            }
        });
        std::uint32_t           wrong = 0;
        for (std::uint32_t round = 1; round <= rounds; ++round) {
            while (catenae::detail::atomic_load_acquire(words.published) != round) {
            }
            for (const std::uint32_t v : words.payload) {
                wrong += v == round ? 0u : 1u;
            }
            catenae::detail::atomic_store_release(words.acked, round);
        }
        producer.join();
        EXPECT_EQ(wrong, 0u);
    }
#endif

} // namespace
