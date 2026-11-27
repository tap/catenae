/// @file interrupt.cpp
/// @brief A timer interrupt as the mailbox's poster and the handoff's producer, the main loop
/// as the taker and the consumer: the single-core deployment AOU-001 describes (PLAN.md 6.3,
/// 7.3). Built into the battery on the bare-metal legs only.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "catenae/compiler.h"
#include "catenae/handoff.h"
#include "catenae/mailbox.h"

namespace {

    /// 64 bytes, every word the tick's number: a torn reading has two different words.
    struct reading {
        std::array<std::uint32_t, 16> words;
    };

    using link_t = catenae::handoff<float, 2, 256, 64, 16>;

    constexpr std::uint32_t k_ticks = 400; // the interrupts the test waits for

    CATENAE_CONSTINIT catenae::mailbox<reading> s_box;
    CATENAE_CONSTINIT link_t::state s_link{};
    std::uint32_t                   s_ticks; // the interrupt's count: a word of its own
    std::uint32_t                   s_frame; // the next frame's number: the interrupt's only

    /// The SysTick block of an Armv7-M or Armv8-M core (the Architecture Reference Manual's
    /// B3.3 and D1.2): control and status, reload value, current value.
    struct systick_registers {
        std::uint32_t ctrl;
        std::uint32_t load;
        std::uint32_t val;
    };
    constexpr std::uintptr_t k_systick   = 0xE000E010u;
    constexpr std::uint32_t  k_enable    = 1u; // count
    constexpr std::uint32_t  k_interrupt = 2u; // raise SysTick at zero
    constexpr std::uint32_t  k_core      = 4u; // count the processor clock

    volatile systick_registers& systick() noexcept {
        return *reinterpret_cast<volatile systick_registers*>(k_systick);
    }

} // namespace

/// The timer's interrupt: one reading posted, and 16 frames pushed, each tick.
extern "C" void SysTick_Handler(void);
extern "C" void SysTick_Handler(void) { // the vector's name, which the startup declares weak
    const std::uint32_t done = catenae::detail::atomic_load_relaxed(s_ticks);
    if (done >= k_ticks) {
        // A tick already pending when the last one stopped the timer: QEMU's SysTick counts the
        // host's clock, so a slow handler can let the next one fall due before it ends.
        return;
    }
    const std::uint32_t tick = done + 1u;
    reading             r{};
    r.words.fill(tick);
    s_box.post(r);
    std::array<float, 16 * 2> block{};
    for (std::size_t i = 0; i < 16; ++i) {
        block[2 * i]     = static_cast<float>(++s_frame);
        block[2 * i + 1] = -block[2 * i];
    }
    static_cast<void>(link_t{}.push(s_link, catenae::span<const float>(block))); // an overrun drops the newest
    catenae::detail::atomic_store_release(s_ticks, tick);
    if (tick == k_ticks) {
        systick().ctrl = 0u; // the last tick: stop the timer
    }
}

namespace {

    // Verifies CAT-MAILBOX-002 and CAT-HANDOFF-005 with an interrupt for the other thread.
    TEST(interrupt, ATimerIsThePosterAndTheProducer) {
        std::uint32_t taken = 0;
        std::uint32_t last  = 0;
        std::uint32_t torn  = 0;
        std::uint32_t order = 0;
        const auto    check = [&](const reading& r) noexcept {
            for (const std::uint32_t w : r.words) {
                torn += w == r.words[0] ? 0u : 1u;
            }
            order += r.words[0] > last ? 0u : 1u;
            last = r.words[0];
            ++taken;
        };
        std::array<float, 16 * 2> out{};
        float                     last_frame  = 0.0f;
        std::uint32_t             real        = 0;
        std::uint32_t             torn_frames = 0;
        std::uint32_t             late_frames = 0;
        const auto                drain       = [&] {
            const std::size_t got = link_t{}.pull(s_link, catenae::span<float>(out));
            for (std::size_t i = 0; i < got; ++i) {
                torn_frames += out[2 * i + 1] < -out[2 * i] || out[2 * i + 1] > -out[2 * i] ? 1u : 0u;
                late_frames += out[2 * i] > last_frame ? 0u : 1u;
                last_frame = out[2 * i];
            }
            real += static_cast<std::uint32_t>(got);
        };

        systick().ctrl = 0u;
        systick().load = 4999u; // a tick every 5,000 processor clocks
        systick().val  = 0u;
        systick().ctrl = k_enable | k_interrupt | k_core;
        while (catenae::detail::atomic_load_acquire(s_ticks) < k_ticks) {
            static_cast<void>(s_box.take(check));
            drain();
        }
        static_cast<void>(s_box.take(check)); // the last tick's reading
        drain();
        EXPECT_GT(taken, 0u);
        EXPECT_EQ(last, k_ticks);
        EXPECT_EQ(torn, 0u);
        EXPECT_EQ(order, 0u);
        EXPECT_GT(real, 0u);
        EXPECT_EQ(torn_frames, 0u);
        EXPECT_EQ(late_frames, 0u);
    }

} // namespace
