/// @file design.h
/// @brief Nyquist designs as types, a profile's pins by design divisor, and the divisor rule (CF).
// Copyright 2026 Timothy Place and the catenae contributors
//
// A design that fixes a size is a type (D37, PLAN.md 3.3): `nyquist_design<Band, M>` names the
// generated table of the Band-th-band (Nyquist) filter with M taps per branch, N = 2 M Band - 1
// taps. Only the keys a generator has emitted exist; any other key fails to compile, which is
// how a stage at an unpinned divisor is refused (the CF row: "a stage created at an unpinned
// divisor" is dropped).
//
// The tables were generated once by DspTap's designer at b26a7db (include/tap/dsp/nyquist.h,
// design_nyquist, with SampleRateTap's design_beta: Kaiser's beta for 70 + 4 dB, 7.19606), by a
// host program printing each tap to 17 significant digits, and pasted. The prototype is the
// designer's double; the stage derives its rows from it at compile time, so only the rows, in
// the stage's format, reach ROM, and nothing at run time touches a double. CF has no quality bar
// (PLAN.md 10): m = 1 and 2 are far below SampleRateTap's pins (m = 3 to 12 at 70 dB).
//
// The divisor rule is SampleRateTap's (rational/include/tap/sr/rational/chain.h at 4496888,
// detail::design_divisor and detail::chain_rates): a chain's profile states its passband
// at the chain's lowest rate, so a stage whose lower rate is q times that is designed relaxed by
// the largest 2^a 3^b at or below q (q itself below 1, which only a chain through bridge meets).
// Here it is constexpr over catenae::ratio, and the profile is a type whose pins are looked up
// at compile time: run-time profile objects are not carried over (PLAN.md 3.3).
//
// CF finding 3: PLAN.md 3.3 has a generated table "one per profile and design divisor". The
// prototype depends only on the band, the taps per branch and the attenuation (the Kaiser beta;
// the passband only chooses m), so SampleRateTap's four profiles at their seven within-family
// divisors and five bands, 140 keys, are 46 tables (30 at 70 dB, 16 at 120 dB), and the
// profile's relaxation table is a constexpr lookup from (band, divisor) to m (design_at below).
// What the kernel runs, the rows, differs by ratio as well (1/M decimating, unit phases going
// down, layout and trimming), so it is the stage's constant, derived here at compile time from
// the double prototype: D37's "holds its coefficients in the kernel's type" fits the stage's
// table, not the design. For float and double C++17 constexpr does it; Q15 and Q31 rows need
// DspTap's row-sum-preserving quantization, whose std::llround is not constexpr, so they need a
// constexpr rounding of their own or a generator that emits the rows per stage.
#ifndef CATENAE_FIT_RATIONAL_DESIGN_H
#define CATENAE_FIT_RATIONAL_DESIGN_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "catenae/ratio.h"
#include "rational/ratio.h"

namespace catenae_fit::rational {

    namespace detail {
        template <std::size_t Band, std::size_t M>
        inline constexpr bool k_no_table = false;
    } // namespace detail

    /// The Band-th-band design with M taps per branch: a generated table, or no such type.
    template <std::size_t Band, std::size_t M>
    struct nyquist_design {
        static_assert(detail::k_no_table<Band, M>,
                      "catenae_fit::rational: no generated table at this band and length: a design is a type the "
                      "generator emitted, and a stage at an unpinned divisor does not compile");
    };

    /// The half-band at m = 1: linear interpolation, [1/2, 1, 1/2].
    template <>
    struct nyquist_design<2, 1> {
        static constexpr std::size_t           band            = 2;
        static constexpr std::size_t           taps_per_branch = 1;
        static constexpr std::size_t           taps            = 3;
        static constexpr std::array<double, 3> prototype       = {{0.5, 1.0, 0.5}};
    };

    /// The half-band at m = 2.
    template <>
    struct nyquist_design<2, 2> {
        static constexpr std::size_t           band            = 2;
        static constexpr std::size_t           taps_per_branch = 2;
        static constexpr std::size_t           taps            = 7;
        static constexpr std::array<double, 7> prototype       = {{
            -0.0012093657860519078,
            0.0,
            0.50120936578605191,
            1.0,
            0.50120936578605191,
            0.0,
            -0.0012093657860519078,
        }};
    };

    /// The third-band at m = 1.
    template <>
    struct nyquist_design<3, 1> {
        static constexpr std::size_t           band            = 3;
        static constexpr std::size_t           taps_per_branch = 1;
        static constexpr std::size_t           taps            = 5;
        static constexpr std::array<double, 5> prototype       = {{
            0.0059797440479544307,
            0.99402025595204557,
            1.0,
            0.99402025595204557,
            0.0059797440479544307,
        }};
    };

    /// The third-band at m = 2.
    template <>
    struct nyquist_design<3, 2> {
        static constexpr std::size_t            band            = 3;
        static constexpr std::size_t            taps_per_branch = 2;
        static constexpr std::size_t            taps            = 11;
        static constexpr std::array<double, 11> prototype       = {{
            -0.00086661036059017585,
            -0.016136206016203976,
            0.0,
            0.25139040077431651,
            0.76561241560247772,
            1.0,
            0.76561241560247772,
            0.25139040077431651,
            0.0,
            -0.016136206016203976,
            -0.00086661036059017585,
        }};
    };

    /// Whether a design's table has the Nyquist structure its stage relies on: N = 2 m B - 1,
    /// the centre tap exactly 1, every B-th tap from it exactly 0, and symmetric (exact linear
    /// phase, so the latency is exact). The check a table gets in place of `from_table()`.
    template <typename Design>
    constexpr bool is_nyquist_table() noexcept {
        constexpr std::size_t n = Design::taps;
        constexpr std::size_t b = Design::band;
        constexpr std::size_t c = (n - 1) / 2;
        if (n != 2 * Design::taps_per_branch * b - 1 || Design::prototype.size() != n) {
            return false;
        }
        const auto& h = Design::prototype;
        if (h[c] < 1.0 || h[c] > 1.0) {
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const bool structural = i != c && (i + b - c % b) % b == 0;
            if (structural && (h[i] < 0.0 || h[i] > 0.0)) {
                return false;
            }
            if (h[i] < h[n - 1 - i] || h[i] > h[n - 1 - i]) {
                return false;
            }
        }
        return true;
    }

    /// The design divisor of a stage whose lower rate over the chain's lowest is q: the largest
    /// 2^a 3^b at or below q, or q itself below 1.
    constexpr catenae::ratio design_divisor(catenae::ratio q) noexcept {
        if (q < catenae::ratio{1, 1}) {
            return catenae::reduce(q);
        }
        std::uint32_t best = 1;
        for (std::uint32_t three = 1; three <= 27; three *= 3) {
            for (std::uint64_t d = three; d * q.down <= q.up; d *= 2) {
                best = d > best ? static_cast<std::uint32_t>(d) : best;
            }
        }
        return catenae::ratio{best, 1};
    }

    /// The rates of a chain of the ratios Rs relative to its input, and each stage's divisor.
    template <typename... Rs>
    struct chain_rates {
        static constexpr std::size_t stages = sizeof...(Rs);

        /// rates[i] is the rate before stage i over the input's; rates[stages] the output's.
        static constexpr std::array<catenae::ratio, stages + 1> rates() noexcept {
            constexpr std::array<catenae::ratio, stages> each = {{Rs::value...}};
            std::array<catenae::ratio, stages + 1>       r{};
            r[0] = catenae::ratio{1, 1};
            for (std::size_t i = 0; i < stages; ++i) {
                r[i + 1] = r[i] * each[i];
            }
            return r;
        }
        /// The lowest rate of the chain.
        static constexpr catenae::ratio lowest() noexcept {
            const auto     r   = rates();
            catenae::ratio low = r[0];
            for (const catenae::ratio& x : r) {
                low = x < low ? x : low;
            }
            return low;
        }
        /// Stage i's design divisor: its lower rate over the lowest.
        static constexpr catenae::ratio divisor(std::size_t i) noexcept {
            const auto           r     = rates();
            const catenae::ratio lower = r[i] < r[i + 1] ? r[i] : r[i + 1];
            const catenae::ratio low   = lowest();
            return design_divisor(catenae::reduce(lower * catenae::ratio{low.down, low.up}));
        }
    };

    /// The skeleton's one profile, a type (no run-time profile object, PLAN.md 3.3): 70 dB, with
    /// m = 2 for the bands 2 and 3 at their own rate and m = 1 relaxed (divisor 2 and above). A
    /// real profile carries SampleRateTap's relaxation table here, the same lookup.
    struct fit_profile {
        /// The pinned taps per branch at a band and a design divisor; 0 where there is no pin.
        static constexpr std::size_t taps_per_branch(std::size_t band, catenae::ratio divisor) noexcept {
            if (band != 2 && band != 3) {
                return 0;
            }
            return divisor == catenae::ratio{1, 1} ? 2 : (divisor >= catenae::ratio{2, 1} ? 1 : 0);
        }
    };

    /// The design a profile pins for a stage at ratio R and design divisor Up/Down.
    template <typename Profile, typename R, std::uint32_t Up, std::uint32_t Down>
    using design_at = nyquist_design<ratio_traits<R>::band,
                                     Profile::taps_per_branch(ratio_traits<R>::band, catenae::ratio{Up, Down})>;

} // namespace catenae_fit::rational

#endif // CATENAE_FIT_RATIONAL_DESIGN_H
