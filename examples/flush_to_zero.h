/// @file flush_to_zero.h
/// @brief What a host does once on each audio thread before it runs: flush subnormal results
/// to zero (guide: The audio callback). catenae only reads the setting (AOU-008).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_EXAMPLES_FLUSH_TO_ZERO_H
#define CATENAE_EXAMPLES_FLUSH_TO_ZERO_H

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#elif (defined(__aarch64__) || defined(_M_ARM64)) && defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#include <cstdint>

namespace example {

    // ANCHOR: host
    /// The calling thread's floating point flushes subnormal results to zero from now on: the
    /// control register's FTZ and DAZ bits on x86-64, FZ on Arm. A host does this once per
    /// audio thread; on a core with no such bit (software float, Hexagon) there is nothing to set.
    inline void flush_subnormals_on_this_thread() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_setcsr(_mm_getcsr() | 0x8040u);
#elif (defined(__aarch64__) || defined(_M_ARM64)) && defined(_MSC_VER) && !defined(__clang__)
        _WriteStatusReg(ARM64_FPCR, _ReadStatusReg(ARM64_FPCR) | (std::int64_t{1} << 24));
#elif defined(__aarch64__)
        std::uint64_t fpcr = 0;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (std::uint64_t{1} << 24u)));
#elif defined(__arm__) && defined(__ARM_FP)
        __builtin_arm_set_fpscr(__builtin_arm_get_fpscr() | (1u << 24u));
#endif
    }
    // ANCHOR_END: host

} // namespace example

#endif // CATENAE_EXAMPLES_FLUSH_TO_ZERO_H
