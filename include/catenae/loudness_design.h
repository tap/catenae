/// @file loudness_design.h
/// @brief K-weighting: ITU-R BS.1770-4's two-stage pre-filter at any rate, carried from the
/// standard's 48 kHz coefficients, as a `biquad_design` of two sections (C5).
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | A design    | `k_weighting_design<S>::make(rate)` returns a `biquad_design<S, 2>`: the     |
/// |             | spherical head's shelf, then the RLB high-pass, as `loudness_meter` runs them |
/// |             | and as a `biquad<Ch, 2>` stage can                                           |
/// | 48 kHz      | BS.1770-4 Tables 1 and 2, as published                                      |
/// | Any rate    | each table's section is the bilinear transform of an analog section          |
/// |             | (Vh s^2 + Vb s / Q + Vl) / (s^2 + s / Q + 1), s pre-warped at its f0. The     |
/// |             | transform is inverted exactly from the coefficients: K^2 = (1 + a1 + a2) /    |
/// |             | (1 - a1 + a2), K / Q = 2 (1 - a2) / (1 - a1 + a2), Vl = (b0 + b1 + b2) / (1 + |
/// |             | a1 + a2), Vh = (b0 - b1 + b2) / (1 - a1 + a2), Vb = (b0 - b2) / (1 - a2), f0 |
/// |             | = 48000 atan(K) / pi; then made again at the rate, pre-warped at the same f0 |
/// |             | (Oppenheim and Schafer, "Discrete-Time Signal Processing", 3rd ed., 7.2). The |
/// |             | shelf is f0 1681.974 Hz, Q 0.70718, Vh 1.58486, Vb 1.25872, Vl 1; the        |
/// |             | high-pass f0 38.13547 Hz, Q 0.50033, Vh 1.00499                              |
/// | Agreement   | at 48 kHz the tables themselves. Elsewhere, the largest departure from the   |
/// |             | 48 kHz response from 20 Hz to 20 kHz (to 0.45 of the rate where that is     |
/// |             | lower), in dB, near 2.7 kHz where the shelf turns: 0.287 at 8 kHz, 0.066 at |
/// |             | 16, 0.031 at 22.05, 0.0104 at 32, 0.0016 at 44.1, 0.0063 at 96, 0.0078 at   |
/// |             | 192, 0.0082 at 384 kHz. Float adds its coefficients' rounding, largest at   |
/// |             | 20 Hz: 0.0049 at 44.1, 0.028 at 96, 0.015 at 192 kHz. A test pins them      |
/// | Rejects     | `status::invalid_argument`: a rate that is NaN, infinite or not above 0;     |
/// |             | `status::out_of_range`: a rate at which the high-pass's f0 is below          |
/// |             | `min_frequency<S>` cycles per sample (above 381,354 Hz in float, 3,813,547   |
/// |             | in double) or the shelf's f0 within it of 1/2 (below 3,365 Hz in float,     |
/// |             | 3,364 in double), or whose coefficients in S are not stable                 |
/// | Numbers     | the biquad design's: latency the group delay at 997 Hz (BS.1770's reference |
/// |             | frequency: -1.31 frames at 48 kHz, where the shelf's phase leads) and its   |
/// |             | largest departure over the 1,025 points from 0 to 1/2 within 3 dB of the    |
/// |             | largest gain, the cookbook's rule; tails and residue as `biquad_design`     |
/// |             | computes them                                                               |
/// | Cost        | `make()` runs outside the audio path, in double: see the card's measured     |
/// |             | figures in bench/README.md ("The analyzers")                                 |
/// | Formats     | float and double; q31 holds a Q2.30 table (`biquad_design<q31, 2>`), and its |
/// |             | design arrives with the section scaling at C6 (D47)                          |
/// | Rationale   | D12, D24, D37, D47                                                          |
///
/// @req{CAT-K-WEIGHTING-001} At 48 kHz `make()` gives BS.1770-4's Tables 1 and 2 within
/// rounding; at other rates its response meets the 48 kHz response within the card's numbers,
/// and both sections' f0, Q and gains are the card's.
/// @req{CAT-K-WEIGHTING-002} `make()` rejects what its card names with the status it names; in
/// q31 it does not compile until the section scaling arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_LOUDNESS_DESIGN_H
#define CATENAE_LOUDNESS_DESIGN_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/biquad_design.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/biquad_kernel.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/recursive_tail.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            // The compiled designer (src/loudness_design.cpp): design-time arithmetic in double.

            /// BS.1770-4's Tables 1 and 2, the shelf then the high-pass, at 48 kHz, as published.
            std::array<biquad_row<double>, 2> k_weighting_tables() noexcept;
            /// The K-weighting's two sections at `sample_rate_hz`, normalised, in double; the
            /// card's statuses, `low` the format's `min_frequency`. Checks the rate, not the
            /// format: `make()` does that.
            expected<std::array<biquad_row<double>, 2>, status> k_weighting_sections(double sample_rate_hz,
                                                                                     double low) noexcept;
            /// The latency by the card's rule.
            stage_latency k_weighting_latency(span<const biquad_row<double>> sections, double sample_rate_hz) noexcept;

        } // namespace detail

        /// @brief The K-weighting of BS.1770-4 at a rate, as two biquad sections in S.
        template <typename S>
        class k_weighting_design {
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                              || std::is_same<S, q31>::value,
                          "catenae: a K-weighting design is float, double or q31");

          public:
            using design_type = biquad_design<S, 2>; ///< what make() returns

            /// The K-weighting at `sample_rate_hz` (the card's statuses). Outside the audio path.
            static expected<design_type, status> make(double sample_rate_hz) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point K-weighting needs its section scaling, which arrives at C6 "
                              "(D47)");
                const expected<std::array<detail::biquad_row<double>, 2>, status> exact =
                    detail::k_weighting_sections(sample_rate_hz, min_frequency<S>);
                if (!exact) {
                    return unexpected(exact.error());
                }
                // Taken once, where the compiler sees it is there: no check is left to reach a handler.
                const std::array<detail::biquad_row<double>, 2> sections = *exact;
                return design_type::from_double(
                    sections,
                    detail::k_weighting_latency(span<const detail::biquad_row<double>>(sections), sample_rate_hz));
            }
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_LOUDNESS_DESIGN_H
