/// @file stack_meter.h
/// @brief The measured stack high-water of one call, for the stack probes (bench/README.md).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Carried from SampleRateTap's tools/probe/stack_meter.h (NOTICE.md), names changed. Harness
// code, never shipped: it reads the stack pointer and writes below it, which is sound only on
// a single-threaded target with nothing that interrupts (the bare-metal Cortex-M legs under
// qemu-system-arm, where the stack probes run).
//
// high_water(f) paints the paint_bytes below its own stack pointer with a pattern, runs f, and
// returns how far below that pointer the deepest overwritten word lies: the bytes f's callees
// used, their return state, saved registers and frames, the precompiled runtime routines
// (libgcc, newlib) included, which the static analysis cannot see. f must reach the measured
// code through a noinline call, so its frames start at the pointer read here. The figure is
// the high-water of the path f took, not a bound on every path: the static worst path
// (scripts/ceilings.py) is that.
#ifndef CATENAE_TOOLS_PROBE_STACK_METER_H
#define CATENAE_TOOLS_PROBE_STACK_METER_H

#include <cstddef>
#include <cstdint>

namespace catenae_probe {

#ifndef CATENAE_PROBE_PAINT_BYTES
#define CATENAE_PROBE_PAINT_BYTES 8192
#endif

    /// The bytes painted: 8 KiB unless a probe whose calls go deeper says otherwise (the
    /// designers' probe: make()'s tail search takes 20 KiB at eight sections).
    inline constexpr std::size_t   paint_bytes = CATENAE_PROBE_PAINT_BYTES;
    inline constexpr std::uint32_t paint_word  = 0x5AA5C33Cu;

    [[gnu::always_inline]] inline std::uintptr_t stack_pointer() noexcept {
        std::uintptr_t sp = 0;
#if defined(__arm__)
        __asm__ volatile("mov %0, sp" : "=r"(sp));
#else
        sp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#endif
        return sp;
    }

    // A leaf: it paints from paint_bytes below the caller's pointer up to its own, so its frame
    // (empty on Arm) is never painted over.
    [[gnu::noinline]] inline void paint_below(std::uintptr_t top) noexcept {
        const std::uintptr_t lo   = (top - paint_bytes) & ~std::uintptr_t{3};
        const std::uintptr_t hi   = stack_pointer() & ~std::uintptr_t{3};
        auto* const          word = reinterpret_cast<volatile std::uint32_t*>(lo);
        for (std::size_t i = 0; i < (hi - lo) / 4; ++i) {
            word[i] = paint_word;
        }
    }

    /// Bytes of stack below this frame that f touched; paint_bytes when it reached the bottom
    /// of the painted region (a probe that sees this fails).
    template <typename F>
    [[gnu::noinline]] std::size_t high_water(F&& f) {
        const std::uintptr_t top = stack_pointer();
        paint_below(top);
        f();
        const std::uintptr_t lo   = (top - paint_bytes) & ~std::uintptr_t{3};
        const auto* const    word = reinterpret_cast<const volatile std::uint32_t*>(lo);
        const std::size_t    n    = (top - lo) / 4;
        std::size_t          i    = 0;
        while (i < n && word[i] == paint_word) {
            ++i;
        }
        return top - (lo + 4 * i);
    }

} // namespace catenae_probe

#endif // CATENAE_TOOLS_PROBE_STACK_METER_H
