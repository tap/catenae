/// @file test_handoff.cpp
/// @brief handoff's contract (include/catenae/handoff.h): its fill policy as numbers.
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#if defined(CATENAE_TEST_THREADS)
#include <atomic>
#include <thread>
#endif

#include <gtest/gtest.h>

#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/handoff.h"
#include "catenae/junction_traits.h"
#include "catenae_test/check_trap.h"

namespace {

    using link_t  = catenae::handoff<float, 1, 1024, 128, 64>;
    using state_t = link_t::state;

    /// One producer and one consumer of 64-frame blocks, run by hand on one thread: frames are
    /// numbered from 1, so a frame's value says when it was pushed.
    class bench {
      public:
        bench()
            : m_state(std::make_unique<state_t>()) {}

        /// One push of the next 64 frames; returns the frames kept.
        std::size_t push() {
            std::array<float, 64> block{};
            for (float& v : block) {
                v = static_cast<float>(++m_pushed);
            }
            return m_link.push(*m_state, catenae::span<const float>(block));
        }
        /// One pull of 64 frames. Records, when it delivered real frames, the fill that pull saw:
        /// what the ring held once a starting pull's discard was done, the frames it delivered
        /// and those still held after it.
        std::size_t pull() {
            std::array<float, 64> block{};
            const std::size_t     real = m_link.pull(*m_state, catenae::span<float>(block));
            if (real > 0) {
                m_seen.push_back(m_link.report(*m_state).fill_frames + static_cast<std::uint32_t>(real));
            }
            return real;
        }
        const std::vector<std::uint32_t>& seen() const noexcept { return m_seen; }
        link_t::report_type               report() const noexcept { return m_link.report(*m_state); }
        state_t&                          state() noexcept { return *m_state; }
        const link_t&                     link() const noexcept { return m_link; }

      private:
        link_t                     m_link{};
        std::unique_ptr<state_t>   m_state; // value-initialised: all zero, at rest
        std::uint32_t              m_pushed = 0;
        std::vector<std::uint32_t> m_seen;
    };

    // Verifies CAT-JUNCTION-001 for handoff.
    TEST(handoff, MeetsTheJunctionContract) {
        using traits = catenae::junction_traits<link_t>;
        static_assert(traits::opted_in && traits::clocking == catenae::clocks::shared, "a junction of one clock");
        static_assert(traits::capacity_frames == 1024, "its capacity");
        static_assert(std::is_empty<link_t>::value, "its configuration is its template arguments");
        const link_t link{};
        const auto   latency = link.latency(state_t{});
        EXPECT_EQ(latency.nominal, 128.0f);
        EXPECT_EQ(latency.range, 64.0f);
    }

    // Verifies CAT-HANDOFF-001: the producer 0, 2 and 100 periods ahead.
    TEST(handoff, AStartingPullSeesTheTargetWithTheProducerAhead) {
        for (const int ahead : {0, 2, 100}) {
            bench b;
            for (int i = 0; i < ahead; ++i) {
                static_cast<void>(b.push());
            }
            for (int period = 0; period < 50; ++period) {
                static_cast<void>(b.push());
                static_cast<void>(b.pull());
            }
            ASSERT_FALSE(b.seen().empty()) << ahead;
            for (const std::uint32_t fill : b.seen()) {
                EXPECT_EQ(fill, 128u) << "producer " << ahead << " periods ahead";
            }
        }
    }

    // Verifies CAT-HANDOFF-001: the consumer ahead.
    TEST(handoff, AStartingPullSeesTheTargetWithTheConsumerAhead) {
        bench b;
        for (int i = 0; i < 5; ++i) {
            EXPECT_EQ(b.pull(), 0u); // silence: the ring is filling
        }
        for (int period = 0; period < 50; ++period) {
            static_cast<void>(b.push());
            static_cast<void>(b.pull());
        }
        ASSERT_EQ(b.seen().size(), 49u); // the first pair of pushes fills it to 128
        for (const std::uint32_t fill : b.seen()) {
            EXPECT_EQ(fill, 128u);
        }
        EXPECT_EQ(b.report().underruns, 0u);
    }

    // Verifies CAT-HANDOFF-002: a producer held up for five periods, then its backlog at once.
    TEST(handoff, ABacklogCostsOneUnderrunAndReturnsToTheTarget) {
        bench b;
        for (int period = 0; period < 10; ++period) {
            static_cast<void>(b.push());
            static_cast<void>(b.pull());
        }
        for (int period = 0; period < 5; ++period) {
            static_cast<void>(b.pull()); // the producer is held up
        }
        for (int period = 0; period < 5; ++period) {
            static_cast<void>(b.push()); // and then delivers what it owed
        }
        for (int period = 0; period < 10; ++period) {
            static_cast<void>(b.push());
            static_cast<void>(b.pull());
        }
        EXPECT_EQ(b.report().underruns, 1u);
        EXPECT_EQ(b.seen().back(), 128u);
        EXPECT_EQ(b.report().overruns, 0u);
    }

    // Verifies CAT-HANDOFF-002: a push the ring cannot hold keeps what fits and drops the newest.
    TEST(handoff, AnOverrunDropsTheNewest) {
        bench b;
        for (int i = 0; i < 16; ++i) {
            ASSERT_EQ(b.push(), 64u); // 1024 frames: full
        }
        EXPECT_EQ(b.push(), 0u);
        EXPECT_EQ(b.report().overruns, 1u);
        EXPECT_EQ(b.report().fill_frames, 1024u);
        // The pull that starts keeps the newest 128 of what was kept: frames 897 to 1024.
        std::array<float, 64> block{};
        ASSERT_EQ(b.link().pull(b.state(), catenae::span<float>(block)), 64u);
        EXPECT_EQ(block[0], 897.0f);
    }

    /// The ring of a target, fed 480 frames every 480 frames of time and pulled 64 every 64, a
    /// push due before a pull made before it and a pull first when they coincide: how many
    /// underruns in `periods` of the producer.
    template <std::size_t Target>
    std::uint32_t underruns_at_480_and_64(int periods) {
        using ring = catenae::handoff<float, 1, 2048, Target, 480>;
        const ring             link{};
        auto                   st = std::make_unique<typename ring::state>();
        std::array<float, 480> in{};
        std::array<float, 64>  out{};
        const std::size_t      end       = static_cast<std::size_t>(periods) * 480;
        std::size_t            next_push = 480;
        for (std::size_t t = 64; t <= end; t += 64) {
            for (; next_push < t; next_push += 480) {
                static_cast<void>(link.push(*st, catenae::span<const float>(in)));
            }
            static_cast<void>(link.pull(*st, catenae::span<float>(out)));
            if (next_push == t) { // a push due with the pull: after it
                static_cast<void>(link.push(*st, catenae::span<const float>(in)));
                next_push += 480;
            }
        }
        return link.report(*st).underruns;
    }

    // Verifies CAT-HANDOFF-003.
    TEST(handoff, ServesIsTheArithmetic) {
        static_assert(catenae::handoff<float, 1, 256, 64, 64>::serves(64, 64), "64 and 64: a target of 64");
        static_assert(!catenae::handoff<float, 1, 256, 64, 64>::serves(64, 1), "but not when the sizes vary");
        static_assert(catenae::handoff<float, 1, 256, 127, 64>::serves(64, 1), "64 + 64 - 1 when they vary");
        static_assert(catenae::handoff<float, 1, 2048, 512, 480>::serves(64, 32), "480 beside 64: 512");
        static_assert(!catenae::handoff<float, 1, 2048, 480, 480>::serves(64, 32), "and not 480");
        static_assert(!catenae::handoff<float, 1, 2048, 512, 480>::serves(64, 7), "a quantum that divides no push");
        EXPECT_EQ(underruns_at_480_and_64<512>(200), 0u);
        EXPECT_GT(underruns_at_480_and_64<480>(200), 0u);
    }

    // Verifies CAT-HANDOFF-004.
    TEST(handoff, RestartResetAndReport) {
        bench b;
        for (int period = 0; period < 4; ++period) {
            static_cast<void>(b.push());
            static_cast<void>(b.pull());
        }
        EXPECT_EQ(b.report().running, 1u);
        EXPECT_EQ(b.report().fill_frames, 64u);
        b.link().restart(b.state());
        EXPECT_EQ(b.report().running, 0u);
        EXPECT_EQ(b.report().fill_frames, 0u);
        static_cast<void>(b.push());
        EXPECT_EQ(b.pull(), 0u); // filling again
        static_cast<void>(b.push());
        EXPECT_EQ(b.pull(), 64u);
        EXPECT_EQ(b.seen().back(), 128u);
        b.link().reset(b.state());
        const auto rest = b.report();
        EXPECT_EQ(rest.running + rest.fill_frames + rest.underruns + rest.overruns, 0u);
    }

    // Verifies CAT-HANDOFF-004: the boundary checks.
    TEST(handoff, PushAndPullAreCheckedAtTheBoundary) {
        const link_t           link{};
        auto                   st = std::make_unique<state_t>();
        std::array<float, 129> values{};
        CATENAE_EXPECT_CHECK(catenae::check::push_too_large,
                             static_cast<void>(link.push(*st, catenae::span<const float>(values.data(), 65))));
        CATENAE_EXPECT_CHECK(catenae::check::pull_too_large,
                             static_cast<void>(link.pull(*st, catenae::span<float>(values.data(), 129))));
        using stereo = catenae::handoff<float, 2, 256, 64, 64>;
        auto st2     = std::make_unique<stereo::state>();
        CATENAE_EXPECT_CHECK(catenae::check::input_not_whole_frames,
                             static_cast<void>(stereo{}.push(*st2, catenae::span<const float>(values.data(), 3))));
    }

#if defined(CATENAE_TEST_THREADS)
    // Verifies CAT-HANDOFF-005: three million stereo frames between two threads, the consumer
    // restarting now and then and a third thread reading the report, none torn or out of order.
    TEST(handoff, TwoThreadsMoveEveryFrameUntornAndInOrder) {
        using stereo = catenae::handoff<float, 2, 1024, 128, 64>;
        const stereo              link{};
        auto                      st     = std::make_unique<stereo::state>();
        constexpr std::size_t     frames = 3000000;
        std::atomic<bool>         done{false};
        std::thread               producer([&] {
            std::array<float, 64 * 2> block{};
            std::size_t               next = 1;
            while (next < frames) {
                for (std::size_t i = 0; i < 64; ++i) {
                    block[2 * i]     = static_cast<float>(next + i);
                    block[2 * i + 1] = -block[2 * i];
                }
                next += link.push(*st, catenae::span<const float>(block)); // what was refused is sent again
            }
            done.store(true);
        });
        std::thread               watcher([&] {
            while (!done.load()) {
                EXPECT_LE(link.report(*st).fill_frames, 1024u);
            }
        });
        std::array<float, 64 * 2> out{};
        float                     last  = 0.0f;
        std::size_t               real  = 0;
        std::size_t               torn  = 0;
        std::size_t               order = 0;
        std::size_t               pulls = 0;
        while (!done.load()) {
            const std::size_t got = link.pull(*st, catenae::span<float>(out));
            for (std::size_t i = 0; i < got; ++i) {
                torn += out[2 * i + 1] < -out[2 * i] || out[2 * i + 1] > -out[2 * i] ? 1u : 0u;
                order += out[2 * i] > last ? 0u : 1u;
                last = out[2 * i];
            }
            real += got;
            if (++pulls % 5000 == 0) {
                link.restart(*st); // the consumer's restart, while the producer runs
            }
        }
        producer.join();
        watcher.join();
        EXPECT_GT(real, 0u);
        EXPECT_EQ(torn, 0u);
        EXPECT_EQ(order, 0u);
    }
#endif

} // namespace
