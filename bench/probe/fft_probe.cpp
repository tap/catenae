/// @file fft_probe.cpp
/// @brief The FFT's size and stack probe, and the FFT family's image for the link-stage gate.
// Copyright 2026 Timothy Place and the catenae contributors
//
// A float transform of 512 from the constant tables, both ways, and a plan of 512 made at run
// time into static storage (bench/CMakeLists.txt builds it twice; scripts/ceilings.py measures
// both). The calls:
//
//   forward  fft_plan<float>::of<512>().forward over a static frame
//   inverse  the same plan's inverse
//   make     fft_plan<float>::make(512, workspace): the tables built at run time
//
// The image is the FFT family's for the link-stage gate (D36): float arithmetic and integer
// tables only, so no software-double or libm routine may link (link_gate.FftImageHasNoSoftwareDouble).
#include <cstddef>

#include "call_marker.h"
#include "catenae/compiler.h"
#include "catenae/fft.h"
#include "catenae/span.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#endif

namespace catenae_probe {

    constexpr std::size_t fft_n = 512;

    CATENAE_PROBE_NOINLINE void probe_forward() noexcept;
    CATENAE_PROBE_NOINLINE void probe_inverse() noexcept;
    CATENAE_PROBE_NOINLINE void probe_make() noexcept;

    namespace {
        CATENAE_CONSTINIT catenae::complex<float> s_frame[fft_n / 2u + 1u]{};
        CATENAE_CONSTINIT float                   s_workspace[catenae::fft_workspace_size<float>(fft_n)]{};
        volatile int                              g_sink = 0;
    } // namespace

    CATENAE_PROBE_NOINLINE void probe_forward() noexcept {
        catenae::fft_plan<float>::of<fft_n>().forward(s_frame);
    }

    CATENAE_PROBE_NOINLINE void probe_inverse() noexcept {
        g_sink = catenae::fft_plan<float>::of<fft_n>().inverse(s_frame);
    }

    CATENAE_PROBE_NOINLINE void probe_make() noexcept {
        const auto made = catenae::fft_plan<float>::make(fft_n, catenae::span<float>(s_workspace));
        g_sink          = made.has_value() ? static_cast<int>(made->size()) : 0;
    }

} // namespace catenae_probe

int main() {
    for (std::size_t i = 0; i < catenae_probe::fft_n / 2u; ++i) {
        catenae_probe::s_frame[i] = {0.25f, -0.125f};
    }
#if CATENAE_PROBE_STACK
    const std::size_t forward = catenae_probe::high_water([] { catenae_probe::probe_forward(); });
    const std::size_t inverse = catenae_probe::high_water([] { catenae_probe::probe_inverse(); });
    const std::size_t make    = catenae_probe::high_water([] { catenae_probe::probe_make(); });
    std::printf("CATENAE_STACK forward %lu\n", static_cast<unsigned long>(forward));
    std::printf("CATENAE_STACK inverse %lu\n", static_cast<unsigned long>(inverse));
    std::printf("CATENAE_STACK make %lu\n", static_cast<unsigned long>(make));
    const bool ok = forward < catenae_probe::paint_bytes && inverse < catenae_probe::paint_bytes
                    && make < catenae_probe::paint_bytes;
    std::printf("CATENAE_PROBE_DONE ok=%d\n", ok ? 1 : 0);
#else
    catenae_probe::probe_forward();
    catenae_probe::probe_inverse();
    catenae_probe::probe_make();
#endif
    return 0;
}
