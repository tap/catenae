/// @file designers_probe.cpp
/// @brief What the designers' make() costs in stack (PLAN.md 3.3: on each design's card): one
/// cookbook section, the Butterworth at each section count, and three FIR designs.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Each call is one make() through the compiled designers (src/biquad_design.cpp) and the
// header tail search (catenae/detail/recursive_tail.h), whose matrices are on the stack: its
// static worst path and its painted high-water are each a figure of this probe set
// (scripts/ceilings.py). Painted over 32 KiB: the search takes about 20 KiB at eight sections.
// An FIR's make() designs half its taps in double, on the stack by value (63 float taps, 255
// Q15 taps) or in the caller's workspace (1,023 float taps into a table).
// This image links the designers' software double and libm; the recursive and FIR probes are
// the ones the link-stage gate reads.
#include <array>
#include <cstddef>

#include "call_marker.h"
#include "catenae/biquad_design.h"
#include "catenae/fir_design.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#define CATENAE_PROBE_PAINT_BYTES 32768
#include "stack_meter.h"
#endif

namespace catenae_probe {

    CATENAE_PROBE_NOINLINE bool probe_cookbook() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_butterworth_2() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_butterworth_4() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_butterworth_8() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_butterworth_16() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_fir_63() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_fir_q15_255() noexcept;
    CATENAE_PROBE_NOINLINE bool probe_fir_1023() noexcept;

    namespace {
        using long_fir = catenae::fir_design<float, 1023>;
        std::array<float, 1023>                      s_fir_table{};
        std::array<double, long_fir::workspace_size> s_fir_workspace{};
    } // namespace

    CATENAE_PROBE_NOINLINE bool probe_cookbook() noexcept {
        return catenae::biquad_design<float>::make({catenae::biquad_response::peaking, 0.05, 2.0, 6.0}).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_butterworth_2() noexcept {
        return catenae::butterworth_design<float, 2>::make(0.1).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_butterworth_4() noexcept {
        return catenae::butterworth_design<float, 4>::make(0.1).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_butterworth_8() noexcept {
        return catenae::butterworth_design<float, 8>::make(0.1).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_butterworth_16() noexcept {
        return catenae::butterworth_design<float, 16>::make(0.1).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_fir_63() noexcept {
        return catenae::fir_design<float, 63>::make(catenae::fir_lowpass{0.1, 60.0}).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_fir_q15_255() noexcept {
        return catenae::fir_design<catenae::q15, 255>::make(catenae::fir_lowpass{0.05, 80.0}).has_value();
    }
    CATENAE_PROBE_NOINLINE bool probe_fir_1023() noexcept {
        return long_fir::make(catenae::fir_lowpass{0.02, 100.0}, catenae::span<float, 1023>(s_fir_table),
                              catenae::span<double, long_fir::workspace_size>(s_fir_workspace))
            .has_value();
    }

} // namespace catenae_probe

int main() {
#if CATENAE_PROBE_STACK
    bool              made     = true;
    const std::size_t cookbook = catenae_probe::high_water([&made] { made = catenae_probe::probe_cookbook() && made; });
    const std::size_t b2 = catenae_probe::high_water([&made] { made = catenae_probe::probe_butterworth_2() && made; });
    const std::size_t b4 = catenae_probe::high_water([&made] { made = catenae_probe::probe_butterworth_4() && made; });
    const std::size_t b8 = catenae_probe::high_water([&made] { made = catenae_probe::probe_butterworth_8() && made; });
    const std::size_t b16 =
        catenae_probe::high_water([&made] { made = catenae_probe::probe_butterworth_16() && made; });
    std::printf("CATENAE_STACK cookbook %lu\n", static_cast<unsigned long>(cookbook));
    std::printf("CATENAE_STACK butterworth_2 %lu\n", static_cast<unsigned long>(b2));
    std::printf("CATENAE_STACK butterworth_4 %lu\n", static_cast<unsigned long>(b4));
    std::printf("CATENAE_STACK butterworth_8 %lu\n", static_cast<unsigned long>(b8));
    std::printf("CATENAE_STACK butterworth_16 %lu\n", static_cast<unsigned long>(b16));
    const std::size_t f63   = catenae_probe::high_water([&made] { made = catenae_probe::probe_fir_63() && made; });
    const std::size_t f255  = catenae_probe::high_water([&made] { made = catenae_probe::probe_fir_q15_255() && made; });
    const std::size_t f1023 = catenae_probe::high_water([&made] { made = catenae_probe::probe_fir_1023() && made; });
    std::printf("CATENAE_STACK fir_63 %lu\n", static_cast<unsigned long>(f63));
    std::printf("CATENAE_STACK fir_q15_255 %lu\n", static_cast<unsigned long>(f255));
    std::printf("CATENAE_STACK fir_1023 %lu\n", static_cast<unsigned long>(f1023));
    const bool ok = made && b16 < catenae_probe::paint_bytes && f255 < catenae_probe::paint_bytes;
    std::printf("CATENAE_PROBE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
#else
    const bool made = catenae_probe::probe_cookbook() && catenae_probe::probe_butterworth_2()
                      && catenae_probe::probe_butterworth_4() && catenae_probe::probe_butterworth_8()
                      && catenae_probe::probe_butterworth_16() && catenae_probe::probe_fir_63()
                      && catenae_probe::probe_fir_q15_255() && catenae_probe::probe_fir_1023();
    return made ? 0 : 1;
#endif
}
