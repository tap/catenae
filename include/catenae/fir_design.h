/// @file fir_design.h
/// @brief Direct-form FIR designs: a Kaiser-windowed low-pass, high-pass, band-pass or
/// band-stop, and a checked table (D37, PLAN.md 3.3).
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | A design    | `fir_design<S, Taps, Phase>`: the impulse response h[0] to h[Taps - 1] in   |
/// |             | `sample_traits`' coefficient type, made only by `make()` or `from_table()`; |
/// |             | held by value while the design fits 512 bytes, else viewed (`table_view`)   |
/// | Phase       | `fir_phase::linear`: h symmetric or antisymmetric, a delay of exactly        |
/// |             | (Taps - 1) / 2 frames; `fir_phase::any` (`fir_any_design`): any h, with the |
/// |             | latency its generator states                                                |
/// | Responses   | `fir_lowpass{cutoff, attenuation_db}`, `fir_highpass`, `fir_bandpass{low,   |
/// |             | high, attenuation_db}`, `fir_bandstop`: the ideal response's impulse response |
/// |             | times a Kaiser window, beta from the attenuation (Kaiser 1974; Oppenheim and |
/// |             | Schafer, 3rd ed., 7.5-7.6); the low-pass at unity gain at DC, the high-pass |
/// |             | delta - LP, the band-pass the difference of two low-passes, the band-stop    |
/// |             | delta - BP. Each edge is at half amplitude (-6 dB); the taps set the width  |
/// | Odd taps    | a high-pass or band-stop passes Nyquist, where an even count has a zero: a  |
/// |             | static_assert                                                               |
/// | Estimate    | `fir_taps_estimate(attenuation_db, transition)`, constexpr: Kaiser and     |
/// |             | harris's (A - 8) / (2.285 2 pi df), rounded up, at least 4 (DspTap)         |
/// | Rejects     | `status::invalid_argument`: a NaN or infinite argument, a band whose low    |
/// |             | edge is not below its high; `status::out_of_range`: an edge outside (0, 1/2) |
/// |             | cycles per sample, an attenuation outside 0 to 200 dB                       |
/// | Taps        | designed in double, half of them and mirrored, so h is symmetric bit for   |
/// |             | bit; float and double round each once; q15, q31 and Q4.27 take Q1.14 or      |
/// |             | Q1.30 by DspTap's row-sum-preserving quantization of the first half, and with |
/// |             | an odd count the centre takes what makes the row sum the design's, rounded  |
/// | Storage     | `make(spec)` for a design held by value; a longer one is `make(spec, table, |
/// |             | workspace)`, its taps written into the caller's table, which it then views, |
/// |             | through `workspace_size` doubles of the caller's                            |
/// | Table       | `from_table` checks in S: every tap finite, and for linear phase symmetric  |
/// |             | or antisymmetric exactly; `fir_any_design::from_table(taps, latency)` takes a |
/// |             | latency finite and not negative. A failing table does not initialise a     |
/// |             | constant, and one met at run time reaches `check::table_rejected`           |
/// | Cost        | `make()` runs in the caller's frame, outside the audio path. Cortex-M33, worst |
/// |             | call (Bench run 45866666441): 63 float taps 0.80 M instructions and 2,084 B of |
/// |             | stack; 255 Q15 taps 3.29 M, 3,708 B; 1,023 float taps into a table 12.7 M, 1,076 B |
/// | Formats     | float, double, q15, q31, Q4.27                                              |
/// | Rationale   | D24, D29, D36, D37, D47, D54, PLAN.md 3.3                                   |
///
/// @req{CAT-FIR-DESIGN-001} Each response is its windowed ideal: at the battery's designs the
/// low-pass is unity at DC and one half at its cutoff, the high-pass and band-stop their
/// complements, the band-pass zero at DC and unity between its edges, each within its stated
/// tolerance, and the stopbands below the attenuation past the estimate's transition.
/// @req{CAT-FIR-DESIGN-002} `make()` rejects what its card names with the status it names, and
/// a design's taps are symmetric bit for bit in every format; in a fixed format they are the
/// row-sum-preserving quantization of the double design, summing to its scaled sum, rounded,
/// within an LSB (even counts) or exactly (odd).
/// @req{CAT-FIR-DESIGN-003} A design fitting 512 bytes is held by value; a longer one views the
/// caller's table, which `make(spec, table, workspace)` writes and a temporary cannot supply.
/// @req{CAT-FIR-DESIGN-004} `from_table` accepts a table its card admits bit for bit and refuses
/// the rest: in a constant it does not compile (`compile_fail/fir_table_asymmetric.cpp`), at
/// run time `check::table_rejected`.
/// @req{CAT-FIR-DESIGN-005} `fir_taps_estimate` is the carried formula, rounded up, at least 4.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIR_DESIGN_H
#define CATENAE_FIR_DESIGN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"
#include "catenae/sample_traits.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Whether an FIR's delay is a constant of its type.
        enum class fir_phase : std::uint8_t {
            linear, ///< symmetric or antisymmetric taps: exactly (Taps - 1) / 2 frames
            any,    ///< any taps: the latency the design states
        };

        /// A low-pass: unity below `cutoff`, cycles per sample, its half-amplitude point.
        struct fir_lowpass {
            double cutoff;         ///< the edge, in (0, 1/2)
            double attenuation_db; ///< the stopband's depth, which sets the window's beta
        };
        /// A high-pass: unity above `cutoff`.
        struct fir_highpass {
            double cutoff;         ///< the edge, in (0, 1/2)
            double attenuation_db; ///< the stopband's depth
        };
        /// A band-pass: unity from `low` to `high`.
        struct fir_bandpass {
            double low;            ///< the lower edge
            double high;           ///< the upper edge, above `low` and below 1/2
            double attenuation_db; ///< the stopbands' depth
        };
        /// A band-stop: zero from `low` to `high`, unity outside.
        struct fir_bandstop {
            double low;            ///< the lower edge
            double high;           ///< the upper edge, above `low` and below 1/2
            double attenuation_db; ///< the stopband's depth
        };

        /// The taps a Kaiser-windowed design needs for `attenuation_db` over a transition of
        /// `transition` cycles per sample: (A - 8) / (2.285 2 pi df), rounded up, at least 4, at
        /// most 2^31. A high-pass or band-stop takes the next odd count.
        constexpr std::size_t fir_taps_estimate(double attenuation_db, double transition) noexcept {
            if (!(transition > 0.0)) {
                return 4;
            }
            const double n = (attenuation_db - 8.0) / (2.285 * 2.0 * 3.141592653589793 * transition);
            if (!(n > 4.0)) {
                return 4; // and a NaN
            }
            constexpr double most = 2147483648.0;
            if (n >= most) {
                return static_cast<std::size_t>(most);
            }
            const auto whole = static_cast<std::size_t>(n);
            return static_cast<double>(whole) < n ? whole + 1 : whole;
        }

        namespace detail {
            /// A tap: sample_traits' coefficient type for the format's storage.
            template <typename S>
            using fir_coefficient_t = typename sample_traits<storage_t<S>>::coeff;

            /// The four responses, as the compiled designer sees them.
            enum class fir_response : std::uint8_t { lowpass, highpass, bandpass, bandstop };

            /// A response and its edges: a low-pass or high-pass has one, as `low` and `high`.
            struct fir_request {
                fir_response response;
                double       low;
                double       high;
                double       attenuation_db;
            };

            /// What make() takes: the four specs, and whether each needs an odd count.
            template <typename Spec>
            struct fir_spec {
                static constexpr bool k_is_spec   = false;
                static constexpr bool k_needs_odd = false;
            };
            template <>
            struct fir_spec<fir_lowpass> {
                static constexpr bool        k_is_spec   = true;
                static constexpr bool        k_needs_odd = false;
                static constexpr fir_request request(const fir_lowpass& s) noexcept {
                    return {fir_response::lowpass, s.cutoff, s.cutoff, s.attenuation_db};
                }
            };
            template <>
            struct fir_spec<fir_highpass> {
                static constexpr bool        k_is_spec   = true;
                static constexpr bool        k_needs_odd = true;
                static constexpr fir_request request(const fir_highpass& s) noexcept {
                    return {fir_response::highpass, s.cutoff, s.cutoff, s.attenuation_db};
                }
            };
            template <>
            struct fir_spec<fir_bandpass> {
                static constexpr bool        k_is_spec   = true;
                static constexpr bool        k_needs_odd = false;
                static constexpr fir_request request(const fir_bandpass& s) noexcept {
                    return {fir_response::bandpass, s.low, s.high, s.attenuation_db};
                }
            };
            template <>
            struct fir_spec<fir_bandstop> {
                static constexpr bool        k_is_spec   = true;
                static constexpr bool        k_needs_odd = true;
                static constexpr fir_request request(const fir_bandstop& s) noexcept {
                    return {fir_response::bandstop, s.low, s.high, s.attenuation_db};
                }
            };

            // The compiled designer (src/fir_design.cpp): design-time arithmetic in double.

            /// The request if a design can be made from it; the card's statuses otherwise.
            expected<fir_request, status> fir_checked(const fir_request& request) noexcept;
            /// The first (taps + 1) / 2 taps of the windowed design, in double, into `half`
            /// (h[i] for i up to the centre; the rest mirror them). Precondition: a request
            /// fir_checked accepted, taps from 1, half.size() == (taps + 1) / 2.
            void fir_half_design(const fir_request& request, std::size_t taps, span<double> half) noexcept;

            /// DspTap's correction: `residual` steps of one, each to the tap of the largest
            /// remainder that can still move that way (the lowest index on a tie), none past a
            /// rail; the net of the steps it took.
            template <typename C>
            std::int64_t fir_spread(span<double> remainder, C* q, std::int64_t residual) noexcept {
                const std::size_t n     = remainder.size();
                std::int64_t      taken = 0;
                while (residual != 0) {
                    const double sgn  = residual > 0 ? 1.0 : -1.0;
                    const C      rail = residual > 0 ? std::numeric_limits<C>::max() : std::numeric_limits<C>::min();
                    std::size_t  best = n;
                    for (std::size_t u = 0; u < n; ++u) {
                        if (q[u] != rail && (best == n || sgn * remainder[u] > sgn * remainder[best])) {
                            best = u;
                        }
                    }
                    if (best == n) {
                        break; // every tap at this rail: DspTap's exception
                    }
                    const std::int64_t step = residual > 0 ? 1 : -1;
                    q[best]                 = narrow<C>(q[best] + step);
                    remainder[best] -= sgn;
                    residual -= step;
                    taken += step;
                }
                return taken;
            }

            /// The taps of a symmetric design from its double half, into `out` (`taps` of them):
            /// each rounded once in a floating format; in a fixed one the first taps / 2 by
            /// DspTap's row-sum-preserving quantization, the centre of an odd count what makes
            /// the row sum the scaled design's sum, rounded, then mirrored. `half` is overwritten.
            template <typename T>
            void fir_quantize(span<double> half, typename sample_traits<T>::coeff* out, std::size_t taps) noexcept {
                using traits         = sample_traits<T>;
                using coeff          = typename traits::coeff;
                const std::size_t np = taps / 2; // the pairs
                if constexpr (!traits::is_fixed_point) {
                    for (std::size_t i = 0; i < half.size(); ++i) {
                        out[i] = traits::make_coeff(half[i]);
                    }
                }
                else {
                    double       exact_sum = 0.0;
                    std::int64_t quant_sum = 0;
                    for (std::size_t u = 0; u < np; ++u) {
                        const double scaled = half[u] * traits::coeff_scale;
                        const coeff  q      = traits::make_coeff(half[u]);
                        out[u]              = q;
                        half[u]             = scaled - static_cast<double>(q); // now its remainder
                        exact_sum += scaled;
                        quant_sum += q;
                    }
                    quant_sum += fir_spread<coeff>(half.first(np), out, round_sat<std::int64_t>(exact_sum) - quant_sum);
                    if (taps % 2 == 1) {
                        const double whole = 2.0 * exact_sum + half[np] * traits::coeff_scale;
                        out[np]            = clamp_sat<coeff>(round_sat<std::int64_t>(whole) - 2 * quant_sum);
                    }
                }
                for (std::size_t u = 0; u < np; ++u) {
                    out[taps - 1 - u] = out[u];
                }
            }

            /// Whether a floating tap is finite (false for a NaN); a fixed one always is.
            template <typename C>
            constexpr bool fir_finite(C c) noexcept {
                if constexpr (std::is_floating_point<C>::value) {
                    return c <= std::numeric_limits<C>::max() && c >= -std::numeric_limits<C>::max();
                }
                else {
                    return true;
                }
            }

            /// Whether two taps are one value (-0 and +0 alike), compared in their own type.
            template <typename C>
            constexpr bool fir_same(C a, C b) noexcept {
                if constexpr (std::is_floating_point<C>::value) {
                    return !(a < b) && !(a > b);
                }
                else {
                    return static_cast<std::int64_t>(a) == static_cast<std::int64_t>(b);
                }
            }

            /// Whether every tap of `h` is finite and, for linear phase, h is symmetric or
            /// antisymmetric, exactly.
            template <typename C, std::size_t N>
            constexpr bool fir_table_ok(const C* h, bool linear) noexcept {
                bool finite        = true;
                bool symmetric     = true;
                bool antisymmetric = true;
                for (std::size_t k = 0; k < N; ++k) {
                    const C a = h[k];
                    const C b = h[N - 1 - k];
                    finite    = finite && fir_finite(a);
                    symmetric = symmetric && fir_same(a, b);
                    if constexpr (std::is_floating_point<C>::value) {
                        antisymmetric = antisymmetric && fir_same(a, -b);
                    }
                    else {
                        antisymmetric = antisymmetric && static_cast<std::int64_t>(a) == -static_cast<std::int64_t>(b);
                    }
                }
                return finite && (!linear || symmetric || antisymmetric);
            }

            /// What a design stores beside its taps: nothing for linear phase, whose latency is
            /// its type's.
            template <fir_phase Phase>
            struct fir_stored_latency {
                explicit constexpr fir_stored_latency(stage_latency) noexcept {}
            };
            template <>
            struct fir_stored_latency<fir_phase::any> {
                explicit constexpr fir_stored_latency(stage_latency latency) noexcept
                    : stored(latency) {}
                stage_latency stored; ///< as the design states it
            };

            /// Whether a design of these sizes is held by value: its latency, if it stores one,
            /// and its taps, within the 512 bytes a stage may be (D54).
            template <typename S, std::size_t Taps, fir_phase Phase>
            struct fir_by_value {
                struct probe : fir_stored_latency<Phase> {
                    std::array<fir_coefficient_t<S>, Taps> taps;
                };
                static constexpr bool k_value = sizeof(probe) <= max_configuration_bytes;
            };
        } // namespace detail

        /// The longest direct-form FIR: longer ones are the FFT FIR's (C4).
        inline constexpr std::size_t max_fir_taps = 65536;

        /// @brief An FIR's impulse response in S's coefficient type, by value or viewed. A class,
        /// not an aggregate: only `make()` and `from_table()` produce one.
        template <typename S, std::size_t Taps, fir_phase Phase = fir_phase::linear>
        class fir_design : private detail::fir_stored_latency<Phase> {
            static_assert(Taps >= 1 && Taps <= max_fir_taps, "catenae: a direct-form FIR has 1 to 65,536 taps");
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value || std::is_same<S, q15>::value
                              || std::is_same<S, q31>::value || std::is_same<S, q4_27>::value,
                          "catenae: an FIR design is float, double, q15, q31 or q4_27");
            using stored_latency = detail::fir_stored_latency<Phase>;

          public:
            using coefficient_type                 = detail::fir_coefficient_t<S>;       ///< one tap
            using table_type                       = std::array<coefficient_type, Taps>; ///< h[0] to h[Taps - 1]
            static constexpr std::size_t tap_count = Taps;                               ///< how many
            static constexpr fir_phase   phase     = Phase;                              ///< linear or any
            /// True when the taps are held by value, false when viewed (CAT-FIR-DESIGN-003).
            static constexpr bool holds_by_value = detail::fir_by_value<S, Taps, Phase>::k_value;
            /// What the design holds: the taps, or a view of the caller's.
            using table_holder = std::conditional_t<holds_by_value, table_type, table_view<coefficient_type, Taps>>;
            /// The doubles of the caller's a longer design's make() works in.
            static constexpr std::size_t workspace_size = (Taps + 1) / 2;

            /// A design held by value, from one of the four specs (the card's statuses). Outside
            /// the audio path.
            template <typename Spec>
            static expected<fir_design, status> make(const Spec& spec) noexcept {
                static_assert(holds_by_value, "catenae: a design this long is a table: make(spec, table, workspace)");
                const expected<detail::fir_request, status> request = checked(spec);
                if (!request) {
                    return unexpected(request.error());
                }
                std::array<double, workspace_size> half{};
                table_type                         taps{};
                detail::fir_half_design(*request, Taps, span<double>(half));
                detail::fir_quantize<storage_t<S>>(span<double>(half), taps.data(), Taps);
                return fir_design(taps, made_latency());
            }

            /// A longer design, its taps written into `table`, which it views, through
            /// `workspace`. Outside the audio path.
            template <typename Spec>
            static expected<fir_design, status> make(const Spec& spec, span<coefficient_type, Taps> table,
                                                     span<double, workspace_size> workspace) noexcept {
                static_assert(!holds_by_value, "catenae: a design this short is held by value: make(spec)");
                const expected<detail::fir_request, status> request = checked(spec);
                if (!request) {
                    return unexpected(request.error());
                }
                detail::fir_half_design(*request, Taps, span<double>(workspace));
                detail::fir_quantize<storage_t<S>>(span<double>(workspace), table.data(), Taps);
                return fir_design(table_view<coefficient_type, Taps>(table), made_latency());
            }

            /// A linear-phase design from its taps, checked: finite, symmetric or antisymmetric
            /// exactly. A table that fails does not initialise a constant; at run time it reaches
            /// the handler.
            static constexpr fir_design from_table(const table_holder& taps) noexcept {
                static_assert(Phase == fir_phase::linear,
                              "catenae: an fir_any_design's table states its latency: from_table(taps, latency)");
                detail::check_always(detail::fir_table_ok<coefficient_type, Taps>(taps.data(), true),
                                     check::table_rejected);
                return fir_design(taps, made_latency());
            }

            /// Any FIR from its taps and the latency its generator measured, checked: finite
            /// taps, a latency finite and not negative.
            static constexpr fir_design from_table(const table_holder& taps, stage_latency latency) noexcept {
                static_assert(Phase == fir_phase::any,
                              "catenae: a linear-phase design's latency is its type's: from_table(taps)");
                detail::check_always(detail::fir_table_ok<coefficient_type, Taps>(taps.data(), false)
                                         && latency.nominal >= 0.0f && latency.deviation >= 0.0f
                                         && latency.nominal <= std::numeric_limits<float>::max()
                                         && latency.deviation <= std::numeric_limits<float>::max(),
                                     check::table_rejected);
                return fir_design(taps, latency);
            }

            /// h[0] to h[Taps - 1].
            constexpr span<const coefficient_type, Taps> table() const noexcept {
                return span<const coefficient_type, Taps>(m_taps.data(), Taps);
            }
            /// The first tap, for a kernel.
            constexpr const coefficient_type* data() const noexcept { return m_taps.data(); }
            /// `{nominal, deviation}`, in frames: (Taps - 1) / 2 and 0 for linear phase.
            constexpr stage_latency latency() const noexcept {
                if constexpr (Phase == fir_phase::linear) {
                    return made_latency();
                }
                else {
                    return this->stored;
                }
            }

          private:
            constexpr fir_design(const table_holder& taps, stage_latency latency) noexcept
                : stored_latency(latency)
                , m_taps(taps) {}

            /// A linear-phase design's latency.
            static constexpr stage_latency made_latency() noexcept {
                return {static_cast<float>(Taps - 1) * 0.5f, 0.0f};
            }

            /// The spec as the designer's request, checked (the card's statuses).
            template <typename Spec>
            static expected<detail::fir_request, status> checked(const Spec& spec) noexcept {
                static_assert(detail::fir_spec<Spec>::k_is_spec,
                              "catenae: an FIR's make() takes fir_lowpass, fir_highpass, fir_bandpass or fir_bandstop");
                static_assert(!detail::fir_spec<Spec>::k_needs_odd || Taps % 2 == 1,
                              "catenae: a high-pass or band-stop passes Nyquist, where an even tap count has a zero: "
                              "an odd Taps");
                return detail::fir_checked(detail::fir_spec<Spec>::request(spec));
            }

            table_holder m_taps;
        };

        /// Any FIR: its latency the design's, not its type's.
        template <typename S, std::size_t Taps>
        using fir_any_design = fir_design<S, Taps, fir_phase::any>;

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FIR_DESIGN_H
