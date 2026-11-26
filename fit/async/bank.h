/// @file bank.h
/// @brief The async skeleton's polyphase bank: a table design type whose table is a constant (CF).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Design    | a type: `bank<Phases, Taps>` names its table and its tap count (PLAN.md 3.3)      |
/// | Table     | `(Phases + 1) * Taps` floats, rows tap-reversed, row `Phases` row 0 one frame on |
/// | Prototype | Kaiser-windowed sinc, cutoff 1.0 (18 + 30 kHz at 48 kHz), beta for 96 dB         |
/// | Latency   | group delay `(Phases * Taps - 1) / (2 Phases)` input frames                     |
/// | Limits    | no quality bar (CF): 32 x 16 is the size of a test, not of a profile             |
///
/// Carried from DspTap b26a7db, include/tap/dsp/kaiser.h (`bessel_i0`, `sinc`,
/// `design_prototype`), and SampleRateTap 4496888, async/include/tap/sr/async/polyphase_filter.h
/// (`polyphase_filter_bank`'s table layout: rows tap-reversed, the extra row). The design runs
/// as `constexpr` over `detail::cx_sin` and `detail::cx_sqrt` in place of `std::sin` and
/// `std::sqrt`, so the table is a constant in ROM and nothing designs it at run time.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_ASYNC_BANK_H
#define CATENAE_FIT_ASYNC_BANK_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace catenae_fit {
    namespace async {
        namespace detail {

            inline constexpr double k_pi = 3.14159265358979323846;

            /// The square root by Newton's iteration from above: it falls until it settles.
            constexpr double cx_sqrt(double x) noexcept {
                if (x <= 0.0) {
                    return 0.0;
                }
                double root = x > 1.0 ? x : 1.0;
                for (int i = 0; i < 200; ++i) {
                    const double next = 0.5 * (root + x / root);
                    if (!(next < root)) {
                        break;
                    }
                    root = next;
                }
                return root;
            }

            /// The sine, reduced to [-pi, pi] and summed as its Taylor series.
            constexpr double cx_sin(double x) noexcept {
                const double turns = x / (2.0 * k_pi);
                const double whole = static_cast<double>(static_cast<std::int64_t>(turns + (turns < 0.0 ? -0.5 : 0.5)));
                const double r     = x - whole * 2.0 * k_pi;
                double       term  = r;
                double       sum   = r;
                for (int k = 1; k < 24; ++k) {
                    term *= -r * r / static_cast<double>((2 * k) * (2 * k + 1));
                    sum += term;
                }
                return sum;
            }

            /// The modified Bessel function of the first kind, order 0: DspTap's series.
            constexpr double bessel_i0(double x) noexcept {
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

            /// sin(pi x) / (pi x), 1 at 0.
            constexpr double sinc(double x) noexcept {
                if (x < 1e-12 && x > -1e-12) {
                    return 1.0;
                }
                const double px = k_pi * x;
                return cx_sin(px) / px;
            }

            /// The bank's table: DspTap's `design_prototype` over `Phases * Taps` taps, then
            /// SampleRateTap's layout, each coefficient its plain conversion to float.
            template <std::size_t Phases, std::size_t Taps>
            constexpr std::array<float, (Phases + 1) * Taps> design_table(double cutoff_norm, double beta) noexcept {
                constexpr std::size_t                  n = Phases * Taps;
                std::array<double, n>                  proto{};
                const double                           center  = 0.5 * static_cast<double>(n - 1);
                const double                           i0_beta = bessel_i0(beta);
                double                                 sum     = 0.0;
                std::array<float, (Phases + 1) * Taps> table{};
                for (std::size_t i = 0; i < n; ++i) {
                    const double u = (static_cast<double>(i) - center) / center;
                    const double w = bessel_i0(beta * cx_sqrt(1.0 - u * u)) / i0_beta;
                    const double t = (static_cast<double>(i) - center) / static_cast<double>(Phases);
                    proto[i]       = cutoff_norm * sinc(cutoff_norm * t) * w;
                    sum += proto[i];
                }
                const double gain = static_cast<double>(Phases) / sum;
                for (std::size_t p = 0; p < Phases; ++p) {
                    for (std::size_t t = 0; t < Taps; ++t) {
                        table[p * Taps + (Taps - 1 - t)] = static_cast<float>(proto[t * Phases + p] * gain);
                    }
                }
                // The extra row is row 0 advanced one input frame, so the blend between rows
                // Phases - 1 and Phases, and the wrap of the phase, need no branch.
                table[Phases * Taps] = 0.0f;
                for (std::size_t u = 1; u < Taps; ++u) {
                    table[Phases * Taps + u] = table[u - 1];
                }
                return table;
            }

            /// log2 of a power of two.
            constexpr std::size_t log2_of(std::size_t x) noexcept {
                std::size_t bits = 0;
                while (x > 1) {
                    x >>= 1u;
                    ++bits;
                }
                return bits;
            }

        } // namespace detail

        /// @brief A polyphase Kaiser-sinc bank of `Phases` branches of `Taps` taps: a table design
        /// that fixes a size, so a type (PLAN.md 3.3), its table a constant of the type.
        template <std::size_t Phases, std::size_t Taps>
        struct bank {
            static_assert(Phases >= 2 && (Phases & (Phases - 1)) == 0, "a bank's phases are a power of two");
            static_assert(Taps >= 4, "a bank has at least four taps a phase");
            static constexpr std::size_t phases      = Phases;                        ///< L
            static constexpr std::size_t taps        = Taps;                          ///< T
            static constexpr std::size_t phase_bits  = detail::log2_of(Phases);       ///< log2 L
            static constexpr double      cutoff_norm = (18000.0 + 30000.0) / 48000.0; ///< (pass + stop) / rate
            static constexpr double      beta        = 0.1102 * (96.0 - 8.7);         ///< Kaiser's, for 96 dB
            /// The rows: `Phases + 1` of `Taps` coefficients, each tap-reversed.
            static constexpr std::array<float, (Phases + 1) * Taps> table =
                detail::design_table<Phases, Taps>(cutoff_norm, beta);
            /// The linear-phase group delay in input frames, about Taps / 2.
            static constexpr float group_delay_frames =
                static_cast<float>(static_cast<double>(Phases * Taps - 1) / (2.0 * static_cast<double>(Phases)));
        };

        /// The skeleton's bank: 32 phases of 16 taps, 2,112 bytes of table.
        using bank_32x16 = bank<32, 16>;

    } // namespace async
} // namespace catenae_fit

#endif // CATENAE_FIT_ASYNC_BANK_H
