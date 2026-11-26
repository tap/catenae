/// @file compiler.h
/// @brief The compiler extensions catenae uses, each in one place (PLAN.md section 8).
///
/// | Extension                         | What it is for                                        | Deviation |
/// |-----------------------------------|-------------------------------------------------------|-----------|
/// | `CATENAE_CONSTINIT`               | a static that start-up code would build does not compile | DEV-001 |
/// | `CATENAE_NOINLINE`, `_ALWAYS_INLINE` | inlining control                                   | DEV-002   |
/// | `CATENAE_RESTRICT`                | no aliasing between a kernel's pointers               | DEV-003   |
/// | `detail::atomic_*` on a `std::uint32_t` | the words a junction's two sides share (D19)    | DEV-004   |
/// | `detail::atomic_load_*`, `_store_*` on a `float` | a measured number one side publishes   | DEV-004   |
///
/// @req{CAT-COMPILER-001} A static declared `CATENAE_CONSTINIT` that would need start-up code
/// does not compile on GCC and Clang, at C++17, 20 and 23; on MSVC the macro is empty and
/// those two carry the check (PLAN.md section 3.3). Plain storage is written with `{}`:
/// `CATENAE_CONSTINIT static T s{};` is zero, in `.bss`, on every compiler.
///
/// Why `{}`: GCC accepts a plain aggregate with no initializer as constant-initialised, but
/// Clang (its attribute, and C++20's `constinit` alike) rejects it, its members being
/// uninitialised; value-initialisation is a constant on both, and still costs no start-up code.
/// @req{CAT-COMPILER-002} The atomic operations act on a plain `std::uint32_t`, so a state
/// that holds the word is a plain aggregate with no start-up code; each is lock-free on every
/// target CI runs (asserted), with the memory order its name states.
/// @req{CAT-COMPILER-003} The loads and stores also act on a plain `float`, as one lock-free
/// 32-bit word with the order its name states, so a number one side measures (a junction's
/// report) is shared as a `float` with no copy of its bits in the caller.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_COMPILER_H
#define CATENAE_COMPILER_H

#include <cstdint>

#include "catenae/detail/abi.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <cstring>

#include <intrin.h>
#endif

/// Constant initialisation required (DEV-001): GCC's `__constinit`, Clang's attribute; empty
/// on MSVC, where GCC and Clang carry the check. Plain storage is written with `{}`.
#if defined(__clang__)
#define CATENAE_CONSTINIT [[clang::require_constant_initialization]]
#elif defined(__GNUC__)
#define CATENAE_CONSTINIT __constinit
#else
#define CATENAE_CONSTINIT
#endif

// Inlining control (DEV-002) and the no-alias qualifier (DEV-003), each in its compiler's spelling.
#if defined(_MSC_VER) && !defined(__clang__)
#define CATENAE_NOINLINE __declspec(noinline)
#define CATENAE_ALWAYS_INLINE __forceinline
#define CATENAE_RESTRICT __restrict
#else
/// A function the compiler keeps out of line (DEV-002).
#define CATENAE_NOINLINE __attribute__((noinline))
/// An inline function the compiler inlines wherever it is called (DEV-002).
#define CATENAE_ALWAYS_INLINE inline __attribute__((always_inline))
/// A pointer through which nothing else the function reaches is accessed (DEV-003).
#define CATENAE_RESTRICT __restrict__
#endif

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

#if defined(_MSC_VER) && !defined(__clang__)
            // MSVC's interlocked intrinsics act on a long, which is 32 bits on every Windows
            // target. Each is a full barrier, at least the order each function names.
            static_assert(sizeof(long) == sizeof(std::uint32_t), "catenae: a long is 32 bits on MSVC");
            inline volatile long* as_interlocked(std::uint32_t& word) noexcept {
                return reinterpret_cast<volatile long*>(&word);
            }
            inline std::uint32_t atomic_load_acquire(std::uint32_t& word) noexcept {
                return static_cast<std::uint32_t>(_InterlockedOr(as_interlocked(word), 0));
            }
            inline std::uint32_t atomic_load_relaxed(std::uint32_t& word) noexcept {
                return static_cast<std::uint32_t>(_InterlockedOr(as_interlocked(word), 0));
            }
            inline void atomic_store_release(std::uint32_t& word, std::uint32_t value) noexcept {
                static_cast<void>(_InterlockedExchange(as_interlocked(word), static_cast<long>(value)));
            }
            inline void atomic_store_relaxed(std::uint32_t& word, std::uint32_t value) noexcept {
                static_cast<void>(_InterlockedExchange(as_interlocked(word), static_cast<long>(value)));
            }
            inline std::uint32_t atomic_exchange_acq_rel(std::uint32_t& word, std::uint32_t value) noexcept {
                return static_cast<std::uint32_t>(_InterlockedExchange(as_interlocked(word), static_cast<long>(value)));
            }
            inline std::uint32_t atomic_fetch_add_relaxed(std::uint32_t& word, std::uint32_t value) noexcept {
                return static_cast<std::uint32_t>(
                    _InterlockedExchangeAdd(as_interlocked(word), static_cast<long>(value)));
            }
            // A float as the long its bits are (MSVC does not optimise by type-based aliasing),
            // its bits copied in and out (DEV-006).
            static_assert(sizeof(float) == sizeof(long), "catenae: a float is 32 bits");
            inline volatile long* as_interlocked(float& word) noexcept {
                return reinterpret_cast<volatile long*>(&word);
            }
            inline float float_of_long(long bits) noexcept {
                float value;
                std::memcpy(&value, &bits, sizeof value);
                return value;
            }
            inline long long_of_float(float value) noexcept {
                long bits;
                std::memcpy(&bits, &value, sizeof bits);
                return bits;
            }
            inline float atomic_load_acquire(float& word) noexcept {
                return float_of_long(_InterlockedOr(as_interlocked(word), 0));
            }
            inline float atomic_load_relaxed(float& word) noexcept {
                return float_of_long(_InterlockedOr(as_interlocked(word), 0));
            }
            inline void atomic_store_release(float& word, float value) noexcept {
                static_cast<void>(_InterlockedExchange(as_interlocked(word), long_of_float(value)));
            }
            inline void atomic_store_relaxed(float& word, float value) noexcept {
                static_cast<void>(_InterlockedExchange(as_interlocked(word), long_of_float(value)));
            }
#else
            static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr),
                          "catenae: a junction needs lock-free 32-bit atomic operations (PLAN.md section 6.1)");
            /// The word, read with acquire order: what the other side released before it is visible.
            inline std::uint32_t atomic_load_acquire(std::uint32_t& word) noexcept {
                return __atomic_load_n(&word, __ATOMIC_ACQUIRE);
            }
            /// The word, read with no ordering: a value this side wrote, or a report.
            inline std::uint32_t atomic_load_relaxed(std::uint32_t& word) noexcept {
                return __atomic_load_n(&word, __ATOMIC_RELAXED);
            }
            /// The word, written with release order: what this side wrote before it is published.
            inline void atomic_store_release(std::uint32_t& word, std::uint32_t value) noexcept {
                __atomic_store_n(&word, value, __ATOMIC_RELEASE);
            }
            /// The word, written with no ordering.
            inline void atomic_store_relaxed(std::uint32_t& word, std::uint32_t value) noexcept {
                __atomic_store_n(&word, value, __ATOMIC_RELAXED);
            }
            /// The word swapped for a value, acquiring and releasing; returns what it held.
            inline std::uint32_t atomic_exchange_acq_rel(std::uint32_t& word, std::uint32_t value) noexcept {
                return __atomic_exchange_n(&word, value, __ATOMIC_ACQ_REL);
            }
            /// The word incremented with no ordering, wrapping; returns what it held.
            inline std::uint32_t atomic_fetch_add_relaxed(std::uint32_t& word, std::uint32_t value) noexcept {
                return __atomic_fetch_add(&word, value, __ATOMIC_RELAXED);
            }

            static_assert(sizeof(float) == sizeof(std::uint32_t) && __atomic_always_lock_free(sizeof(float), nullptr),
                          "catenae: a float is one lock-free 32-bit word");
            /// The float, read with acquire order.
            inline float atomic_load_acquire(float& word) noexcept {
                float value;
                __atomic_load(&word, &value, __ATOMIC_ACQUIRE);
                return value;
            }
            /// The float, read with no ordering: a number the other side measured.
            inline float atomic_load_relaxed(float& word) noexcept {
                float value;
                __atomic_load(&word, &value, __ATOMIC_RELAXED);
                return value;
            }
            /// The float, written with release order.
            inline void atomic_store_release(float& word, float value) noexcept {
                __atomic_store(&word, &value, __ATOMIC_RELEASE);
            }
            /// The float, written with no ordering: a number this side measured.
            inline void atomic_store_relaxed(float& word, float value) noexcept {
                __atomic_store(&word, &value, __ATOMIC_RELAXED);
            }
#endif

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_COMPILER_H
