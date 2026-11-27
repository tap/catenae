// Copyright 2026 Timothy Place and the catenae contributors
// A q31 FFT FIR run: its contract is declared, its arithmetic arrives with its profile at C6
// (D47).
// Verifies CAT-FFT-FIR-005.
// Expect: catenae: a fixed-point FFT FIR is new design
#include <array>
#include <cstddef>
#include <cstdint>

#include "catenae/fft_fir.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage_t = catenae::fft_fir<2, 3, 2>;
    using v_t     = float;
#else
    using stage_t = catenae::fft_fir_q31<2, 3, 2>;
    using v_t     = std::int32_t;
#endif

    std::size_t ran(const stage_t& s, stage_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept;
    std::size_t ran(const stage_t& s, stage_t::state& st, std::array<v_t, 4>& in, std::array<v_t, 4>& out) noexcept {
        stage_t::workspace ws{};
        return s.run(st, ws, catenae::span<const v_t>(in), catenae::span<v_t>(out));
    }
} // namespace cf
