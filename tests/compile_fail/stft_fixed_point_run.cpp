// Copyright 2026 Timothy Place and the catenae contributors
// A q31 STFT running: its contract is declared, fixed-point spectra arrive with their profile at
// C6 (D38, D47).
// Verifies CAT-STFT-004.
// Expect: catenae: fixed-point spectra arrive with their profile at C6
#include <array>
#include <cstdint>

#include "catenae/stft.h"

namespace cf {
#ifdef CATENAE_CF_CONTROL
    using stage_t = catenae::stft<64, 16, 1>;
    using v_t     = float;
    using c_t     = catenae::complex<float>;
#else
    using stage_t = catenae::stft_q31<64, 16, 1>;
    using v_t     = std::int32_t;
    using c_t     = catenae::complex<std::int32_t>;
#endif

    std::size_t ran(const stage_t& s, stage_t::state& st, const std::array<v_t, 16>& in,
                    std::array<c_t, 33>& out) noexcept;
    std::size_t ran(const stage_t& s, stage_t::state& st, const std::array<v_t, 16>& in,
                    std::array<c_t, 33>& out) noexcept {
        catenae::no_workspace ws{};
        return s.run(st, ws, catenae::span<const v_t>(in), catenae::span<c_t>(out));
    }
} // namespace cf
