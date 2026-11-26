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
/// | `detail::floating_point_control()` | reading the thread's flush-to-zero setting (AOU-008)  | DEV-005   |
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
/// @req{CAT-COMPILER-004} `detail::subnormals_flushed<S>()` is whether the calling thread
/// flushes S's subnormal results to zero, read from the control register (MXCSR's FTZ and DAZ
/// on x86-64, FPCR.FZ on AArch64, FPSCR.FZ on 32-bit Arm) where one governs S's arithmetic,
/// and `detail::k_subnormal_control<S>` is false where none does (software float, Hexagon).
/// Nothing in catenae writes the register; the host sets it (carried from DspTap's
/// `subnormals.h`, NOTICE.md).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_COMPILER_H
#define CATENAE_COMPILER_H

#include <cstdint>
#include <type_traits>

#include "catenae/config.h"
#include "catenae/detail/abi.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <cstring>

#include <intrin.h>
#endif

// The floating-point control register this target has, if any (DEV-005).
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define CATENAE_FP_CONTROL_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CATENAE_FP_CONTROL_AARCH64 1
#elif defined(__arm__) && defined(__ARM_FP)
#define CATENAE_FP_CONTROL_AARCH32 1
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

            /// Whether a control bit of this target governs the subnormal results of S's
            /// arithmetic: float and double on x86-64 and AArch64; on 32-bit Arm, float with a
            /// single-precision FPU and double with a double-precision one (__ARM_FP's bits 2
            /// and 3); nothing else, with software float, on Hexagon or for a fixed-point S.
            template <typename S>
            inline constexpr bool k_subnormal_control =
#if defined(CATENAE_FP_CONTROL_X86_64) || defined(CATENAE_FP_CONTROL_AARCH64)
                std::is_same<S, float>::value || std::is_same<S, double>::value;
#elif defined(CATENAE_FP_CONTROL_AARCH32)
                (std::is_same<S, float>::value && (__ARM_FP & 4) != 0)
                || (std::is_same<S, double>::value && (__ARM_FP & 8) != 0);
#else
                false;
#endif

            /// The bits a host sets to flush subnormals: MXCSR's FTZ and DAZ on x86-64, FZ
            /// (bit 24) of FPCR on AArch64 and of FPSCR on 32-bit Arm; 0 where there are none.
            inline constexpr std::uint64_t k_flush_to_zero_bits =
#if defined(CATENAE_FP_CONTROL_X86_64)
                0x8040u;
#elif defined(CATENAE_FP_CONTROL_AARCH64) || defined(CATENAE_FP_CONTROL_AARCH32)
                std::uint64_t{1} << 24u;
#else
                0u;
#endif

            /// The calling thread's floating-point control register (MXCSR, FPCR, FPSCR), read
            /// without side effects; 0 where the target has none (DEV-005). Never written here.
            inline std::uint64_t floating_point_control() noexcept {
#if defined(CATENAE_FP_CONTROL_X86_64)
                return _mm_getcsr();
#elif defined(CATENAE_FP_CONTROL_AARCH64)
#if defined(_MSC_VER) && !defined(__clang__)
                return static_cast<std::uint64_t>(_ReadStatusReg(ARM64_FPCR));
#else
                std::uint64_t value = 0;
                __asm__ volatile("mrs %0, fpcr" : "=r"(value));
                return value;
#endif
#elif defined(CATENAE_FP_CONTROL_AARCH32)
                return __builtin_arm_get_fpscr();
#else
                return 0;
#endif
            }

            /// Whether S's subnormal results are flushed to zero on the calling thread now:
            /// the control bits, all of them set, where a bit governs S; false for a floating
            /// S no bit governs, where gradual underflow is all there is.
            template <typename S>
            inline bool subnormals_flushed() noexcept {
                if constexpr (k_subnormal_control<S>) {
                    return (floating_point_control() & k_flush_to_zero_bits) == k_flush_to_zero_bits;
                }
                else {
                    return false;
                }
            }

            /// Whether the calling thread's setting is the one the build states (AOU-008):
            /// with CATENAE_FLUSH_TO_ZERO on, every bit set; off, none of them. True where no
            /// bit governs S: there is nothing to differ.
            template <typename S>
            inline bool flush_to_zero_as_stated() noexcept {
                if constexpr (k_subnormal_control<S>) {
                    const std::uint64_t bits = floating_point_control() & k_flush_to_zero_bits;
                    return bits == (CATENAE_FLUSH_TO_ZERO != 0 ? k_flush_to_zero_bits : std::uint64_t{0});
                }
                else {
                    return true;
                }
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_COMPILER_H
