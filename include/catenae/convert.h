/// @file convert.h
/// @brief `convert`: one sample format to another, with its numeric contract (D29, PLAN.md 12).
///
/// | Contract   |                                                                            |
/// |------------|----------------------------------------------------------------------------|
/// | Ports      | `port<From, Ch>` in, `port<To, Ch>` out                                    |
/// | Ratio      | 1:1, input group 1 (frame-synchronous); not in place, the two ports differ |
/// | Configured | by nothing: the formats are its type                                      |
/// | State      | none (`rests_at_zero`)                                                     |
/// | Workspace  | none                                                                       |
/// | Latency    | exactly 0                                                                  |
/// | Tail       | 0 frames                                                                   |
/// | Formats    | among float, q15, q31 and Q4.27, in either direction                       |
/// | Scale      | x in [-1, 1) is x x 2^15 in q15, x 2^31 in q31; Q4.27 is x 2^27 over [-16, 16) |
/// | Rounding   | once: into a fixed format half up (add half an LSB, floor); into float as the |
/// |            | target's integer conversion does, to nearest, ties to even                 |
/// | Saturation | at the format's limits: +1.0 and above to the maximum of q15 and q31, -1.0 |
/// |            | and below to the minimum; Q4.27 at +16 and -16; NaN to 0                  |
/// | Method     | `std::transform` with one converter per pair of formats                    |
/// | Rationale  | D29, D47, PLAN.md section 12 (written new)                                 |
///
/// @req{CAT-CONVERT-002} float to a fixed format is round half up of x x 2^F, saturated at
/// the format's limits, every value of the range exactly: k + 1/2 LSB gives k + 1, k - 1/2
/// gives k, and q15's or q31's maximum at x of 1.0 and above.
/// @req{CAT-CONVERT-003} NaN converts to 0, +infinity to the maximum and -infinity to the
/// minimum, so no input is undefined.
/// @req{CAT-CONVERT-004} A fixed format to float is its value times 2^-F, rounded once by the
/// integer conversion: exact for q15 (and so a q15 round trip through float is the identity),
/// to nearest even for q31 and Q4.27 (q31's maximum gives 1.0).
/// @req{CAT-CONVERT-005} Between fixed formats a wider fraction is an exact shift (q15 to q31
/// is x x 2^16), a narrower one rounds half up, and both saturate: Q4.27 to q31 at +1.0.
/// @req{CAT-CONVERT-006} Only float, q15, q31 and Q4.27 convert, and never a format to itself.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_CONVERT_H
#define CATENAE_CONVERT_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/shift.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/sample_traits.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {

            template <typename S>
            inline constexpr bool k_converts = std::is_same<S, float>::value || std::is_same<S, q15>::value
                                               || std::is_same<S, q31>::value || std::is_same<S, q4_27>::value;

            /// floor(v + 1/2) for a float below 2^31 in magnitude, exactly: v + 0.5f can round,
            /// so the fraction is taken apart first (v less its truncation is exact).
            inline std::int64_t round_half_up(float v) noexcept {
                const auto  whole = static_cast<std::int64_t>(v); // truncates toward zero
                const float frac  = v - static_cast<float>(whole);
                if (frac >= 0.5f) {
                    return whole + 1;
                }
                if (frac < -0.5f) {
                    return whole - 1;
                }
                return whole;
            }

            /// One value of format From as format To.
            template <typename From, typename To>
            struct converter;

            /// float into a fixed format: scaled, rounded half up once, saturated; NaN to 0.
            template <typename St, int F>
            struct converter<float, fixed<St, F>> {
                St operator()(float x) const noexcept {
                    constexpr float scale = static_cast<float>(shift_left(std::int64_t{1}, F));
                    // The format's limits, as floats: exact powers of two.
                    constexpr float top =
                        static_cast<float>(shift_left(std::int64_t{1}, std::numeric_limits<St>::digits));
                    const float v = x * scale;
                    if (v >= top) {
                        return std::numeric_limits<St>::max();
                    }
                    if (v <= -top) {
                        return std::numeric_limits<St>::min();
                    }
                    if (!(v > -top)) {
                        return St{0}; // NaN fails every comparison
                    }
                    return clamp_sat<St>(round_half_up(v));
                }
            };

            /// A fixed format into float: one rounding, by the integer conversion.
            template <typename St, int F>
            struct converter<fixed<St, F>, float> {
                float operator()(St x) const noexcept {
                    constexpr float unit = 1.0f / static_cast<float>(shift_left(std::int64_t{1}, F));
                    return static_cast<float>(x) * unit;
                }
            };

            /// Between fixed formats: an exact shift up, a half-up shift down, saturated.
            template <typename St1, int F1, typename St2, int F2>
            struct converter<fixed<St1, F1>, fixed<St2, F2>> {
                St2 operator()(St1 x) const noexcept {
                    const std::int64_t wide = x;
                    if constexpr (F2 >= F1) {
                        return clamp_sat<St2>(shift_left(wide, F2 - F1));
                    }
                    else {
                        constexpr int d = F1 - F2;
                        return clamp_sat<St2>(shift_right(wide + shift_left(std::int64_t{1}, d - 1), d));
                    }
                }
            };

        } // namespace detail

        /// @brief Every value of a `port<From, Ch>` in format To.
        template <typename From, typename To, std::size_t Ch>
        class convert {
            static_assert(detail::k_converts<From> && detail::k_converts<To>,
                          "catenae: convert is among float, q15, q31 and Q4.27 (D29)");
            static_assert(!std::is_same<From, To>::value, "catenae: convert is between two different formats");

          public:
            using input_port  = port<From, Ch>; ///< what it reads
            using output_port = port<To, Ch>;   ///< what it writes

            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = false;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< it remembers nothing

            /// Nothing.
            struct state {};
            using workspace = no_workspace; ///< nothing

            /// Each value converted.
            std::size_t run(state&, workspace&, typename input_port::input_view in,
                            typename output_port::output_view out) const noexcept {
                std::transform(in.begin(), in.end(), out.begin(), detail::converter<From, To>{});
                return in.size() / Ch;
            }

            /// One value converted, by the contract above.
            static storage_t<To> value(storage_t<From> x) noexcept { return detail::converter<From, To>{}(x); }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }
        };

        template <typename From, typename To, std::size_t Ch>
        struct is_stage<convert<From, To, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_CONVERT_H
