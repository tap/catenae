# The mailbox contract {#mailbox_contract}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

A mailbox carries one value from one thread to one other: the newest value wins, and neither
side ever waits (D44). It is how a setting reaches a chain from a thread that does not run
it. Where a junction carries a stream of frames, a mailbox carries a state: a value never
taken is replaced by the next post, not queued behind it. `catenae::mailbox<T>`
(`catenae/mailbox.h`) is the one mailbox; its card and its requirements, CAT-MAILBOX-001 to
003, are on [its header's page](mailbox_8h.html). Its members are documented here, once.

| Member | Meaning |
|---|---|
| `mailbox()` | `constexpr`: empty. A mailbox is an object with a constant constructor, not a state, so a static one is in `.bss` and needs no start-up code. |
| `post(value) noexcept` | Exactly one thread, the poster. Never waits. Begins the value's lifetime in the poster's slot with placement new, then makes that slot the middle one. A value no take has seen is replaced. |
| `take(apply) noexcept` | Exactly one other thread, the taker. If a value has arrived since the last take, makes the middle slot its own, calls `apply(value)` once and returns true; otherwise returns false and calls nothing. Never waits. |
| `reset() noexcept` | Empty again. Only while neither side runs. |
| `T` | Trivially copyable and trivially destructible, asserted: a design, a level, a reading. It owns everything it refers to: no pointer, span or view inside it. |

## How it works

Three slots of `T` and one atomic 32-bit word. Each side owns a slot; the word names the
third, the middle one, and says whether it holds a value no take has seen. A post writes its
own slot and swaps it into the middle; a take swaps the middle slot out for its own. Each
side touches only the slot it owns, so a value is never read while it is written, and two
fields posted together arrive together. The memory orders are CAT-MAILBOX-002's.

The word is a `std::atomic<std::uint32_t>`, which must be lock-free (asserted). A junction's
state cannot hold one, since it is plain storage with no constructor; a mailbox is not a
state, and its constant constructor is what puts a static one in `.bss` at every standard.

## What the compiler cannot check

- **One thread per side** (AOU-001), where an interrupt handler counts as a thread. Two
  posters, or two takers, are a data race no check can see. The mailbox is verified where a
  mistake would show: two million 64-byte values between two threads under the thread
  sanitizer and natively on an arm64 runner, and a timer interrupt as the poster against the
  main loop on the bare-metal Cortex-M legs (`tests/test_mailbox.cpp`,
  `tests/bare_metal/interrupt.cpp`).
- **A value owns what it refers to** (AOU-004). A pointer inside a posted value hands the
  race back to its two threads.
- **The value's lifetime.** A post constructs with placement new and a take reads through
  `std::launder` (DEV-013). No sanitizer sees a mistake here and no constant expression can
  test it, so `scripts/check_slots.py` refuses an assignment into a slot (CAT-MAILBOX-003).

## A table design

A table design is held by view, so a mailbox cannot carry the table itself. It is replaced
while the audio thread runs only with an acknowledgement: the control thread posts the new
table's identity, the audio thread posts back the generation it now runs through a second
mailbox, and the control thread reuses a table's storage only after it sees a later
generation acknowledged (PLAN.md section 6.3). The first table design arrives at C3.
