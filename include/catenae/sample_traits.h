/// @file sample_traits.h
/// @brief How each storage type stores coefficients, accumulates and rounds (FIR datapaths).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Types     | `double`, `float`, `std::int16_t` (Q15 samples), `std::int32_t` (Q31 samples)    |
/// | double    | the golden model: coefficients and accumulator in `double`, nothing rounds       |
/// | float     | coefficients and accumulator in `float` (D36): it never leaves a single-precision FPU |
/// | Q15       | Q1.14 coefficients, exact `int64` accumulation (Q29), one rounding to Q15         |
/// | Q31       | Q1.30 coefficients, products pre-shifted 16 bits (Q45), one rounding to Q31       |
/// | Rounding  | round half up at the one rounding point, then saturation                         |
/// | Method    | carried from DspTap's sample_traits.h (NOTICE.md); the float accumulator is new  |
/// | Rationale | D29, D36, D47                                                                    |
///
/// Raw sample types and traits, never wrapper classes (D29): the ladder (headroom bits, the
/// accumulator's pre-shift, where the one rounding happens) is a design decision a test pins.
/// A fixed<> tag is stripped to its storage before a kernel is called (catenae/sample_format.h),
/// so Q4.27 and Q0.31 share one copy of this arithmetic.
///
/// @req{CAT-TRAITS-001} Coefficients: `make_coeff(1.0)` is 16384 (Q1.14) for Q15 and 2^30 (Q1.30)
/// for Q31, rounded half away from zero and saturated; the floating formats convert.
/// @req{CAT-TRAITS-002} The Q15 ladder is Q0.15 x Q1.14 = Q29 with no pre-shift; the Q31 ladder
/// is Q0.31 x Q1.30 = Q61, pre-shifted 16 bits to Q45. Both round once, 14 bits, half up, and
/// saturate.
/// @req{CAT-TRAITS-003} The float profile accumulates in float and double in double; neither
/// rounds or saturates.
/// @req{CAT-TRAITS-004} `finalize_divided<S, D>` is finalize of the accumulator divided by D
/// under the one rounding, exact at every multiple of D times the rounding step.
/// @req{CAT-TRAITS-005} `silence()` is the format's zero.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_SAMPLE_TRAITS_H
#define CATENAE_SAMPLE_TRAITS_H

#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/shift.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {

            /// A double rounded half away from zero and saturated to a signed integer type.
            template <typename I>
            constexpr I round_sat(double v) noexcept {
                constexpr double lo = static_cast<double>(std::numeric_limits<I>::min());
                constexpr double hi = static_cast<double>(std::numeric_limits<I>::max());
                const double     r  = v < 0.0 ? v - 0.5 : v + 0.5;
                if (r <= lo) {
                    return std::numeric_limits<I>::min();
                }
                if (r >= hi) {
                    return std::numeric_limits<I>::max();
                }
                return static_cast<I>(r);
            }

            /// A 64-bit accumulator saturated to a narrower signed integer type.
            template <typename I>
            constexpr I clamp_sat(std::int64_t v) noexcept {
                constexpr auto lo = static_cast<std::int64_t>(std::numeric_limits<I>::min());
                constexpr auto hi = static_cast<std::int64_t>(std::numeric_limits<I>::max());
                return narrow<I>(v < lo ? lo : (v > hi ? hi : v));
            }

        } // namespace detail

        /// The traits of one storage type; specialised for the four below and nothing else.
        template <typename T>
        struct sample_traits;

        /// The golden model: double samples, coefficients and accumulator; nothing rounds.
        template <>
        struct sample_traits<double> {
            using coeff = double; ///< a stored coefficient
            using accum = double; ///< a dot product's accumulator

            static constexpr double coeff_scale    = 1.0;   ///< coefficient units per 1.0
            static constexpr bool   is_fixed_point = false; ///< whether the storage is an integer

            /// A designed coefficient in storage form.
            static constexpr coeff make_coeff(double c) noexcept { return c; }
            /// acc + x c, in the accumulator.
            static constexpr accum mac(accum acc, double x, coeff c) noexcept { return acc + x * c; }
            /// The accumulator as a sample.
            static constexpr double finalize(accum acc) noexcept { return acc; }
            /// The sample of silence.
            static constexpr double silence() noexcept { return 0.0; }
        };

        /// The default profile: float samples, coefficients and accumulator (D36).
        template <>
        struct sample_traits<float> {
            using coeff = float; ///< a stored coefficient
            using accum = float; ///< a dot product's accumulator: float, never double (D36)

            static constexpr double coeff_scale    = 1.0;   ///< coefficient units per 1.0
            static constexpr bool   is_fixed_point = false; ///< whether the storage is an integer

            /// A designed coefficient in storage form (design time: the designer is double).
            static constexpr coeff make_coeff(double c) noexcept { return static_cast<coeff>(c); }
            /// acc + x c, in float.
            static constexpr accum mac(accum acc, float x, coeff c) noexcept { return acc + x * c; }
            /// The accumulator as a sample.
            static constexpr float finalize(accum acc) noexcept { return acc; }
            /// The sample of silence.
            static constexpr float silence() noexcept { return 0.0f; }
        };

        /// Q15: Q0.15 samples, Q1.14 coefficients (a bit of headroom for a peak tap near 1.0),
        /// products summed exactly in int64 as Q29, rounded once to Q15 and saturated.
        template <>
        struct sample_traits<std::int16_t> {
            using coeff = std::int16_t; ///< a stored Q1.14 coefficient
            using accum = std::int64_t; ///< the Q29 accumulator

            static constexpr int sample_frac_bits = 15; ///< Q0.15 samples
            static constexpr int coeff_frac_bits  = 14; ///< Q1.14 coefficients
            static constexpr int accum_pre_shift  = 0;  ///< products accumulate exactly
            /// Q0.15 x Q1.14 = Q29, accumulated as it is.
            static constexpr int accum_frac_bits = sample_frac_bits + coeff_frac_bits - accum_pre_shift;
            /// Q29 to Q15: the one rounding, 14 bits.
            static constexpr int finalize_shift = accum_frac_bits - sample_frac_bits;
            static_assert(accum_frac_bits == 29 && finalize_shift == 14,
                          "catenae: the Q15 ladder is Q0.15 x Q1.14 = Q29, to Q15 by a 14-bit shift");

            /// Q1.14 units per 1.0.
            static constexpr double coeff_scale =
                static_cast<double>(detail::shift_left(std::int64_t{1}, coeff_frac_bits));
            static constexpr bool is_fixed_point = true; ///< whether the storage is an integer

            /// A designed coefficient in Q1.14, rounded and saturated.
            static constexpr coeff make_coeff(double c) noexcept { return detail::round_sat<coeff>(c * coeff_scale); }
            /// acc + x c: an exact Q29 product.
            static constexpr accum mac(accum acc, std::int16_t x, coeff c) noexcept {
                return acc + static_cast<std::int64_t>(static_cast<std::int32_t>(x) * static_cast<std::int32_t>(c));
            }
            /// Q29 to Q15, round half up, saturated.
            static constexpr std::int16_t finalize(accum acc) noexcept {
                return detail::clamp_sat<std::int16_t>(
                    detail::shift_right(acc + detail::shift_left(std::int64_t{1}, finalize_shift - 1), finalize_shift));
            }
            /// The sample of silence.
            static constexpr std::int16_t silence() noexcept { return 0; }
        };

        /// Q31: Q0.31 samples, Q1.30 coefficients; each Q61 product drops 16 bits to Q45 before
        /// it is summed (headroom for a polyphase decimator's rows), rounded once to Q31.
        template <>
        struct sample_traits<std::int32_t> {
            using coeff = std::int32_t; ///< a stored Q1.30 coefficient
            using accum = std::int64_t; ///< the Q45 accumulator

            static constexpr int sample_frac_bits = 31; ///< Q0.31 samples
            static constexpr int coeff_frac_bits  = 30; ///< Q1.30 coefficients
            static constexpr int accum_pre_shift  = 16; ///< each Q61 product drops 16 bits
            /// Q0.31 x Q1.30 = Q61, pre-shifted to Q45.
            static constexpr int accum_frac_bits = sample_frac_bits + coeff_frac_bits - accum_pre_shift;
            /// Q45 to Q31: the one rounding, 14 bits, as Q15's.
            static constexpr int finalize_shift = accum_frac_bits - sample_frac_bits;
            static_assert(accum_frac_bits == 45 && finalize_shift == 14,
                          "catenae: the Q31 ladder is Q0.31 x Q1.30 = Q61 >> 16 = Q45, to Q31 by a 14-bit shift");

            /// Q1.30 units per 1.0.
            static constexpr double coeff_scale =
                static_cast<double>(detail::shift_left(std::int64_t{1}, coeff_frac_bits));
            static constexpr bool is_fixed_point = true; ///< whether the storage is an integer

            /// A designed coefficient in Q1.30, rounded and saturated.
            static constexpr coeff make_coeff(double c) noexcept { return detail::round_sat<coeff>(c * coeff_scale); }
            /// acc + (x c >> 16): the Q61 product in Q45.
            static constexpr accum mac(accum acc, std::int32_t x, coeff c) noexcept {
                return acc + detail::shift_right(static_cast<std::int64_t>(x) * c, accum_pre_shift);
            }
            /// Q45 to Q31, round half up, saturated.
            static constexpr std::int32_t finalize(accum acc) noexcept {
                return detail::clamp_sat<std::int32_t>(
                    detail::shift_right(acc + detail::shift_left(std::int64_t{1}, finalize_shift - 1), finalize_shift));
            }
            /// The sample of silence.
            static constexpr std::int32_t silence() noexcept { return 0; }
        };

        /// Whether a type has a complete sample_traits: the four above.
        template <typename T, typename = void>
        struct is_kernel_sample : std::false_type {};
        template <typename T>
        struct is_kernel_sample<T, std::void_t<typename sample_traits<T>::coeff, typename sample_traits<T>::accum,
                                               decltype(sample_traits<T>::make_coeff(0.0)),
                                               decltype(sample_traits<T>::mac(typename sample_traits<T>::accum{}, T{},
                                                                              typename sample_traits<T>::coeff{})),
                                               decltype(sample_traits<T>::finalize(typename sample_traits<T>::accum{})),
                                               decltype(sample_traits<T>::silence())>>
            : std::bool_constant<
                  std::is_same<decltype(sample_traits<T>::finalize(typename sample_traits<T>::accum{})), T>::value
                  && std::is_same<decltype(sample_traits<T>::silence()), T>::value> {};

        /// finalize() of acc / Divisor under the one rounding: for a dot whose coefficients
        /// carry a gain of Divisor the format cannot hold (a polyphase decimator's rows).
        /// Floating: finalize(acc / Divisor). Fixed, a power of two 2^k: finalize with its shift
        /// widened by k. Q31, any other divisor: the exact round-half-up quotient. Q15, any
        /// other divisor: a multiply-back by round(2^26 / D), exact at every multiple of
        /// D 2^finalize_shift and within D 2^-11 of a rounding boundary elsewhere.
        template <typename S, std::uint32_t Divisor>
        constexpr S finalize_divided(typename sample_traits<S>::accum acc) noexcept {
            static_assert(is_kernel_sample<S>::value, "catenae: finalize_divided is for a sample_traits type");
            static_assert(Divisor >= 1, "catenae: finalize_divided's divisor is a positive integer");
            using tr = sample_traits<S>;
            if constexpr (!tr::is_fixed_point) {
                return tr::finalize(acc / static_cast<typename tr::accum>(Divisor));
            }
            else {
                constexpr int f = tr::finalize_shift;
                if constexpr ((Divisor & (Divisor - 1)) == 0) {
                    constexpr int k = [] {
                        int n = 0;
                        while ((std::uint32_t{1} << n) < Divisor) {
                            ++n;
                        }
                        return n;
                    }();
                    return detail::clamp_sat<S>(
                        detail::shift_right(acc + detail::shift_left(std::int64_t{1}, f + k - 1), f + k));
                }
                else if constexpr (std::is_same<S, std::int16_t>::value) {
                    constexpr int          s = 26;
                    constexpr std::int64_t c = (detail::shift_left(std::int64_t{1}, s) + Divisor / 2) / Divisor;
                    return detail::clamp_sat<S>(
                        detail::shift_right(acc * c + detail::shift_left(std::int64_t{1}, f + s - 1), f + s));
                }
                else {
                    constexpr std::int64_t den = detail::shift_left(static_cast<std::int64_t>(Divisor), f);
                    const std::int64_t     num = acc + den / 2;
                    std::int64_t           q   = num / den; // truncates toward zero
                    if (num % den != 0 && num < 0) {
                        --q; // floor
                    }
                    return detail::clamp_sat<S>(q);
                }
            }
        }

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SAMPLE_TRAITS_H
