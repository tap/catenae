/// @file design.h
/// @brief CF skeleton: bridge's phase table as a table design, made into the caller's storage.
///
/// | Contract   |                                                                                  |
/// |------------|----------------------------------------------------------------------------------|
/// | Ratio      | 44.1 -> 48 kHz only: L/M = 160/147 (bridge's `direction::up_to_48k`, K = 0)       |
/// | Type       | `design<S, Taps>`: the tap count per phase fixes the sizes, so it is a type (D37) |
/// | Table      | bridge's layout: ceil(L/2) = 80 stored rows of Taps, each tap-reversed; phase p  |
/// |            | >= 80 reads row L-1-p backward (the prototype is symmetric)                      |
/// | Sizes      | `table_size()` coefficients, `workspace_size()` doubles: `constexpr`              |
/// | make()     | `make(spec, table, workspace)`: double and libm, outside the audio path; writes  |
/// |            | nothing unless it succeeds; `size_mismatch` for short storage, `invalid_argument` |
/// |            | for a spec that is not a number or not positive, `out_of_range` for a passband   |
/// |            | at or above 24 kHz or an attenuation Kaiser's estimate puts beyond Taps          |
/// | from_table | a ROM table, checked without libm: finite, every row summing to 1 (1e-5); a      |
/// |            | temporary is refused                                                             |
/// | Held       | by view: the design is a pointer to the caller's table, which outlives it        |
/// | Method     | Kaiser-windowed sinc (Kaiser 1974), per-branch unity DC (bridge's design.h)      |
///
/// Carried from SampleRateTap 4496888 bridge/include/tap/sr/bridge/{design.h (the cutoff, the
/// checks of validate_profile, the per-branch normalisation), phase_table.h (the stored half,
/// is_mirrored, stored_index, the tap-reversed rows)} and DspTap b26a7db include/tap/dsp/kaiser.h
/// (bessel_i0, kaiser_beta, sinc, design_prototype, the length estimate of estimate_taps).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_BRIDGE_DESIGN_H
#define CATENAE_FIT_BRIDGE_DESIGN_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "catenae/check.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae_fit::bridge {

    /// L: the phases, and the superblock's outputs.
    inline constexpr std::size_t phases = 160;
    /// M: the input frames a superblock takes.
    inline constexpr std::size_t decimation = 147;
    /// The input rate the spec's Hz are read against.
    inline constexpr double input_rate_hz = 44100.0;
    /// The anti-image stopband edge: the output Nyquist.
    inline constexpr double stopband_edge_hz = 24000.0;

    /// What a design is made from: bridge's profile less its tap counts, which are the type's.
    struct spec {
        double passband_hz;       ///< the edge of the flat passband, in Hz
        double stopband_atten_db; ///< the prototype's stopband target, in dB
    };

    namespace detail {

        constexpr double k_pi = 3.14159265358979323846;

        /// The modified Bessel function of the first kind, order zero, by its power series.
        inline double bessel_i0(double x) noexcept {
            const double half_x = 0.5 * x;
            double       term   = 1.0;
            double       sum    = 1.0;
            for (int k = 1; k < 1000; ++k) {
                const double r = half_x / static_cast<double>(k);
                term *= r * r;
                sum += term;
                if (term < 1e-21 * sum) {
                    break;
                }
            }
            return sum;
        }

        /// Kaiser's shape parameter for a stopband attenuation in dB (Kaiser 1974).
        inline double kaiser_beta(double atten_db) noexcept {
            if (atten_db > 50.0) {
                return 0.1102 * (atten_db - 8.7);
            }
            if (atten_db > 21.0) {
                return 0.5842 * std::pow(atten_db - 21.0, 0.4) + 0.07886 * (atten_db - 21.0);
            }
            return 0.0;
        }

        /// sin(pi x) / (pi x), 1 at 0.
        inline double sinc(double x) noexcept {
            if (std::abs(x) < 1e-12) {
                return 1.0;
            }
            const double px = k_pi * x;
            return std::sin(px) / px;
        }

        /// The attenuation Kaiser's length estimate gives `taps` per phase, at this passband
        /// edge: estimate_taps inverted, (A - 8) / (2.285 * 2 pi * df) = taps.
        inline double reachable_atten_db(double passband_hz, std::size_t taps) noexcept {
            const double width = (stopband_edge_hz - passband_hz) / input_rate_hz;
            return 8.0 + 2.285 * 2.0 * k_pi * width * static_cast<double>(taps);
        }

        /// The Kaiser-windowed sinc prototype over L x taps points, normalised to sum L, then
        /// every polyphase branch to a coefficient sum of exactly 1.
        inline void design_prototype(catenae::span<double> h, std::size_t taps, double cutoff_norm,
                                     double beta) noexcept {
            const std::size_t n       = h.size();
            const double      center  = 0.5 * static_cast<double>(n - 1);
            const double      i0_beta = bessel_i0(beta);
            double            sum     = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const double u = (static_cast<double>(i) - center) / center;
                const double w = bessel_i0(beta * std::sqrt(1.0 - u * u > 0.0 ? 1.0 - u * u : 0.0)) / i0_beta;
                const double t = (static_cast<double>(i) - center) / static_cast<double>(phases);
                h[i]           = cutoff_norm * sinc(cutoff_norm * t) * w;
                sum += h[i];
            }
            const double gain = static_cast<double>(phases) / sum;
            for (std::size_t i = 0; i < n; ++i) {
                h[i] *= gain;
            }
            for (std::size_t ph = 0; ph < phases; ++ph) {
                double branch = 0.0;
                for (std::size_t t = 0; t < taps; ++t) {
                    branch += h[t * phases + ph];
                }
                const double unity = 1.0 / branch;
                for (std::size_t t = 0; t < taps; ++t) {
                    h[t * phases + ph] *= unity;
                }
            }
        }

    } // namespace detail

    /// @brief bridge's polyphase table, `Taps` per phase, held by view of the caller's storage.
    template <typename S, std::size_t Taps>
    class design {
        static_assert(Taps >= 4, "catenae_fit: bridge's designs have at least 4 taps per phase");

      public:
        using format                             = S;                     ///< the sample format it is for (D37)
        using coeff                              = catenae::storage_t<S>; ///< a coefficient, in the kernel's type
        static constexpr std::size_t taps        = Taps;                  ///< per phase: MACs per output
        static constexpr std::size_t stored_rows = (phases + 1) / 2;      ///< the half that is kept
        /// A table in ROM, for from_table.
        using table_type = std::array<coeff, stored_rows * Taps>;

        /// The coefficients the caller's table holds.
        static constexpr std::size_t table_size() noexcept { return stored_rows * Taps; }
        /// The doubles make() needs as its workspace: the whole prototype.
        static constexpr std::size_t workspace_size() noexcept { return phases * Taps; }

        /// Designs into `storage` (at least table_size()) through `scratch` (at least
        /// workspace_size()). Writes nothing unless it succeeds.
        static catenae::expected<design, catenae::status> make(const spec& wanted, catenae::span<coeff> storage,
                                                               catenae::span<double> scratch) noexcept {
            if (storage.size() < table_size() || scratch.size() < workspace_size()) {
                return catenae::unexpected(catenae::status::size_mismatch);
            }
            if (!(std::isfinite(wanted.passband_hz) && std::isfinite(wanted.stopband_atten_db))
                || !(wanted.passband_hz > 0.0) || !(wanted.stopband_atten_db > 0.0)) {
                return catenae::unexpected(catenae::status::invalid_argument);
            }
            if (!(wanted.passband_hz < stopband_edge_hz)
                || detail::reachable_atten_db(wanted.passband_hz, Taps) < wanted.stopband_atten_db) {
                return catenae::unexpected(catenae::status::out_of_range);
            }
            const catenae::span<double> proto = scratch.first(workspace_size());
            detail::design_prototype(proto, Taps, (wanted.passband_hz + stopband_edge_hz) / input_rate_hz,
                                     detail::kaiser_beta(wanted.stopband_atten_db));
            for (std::size_t ph = 0; ph < stored_rows; ++ph) {
                for (std::size_t t = 0; t < Taps; ++t) {
                    storage[ph * Taps + (Taps - 1 - t)] = static_cast<coeff>(proto[t * phases + ph]);
                }
            }
            return design(storage.data());
        }

        /// A table in ROM, checked without libm: a bad one that initialises a constant does not
        /// compile, and one met at run time reaches the handler.
        static constexpr design from_table(const table_type& rom) noexcept {
            for (std::size_t row = 0; row < stored_rows; ++row) {
                coeff sum{};
                for (std::size_t t = 0; t < Taps; ++t) {
                    const coeff c = rom[row * Taps + t];
                    // CF finding: check.h has no code for a table that fails its check (PLAN.md
                    // 3.3 says one met at run time reaches the handler); invariant stands in.
                    catenae::detail::check_always(c > coeff{-4} && c < coeff{4}, catenae::check::invariant);
                    sum += c;
                }
                catenae::detail::check_always(sum > coeff{1} - static_cast<coeff>(1e-5)
                                                  && sum < coeff{1} + static_cast<coeff>(1e-5),
                                              catenae::check::invariant);
            }
            return design(rom.data());
        }
        /// A temporary table would leave the design viewing nothing.
        static design from_table(const table_type&&) = delete;

        /// Whether phase `ph` reads its partner's stored row backward.
        static constexpr bool is_mirrored(std::size_t ph) noexcept { return ph >= stored_rows; }
        /// The stored row that serves phase `ph`.
        static constexpr std::size_t stored_index(std::size_t ph) noexcept {
            return ph < stored_rows ? ph : phases - 1 - ph;
        }
        /// The stored row serving phase `ph`, tap-reversed: read forward against a window whose
        /// oldest frame is first, or backward when the phase is mirrored.
        constexpr catenae::span<const coeff> row(std::size_t ph) const noexcept {
            return catenae::span<const coeff>(m_rows + stored_index(ph) * Taps, Taps);
        }
        /// The whole stored table.
        constexpr catenae::span<const coeff> table() const noexcept {
            return catenae::span<const coeff>(m_rows, table_size());
        }

      private:
        explicit constexpr design(const coeff* rows) noexcept
            : m_rows(rows) {}

        const coeff* m_rows; // the caller's table: a view (PLAN.md 3.3)
    };

} // namespace catenae_fit::bridge

#endif // CATENAE_FIT_BRIDGE_DESIGN_H
