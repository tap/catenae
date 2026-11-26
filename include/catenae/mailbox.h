/// @file mailbox.h
/// @brief `mailbox<T>`: one value from one thread to one other, the newest winning and nobody
/// waiting (D44, PLAN.md 6.3).
///
/// | Contract   |                                                                                |
/// |------------|--------------------------------------------------------------------------------|
/// | Value      | `T`, trivially copyable and trivially destructible, owning all it refers to;  |
/// |            | a `T` that says it holds a view (`holds_view<T>`, a table design) is refused   |
/// | Sides      | one poster thread, one taker thread; an interrupt handler counts as a thread   |
/// | Storage    | three slots of `T` and one atomic word; starts empty, `.bss` when static       |
/// | Waits      | never: a post replaces a value never taken, a take finds a value or nothing   |
/// | Limits     | a value that refers to storage it does not own and does not say so is a race  |
/// |            | the compiler cannot see (AOU-004)                                              |
/// | Method     | a triple buffer: each side owns a slot and swaps it with the one in the middle |
/// | Rationale  | D19, D22, D44                                                                  |
///
/// @req{CAT-MAILBOX-001} A take after a post calls its function once, with the newest value
/// posted; a take with nothing new returns false and calls nothing. A value never taken is
/// replaced by the next post. Neither side waits, and a mailbox starts empty.
/// @req{CAT-MAILBOX-002} Memory orders: a post writes its own slot and then exchanges it into
/// the middle with acquire and release, so a take that exchanges it out (acquire and release)
/// sees the whole value; a take first loads the middle word with acquire to see whether
/// anything is new. Each side touches only its own slot, so a value is never read while it is
/// written; two threads put two million 64-byte values through it, none torn, none out of order.
/// @req{CAT-MAILBOX-003} A post begins its value's lifetime with placement new in the poster's
/// slot, never by assignment, and a take reads it through `std::launder` (DEV-013);
/// `scripts/check_slots.py` refuses an assignment into a slot.
/// @req{CAT-MAILBOX-004} A mailbox of a type that holds a view (`holds_view<T>`, true for a
/// `table_view`, and for a chain, `steady<>` or `parallel<>` with a stage that holds one) does
/// not compile: a table design is replaced through the acknowledgement of PLAN.md 6.3.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_MAILBOX_H
#define CATENAE_MAILBOX_H

#include <array>
#include <atomic>
#include <cstdint>
#include <new>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/fwd.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief One value handed from one thread to one other: the newest wins, nobody waits.
        template <typename T>
        class mailbox {
            static_assert(std::is_trivially_copyable<T>::value && std::is_trivially_destructible<T>::value,
                          "catenae: a mailbox carries a trivially copyable, trivially destructible value");
            static_assert(!holds_view<T>::value,
                          "catenae: a mailbox carries a value that owns what it refers to, not one that holds a "
                          "view; a table design is replaced through an acknowledgement (PLAN.md 6.3)");
            static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                          "catenae: a mailbox needs lock-free 32-bit atomic operations (PLAN.md section 6.1)");

          public:
            /// Empty: a constant constructor, so a static mailbox is in .bss with no start-up code.
            constexpr mailbox() noexcept = default;

            /// The posting thread, and only it. Never waits; a value not yet taken is replaced.
            void post(const T& value) noexcept {
                // The value begins its lifetime in the poster's slot (assignment could not, for a
                // T with no trivial default constructor), then that slot becomes the middle one.
                ::new (static_cast<void*>(m_slots[m_poster].bytes)) T(value);
                const std::uint32_t was = m_middle.exchange(to_middle(m_poster) | k_fresh, std::memory_order_acq_rel);
                m_poster                = middle_of(was); // the old middle slot is the poster's now
            }

            /// The taking thread, and only it. If a value has arrived since the last take, calls
            /// `apply(value)` and returns true; otherwise returns false. Never waits.
            template <typename Apply>
            bool take(Apply&& apply) noexcept {
                if ((m_middle.load(std::memory_order_acquire) & k_fresh) == 0u) {
                    return false;
                }
                const std::uint32_t was    = m_middle.exchange(to_middle(taker_of(m_taker)), std::memory_order_acq_rel);
                const std::uint32_t newest = middle_of(was); // the newest value's slot is the taker's now
                m_taker                    = to_taker(newest);
                apply(*std::launder(static_cast<const T*>(static_cast<const void*>(m_slots[newest].bytes))));
                return true;
            }

            /// Empty again. Only while neither side runs.
            void reset() noexcept {
                m_middle.store(0u, std::memory_order_release);
                m_poster = 0u;
                m_taker  = 0u;
            }

          private:
            // The three slots are a permutation of 0, 1 and 2, counted round a ring of three with a
            // compare rather than a division. Each is stored so that all zero is the arrangement at
            // rest (the poster's slot 0, the middle 1, the taker's 2), and a static mailbox is in
            // .bss: the poster's index as itself, the middle's as the slot before it, the taker's
            // as the slot after it. Above the middle's index, a bit says it holds a value no take
            // has seen.
            static constexpr std::uint32_t k_slot_bits = 3u;
            static constexpr std::uint32_t k_fresh     = 4u;
            static constexpr std::uint32_t after(std::uint32_t slot) noexcept { return slot == 2u ? 0u : slot + 1u; }
            static constexpr std::uint32_t before(std::uint32_t slot) noexcept { return slot == 0u ? 2u : slot - 1u; }
            static constexpr std::uint32_t to_middle(std::uint32_t slot) noexcept { return before(slot); }
            static constexpr std::uint32_t middle_of(std::uint32_t word) noexcept { return after(word & k_slot_bits); }
            static constexpr std::uint32_t to_taker(std::uint32_t slot) noexcept { return after(slot); }
            static constexpr std::uint32_t taker_of(std::uint32_t stored) noexcept { return before(stored); }

            /// Room for one T, its lifetime begun by a post.
            struct slot {
                alignas(T) unsigned char bytes[sizeof(T)];
            };

            std::array<slot, 3>        m_slots{};     // three values' room; which is whose, the indices say
            std::atomic<std::uint32_t> m_middle{0u};  // a word of its own, shared by both sides
            std::uint32_t              m_poster = 0u; // touched by the posting thread only
            std::uint32_t              m_taker  = 0u; // touched by the taking thread only
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_MAILBOX_H
