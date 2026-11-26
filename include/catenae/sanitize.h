/// @file sanitize.h
/// @brief `sanitize`: every sample a later stage can trust, for a caller who cannot vouch for
/// its input (PLAN.md 3.4: content is not a precondition).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out, S float or double                                  |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous)                             |
/// | Configured | by nothing: the format and the channel count are its type                    |
/// | Replaces   | NaN, +infinity, -infinity and every subnormal with +0                        |
/// | Keeps      | every finite normal value and both zeros, bit for bit                        |
/// | Method     | the sample's exponent field, read through its bits (DEV-006): the same result |
/// |            | whatever the host's flush-to-zero and denormals-are-zero settings            |
/// | State      | none (`rests_at_zero`)                                                       |
/// | Workspace  | none                                                                         |
/// | Latency    | exactly 0                                                                    |
/// | Tail       | 0 frames                                                                     |
/// | Formats    | float and double: a fixed-point value is always a valid sample               |
/// | Rationale  | D12, PLAN.md 3.4                                                             |
///
/// @req{CAT-SANITIZE-001} NaN (quiet or signalling, any payload, either sign), both infinities
/// and every subnormal become +0; every finite normal value and both zeros pass unchanged, bit
/// for bit, in float and in double.
/// @req{CAT-SANITIZE-002} The result does not depend on the host's floating-point settings,
/// and `sanitize` twice is `sanitize` once; it takes only float and double.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_SANITIZE_H
#define CATENAE_SANITIZE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// The bits of an IEEE 754 binary32 or binary64 value, and its exponent field and
            /// magnitude within them.
            template <typename S>
            struct ieee_fields;
            template <>
            struct ieee_fields<float> {
                using bits                        = std::uint32_t;
                static constexpr bits k_exponent  = 0x7F800000u;
                static constexpr bits k_magnitude = 0x7FFFFFFFu;
            };
            template <>
            struct ieee_fields<double> {
                using bits                        = std::uint64_t;
                static constexpr bits k_exponent  = 0x7FF0000000000000u;
                static constexpr bits k_magnitude = 0x7FFFFFFFFFFFFFFFu;
            };
            static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559
                              && std::numeric_limits<double>::is_iec559,
                          "catenae: float and double are IEEE 754 binary32 and binary64");
        } // namespace detail

        /// @brief Every NaN, infinity and subnormal of a `port<S, Ch>` replaced with zero.
        template <typename S, std::size_t Ch>
        class basic_sanitize {
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value,
                          "catenae: sanitize is for float and double: a fixed-point value is always valid");

          public:
            using input_port  = port<S, Ch>; ///< what it reads
            using output_port = port<S, Ch>; ///< what it writes

            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< it remembers nothing

            /// Nothing.
            struct state {};
            using workspace = no_workspace; ///< nothing

            /// Every sample, made one a later stage can trust.
            std::size_t run(state&, workspace&, span<const S> in, span<S> out) const noexcept {
                for (std::size_t i = 0; i < in.size(); ++i) {
                    out[i] = value(in[i]);
                }
                return in.size() / Ch;
            }

            /// One sample: +0 for a NaN, an infinity or a subnormal; itself otherwise.
            static S value(S x) noexcept {
                bits_t bits;
                std::memcpy(&bits, &x, sizeof bits); // a bit cast (DEV-006)
                const bits_t exponent = bits & k_exponent;
                const bool   special  = exponent == k_exponent;                     // NaN or infinity
                const bool   tiny     = exponent == 0 && (bits & k_magnitude) != 0; // subnormal
                return special || tiny ? S{0} : x;
            }

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

          private:
            using bits_t                        = typename detail::ieee_fields<S>::bits;
            static constexpr bits_t k_exponent  = detail::ieee_fields<S>::k_exponent;
            static constexpr bits_t k_magnitude = detail::ieee_fields<S>::k_magnitude;
        };

        template <std::size_t Ch>
        using sanitize = basic_sanitize<float, Ch>; ///< in float (D24)
        template <std::size_t Ch>
        using sanitize_f64 = basic_sanitize<double, Ch>; ///< in double

        template <typename S, std::size_t Ch>
        struct is_stage<basic_sanitize<S, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SANITIZE_H
