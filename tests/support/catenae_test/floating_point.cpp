/// @file floating_point.cpp
/// @brief The battery's flush-to-zero setting: the build's statement, applied at start-up.
// Copyright 2026 Timothy Place and the catenae contributors
#include "catenae_test/floating_point.h"

#include <cstdint>

#include "catenae/compiler.h"
#include "catenae/config.h"

#if defined(CATENAE_FP_CONTROL_AARCH64) && defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace catenae_test {

    bool set_flush_to_zero(bool on) noexcept {
        const std::uint64_t bits = catenae::detail::k_flush_to_zero_bits;
        const std::uint64_t was  = catenae::detail::floating_point_control();
        const std::uint64_t now  = on ? (was | bits) : (was & ~bits);
#if defined(CATENAE_FP_CONTROL_X86_64)
        _mm_setcsr(static_cast<unsigned int>(now));
#elif defined(CATENAE_FP_CONTROL_AARCH64)
#if defined(_MSC_VER) && !defined(__clang__)
        _WriteStatusReg(ARM64_FPCR, static_cast<std::int64_t>(now));
#else
        __asm__ volatile("msr fpcr, %0" : : "r"(now));
#endif
#elif defined(CATENAE_FP_CONTROL_AARCH32)
        __builtin_arm_set_fpscr(static_cast<unsigned int>(now));
#else
        static_cast<void>(now);
#endif
        return bits != 0 && (was & bits) == bits;
    }

    namespace {
        /// The host's setting before any test runs, as the build states it.
        struct apply_the_build {
            apply_the_build() noexcept { static_cast<void>(set_flush_to_zero(CATENAE_FLUSH_TO_ZERO != 0)); }
        };
        const apply_the_build k_at_start_up;
    } // namespace

} // namespace catenae_test
