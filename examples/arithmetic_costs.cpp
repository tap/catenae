/// @file arithmetic_costs.cpp
/// @brief One low-pass in the golden model, the float profile, Q15, Q31 and Q4.27, each held
/// against the golden model (guide: What arithmetic costs; D24, D29, D36).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "catenae/chain.h"
#include "catenae/convert.h"
#include "catenae/expected.h"
#include "catenae/fir.h"
#include "catenae/fir_design.h"
#include "fir_tables.h"

namespace {

    constexpr std::size_t k_frames = 512;
    using samples                  = std::array<float, k_frames>;

    // ANCHOR: golden
    // The golden model: the design and every product and sum in double. The battery holds each
    // other profile against it. On a core whose FPU has no double, each multiply and each add
    // here is a call to a software routine.
    catenae::expected<catenae::fir_design<double, 63>, catenae::status> golden_design() noexcept {
        return catenae::fir_design<double, 63>::make(catenae::fir_lowpass{0.1, 60.0});
    }
    // ANCHOR_END: golden

    // ANCHOR: float_profile
    // The float profile: the taps a table in ROM, every product and sum in float (four partial
    // sums, D36), so the filter never leaves a single-precision FPU.
    constexpr auto k_lowpass = catenae::chain(catenae::fir<1, 63>{example::fir_lowpass});
    // ANCHOR_END: float_profile

    // ANCHOR: q15
    // Q15: 16-bit samples with the binary point after the sign, the taps Q1.14 (one bit of
    // headroom for a tap near 1.0), the sum exact in 64 bits and rounded once. convert rounds
    // once at each edge.
    constexpr auto k_lowpass_q15 = catenae::convert<float, catenae::q15, 1>{}
                                   | catenae::fir_q15<1, 63>{example::fir_lowpass_q15}
                                   | catenae::convert<catenae::q15, float, 1>{};
    // ANCHOR_END: q15

    // ANCHOR: headroom
    // The same 32-bit words and the same arithmetic with the binary point in another place: Q31
    // holds -1 to 1, Q4.27 holds -16 to 16 with 24 dB less resolution. The kernel never knows.
    template <typename Q>
    constexpr auto through(const catenae::fir_design<Q, 63>& design) noexcept {
        return catenae::convert<float, Q, 1>{} | catenae::basic_fir<Q, 1, 63>{design} | catenae::convert<Q, float, 1>{};
    }
    // ANCHOR_END: headroom

    /// A path from reset over the whole input.
    template <typename Path>
    samples run(const Path& path, const samples& in) noexcept {
        static catenae::memory_of<Path> memory{};
        path.reset(memory.state);
        samples out{};
        static_cast<void>(path.process(memory, in, out));
        return out;
    }

    /// Pseudo-random samples at `level` of full scale.
    samples noise(float level) noexcept {
        samples       s{};
        std::uint32_t seed = 1;
        for (float& v : s) {
            seed = seed * 1664525u + 1013904223u;
            v    = level * (static_cast<float>(seed / 256u) / 8388608.0f - 1.0f);
        }
        return s;
    }

    /// The golden model's output for `in`, in double.
    std::array<double, k_frames> golden(const catenae::fir_design<double, 63>& design, const samples& in) noexcept {
        static catenae::fir_f64<1, 63>::state st{};
        st = {};
        catenae::no_workspace        ws{};
        std::array<double, k_frames> x{};
        std::array<double, k_frames> y{};
        for (std::size_t i = 0; i < k_frames; ++i) {
            x[i] = static_cast<double>(in[i]);
        }
        const catenae::fir_f64<1, 63> filter{design};
        static_cast<void>(filter.run(st, ws, x, y));
        return y;
    }

    /// The largest distance of `out` from the golden model.
    double distance(const samples& out, const std::array<double, k_frames>& want) noexcept {
        double worst = 0.0;
        for (std::size_t i = 0; i < k_frames; ++i) {
            worst = std::fmax(worst, std::fabs(static_cast<double>(out[i]) - want[i]));
        }
        return worst;
    }

} // namespace

int main() {
    const auto design = golden_design();
    const auto q31    = catenae::fir_design<catenae::q31, 63>::make(catenae::fir_lowpass{0.1, 60.0});
    const auto q4_27  = catenae::fir_design<catenae::q4_27, 63>::make(catenae::fir_lowpass{0.1, 60.0});
    if (!design || !q31 || !q4_27) {
        return 1;
    }
    // At -6 dBFS: the float profile and Q15 against the golden model.
    const samples quiet       = noise(0.5f);
    const auto    want        = golden(*design, quiet);
    const double  float_error = distance(run(k_lowpass, quiet), want);
    const double  q15_error   = distance(run(k_lowpass_q15, quiet), want) * 32768.0;
    std::printf("float profile: %.2g from the golden model (%.1f dBFS)\n", float_error, 20.0 * std::log10(float_error));
    std::printf("q15: %.1f LSB from the golden model\n", q15_error);
    // At +6 dBFS, above Q31's full scale: Q31 clips the input, Q4.27 carries it.
    const samples loud        = noise(2.0f);
    const auto    want_loud   = golden(*design, loud);
    const double  q31_error   = distance(run(through(*q31), loud), want_loud);
    const double  q4_27_error = distance(run(through(*q4_27), loud), want_loud);
    std::printf("q31 at +6 dBFS: %.2g from the golden model\n", q31_error);
    std::printf("q4.27 at +6 dBFS: %.2g from the golden model\n", q4_27_error);
    const bool ok = float_error < 1.4e-7 && q15_error < 6.0 && q31_error > 0.1 && q4_27_error < 1e-6;
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
