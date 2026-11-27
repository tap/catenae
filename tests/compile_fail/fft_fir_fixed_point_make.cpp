// Copyright 2026 Timothy Place and the catenae contributors
// A q31 FFT FIR design from a q31 FIR design: the spectrum's format is new design and arrives
// with its profile at C6 (D47).
// Verifies CAT-FFT-FIR-DESIGN-004.
// Expect: catenae: a fixed-point FFT FIR's spectrum is new design
#include <array>
#include <cstdint>

#include "catenae/fft_fir_design.h"
#include "catenae/fir_design.h"
#include "catenae/sample_format.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using format = float;
#else
    using format = catenae::q31;
#endif
    using taps_t   = catenae::fir_design<format, 3>;
    using design_t = catenae::fft_fir_design<format, 3, 2>;

    catenae::expected<design_t, catenae::status> made(const taps_t& taps, std::array<design_t::bin_type, 3>& table,
                                                      std::array<catenae::complex<double>, 3>& work) noexcept;
    catenae::expected<design_t, catenae::status> made(const taps_t& taps, std::array<design_t::bin_type, 3>& table,
                                                      std::array<catenae::complex<double>, 3>& work) noexcept {
        return design_t::make(taps, catenae::span<design_t::bin_type, 3>(table),
                              catenae::span<catenae::complex<double>, 3>(work));
    }
} // namespace cf
