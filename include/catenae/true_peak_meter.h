/// @file true_peak_meter.h
/// @brief `true_peak_meter`: each channel's largest magnitude of its signal interpolated four
/// times, over a window of whole hops, a result every hop (C5, ITU-R BS.1770-4 Annex 2).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | `port<S, Ch>`; an analyzer, put in a chain by `tap<Ch, true_peak_meter<...>>` |
/// | Method     | BS.1770-4 Annex 2: 4x over-sampling by its order-48, 4-phase FIR, then the  |
/// |            | absolute value. The four rows of the Annex's table, each dotted with the     |
/// |            | channel's last 12 frames oldest first, are the four interpolated points of   |
/// |            | each frame; the same filter at every rate, 4x of it (the Annex's 48 kHz      |
/// |            | filter, its ratio preferred at any rate). No 12.04 dB attenuation: the sums  |
/// |            | have the headroom                                                            |
/// | Results    | every `Hop` frames, each channel's largest interpolated magnitude over the   |
/// |            | last `Window` frames, `Window` a whole number of hops; frames before reset   |
/// |            | are silence                                                                  |
/// | Result     | `std::array<peak, Ch>`: in S for a floating format, in units of 2^-13 raw    |
/// |            | for a fixed-point one, an `int32_t` for 16-bit storage and an `int64_t` for  |
/// |            | 32-bit storage                                                               |
/// | Reading    | `dbtp(peak)`: 20 log10 of the peak over full scale (1, or 2^F raw), off the  |
/// |            | audio path; above 0 dB TP where the waveform is; silence reads minus infinity |
/// | Arithmetic | the taps are the Annex's, multiples of 2^-13: exact in float and double,     |
/// |            | which sum each point in S, its products in tap order; exact in integers for  |
/// |            | a fixed-point format, whose sums have no rounding point and cannot overflow  |
/// |            | (the largest, 2.023 x full scale, is below 2^30 from Q15, 2^46 from 32 bits) |
/// | State      | each channel's last 12 frames, twice over; each hop's peaks, the hop so far, |
/// |            | the latest result; at rest all zero (`rests_at_zero`)                        |
/// | Formats    | float, double, q15, q31, Q4.27 (D47)                                         |
/// | Delay      | the filter's 23.5 points at 4x: frame n's points are the waveform at n -     |
/// |            | 5.875, -5.625, -5.375 and -5.125 frames, so a peak's result can come a hop   |
/// |            | after the frame that holds it                                                |
/// | Limits     | the Annex's filter as published: its points fall an eighth of a frame off    |
/// |            | the samples, so a peak on a sample reads below it; its phases' DC gains are  |
/// |            | +0.014 and -0.238 dB; at 48 kHz EBU Tech 3341's signals 15 to 19 read -6.22, |
/// |            | -5.98, -6.31, -6.03 and +3.03 dB TP, and 20 to 23, as the battery makes them |
/// |            | (401 taps at 4x, Kaiser beta 10), -0.03, +0.03, -0.14 and -0.18: each within |
/// |            | +0.2/-0.4 of the table's -6.0, +3.0 and 0.0; a test pins them. A NaN in the  |
/// |            | last 12 frames is its channel's result until the window has passed it       |
/// | Cost       | per frame and channel 48 multiply-adds, four magnitudes and comparisons; per |
/// |            | hop the window's `Window / Hop` hops compared per channel                    |
/// | Contraction| floating sums pinned at contraction off; a header template, it compiles      |
/// |            | under the caller's flags, and a fused multiply-add there can move a point by |
/// |            | an ULP. The fixed-point sums are exact under any flags                       |
/// | Rationale  | D11, D12, D24, D47, D49                                                      |
///
/// @req{CAT-TRUE-PEAK-001} After every `Hop` frames from reset, each channel's result is the
/// largest magnitude among the four interpolated points of each of its last `Window` frames,
/// the card's sums, frames before reset counting as zero, bit for bit in every format.
/// @req{CAT-TRUE-PEAK-002} At 48 kHz EBU Tech 3341's true-peak signals 15 to 23 read the
/// card's numbers within 0.01 dB, each within the Tech's tolerance; a fixed-point format reads
/// what float reads of the same values within its quantization; a fixed-point sum's extremes do
/// not overflow; a NaN is its channel's result while it is in the window.
/// @req{CAT-TRUE-PEAK-003} `dbtp` is 20 log10 of the peak over full scale, 2^13 in the result's
/// units for a fixed-point format times 2^F.
/// @req{CAT-TRUE-PEAK-004} A window that is not a whole number of hops does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TRUE_PEAK_METER_H
#define CATENAE_TRUE_PEAK_METER_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/meter.h"
#include "catenae/port.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// BS.1770-4 Annex 2's order-48, 4-phase interpolating FIR, as published: phase by
            /// phase, its twelve coefficients in the Annex's order, each the integer it is times
            /// 2^-13 (0.0017089843750 is 14 x 2^-13).
            inline constexpr std::array<std::array<std::int16_t, 12>, 4> k_true_peak_phases = {{
                {{14, 90, -161, 272, -487, 1125, 7964, -838, 390, -218, 122, -68}},
                {{-239, 240, -424, 730, -1364, 3810, 6388, -1641, 832, -477, 271, -155}},
                {{-155, 271, -477, 832, -1641, 6388, 3810, -1364, 730, -424, 240, -239}},
                {{-68, 122, -218, 390, -838, 7964, 1125, -487, 272, -161, 90, 14}},
            }};

            /// The bits the Annex's coefficients are fractions of.
            inline constexpr int k_true_peak_fraction_bits = 13;

            /// What an interpolated point is summed in, and a peak held in: the sample's own type
            /// when it floats; for a fixed-point format an integer wide enough to hold the sum
            /// exactly, 32 bits from 16-bit storage and 64 from 32-bit storage.
            template <typename S>
            using true_peak_t =
                std::conditional_t<!format_traits<S>::is_fixed_point, storage_t<S>,
                                   std::conditional_t<sizeof(storage_t<S>) == 2, std::int32_t, std::int64_t>>;

            /// The coefficients in the type a point is summed in: exactly the Annex's numbers for
            /// a floating format, their integers for a fixed-point one.
            template <typename S>
            constexpr std::array<std::array<true_peak_t<S>, 12>, 4> true_peak_taps() noexcept {
                using value = true_peak_t<S>;
                std::array<std::array<value, 12>, 4> taps{};
                for (std::size_t k = 0; k < 4; ++k) {
                    for (std::size_t j = 0; j < 12; ++j) {
                        if constexpr (std::is_floating_point<value>::value) {
                            taps[k][j] = static_cast<value>(k_true_peak_phases[k][j])
                                         * power_of_two<value>(-k_true_peak_fraction_bits);
                        }
                        else {
                            taps[k][j] = static_cast<value>(k_true_peak_phases[k][j]);
                        }
                    }
                }
                return taps;
            }

        } // namespace detail

        /// @brief Each channel's largest interpolated magnitude over `Window` frames of a
        /// `port<S, Ch>`, every `Hop`, after BS.1770-4 Annex 2.
        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        class basic_true_peak_meter {
            static_assert(Hop >= 1 && Window >= Hop && Window % Hop == 0,
                          "catenae: a meter's window is a whole number of its hops");
            static_assert(Window <= std::size_t{1} << 31u, "catenae: a meter's window is at most 2^31 frames");
            static constexpr std::size_t k_hops    = Window / Hop;
            static constexpr std::size_t k_taps    = 12;
            static constexpr auto        k_weights = detail::true_peak_taps<S>();

          public:
            using input_port                           = port<S, Ch>;               ///< what it reads
            using value_type                           = storage_t<S>;              ///< a sample
            using peak_type                            = detail::true_peak_t<S>;    ///< a point, and a peak
            using result_type                          = std::array<peak_type, Ch>; ///< each channel's peak
            using reading_type                         = detail::reading_t<S>;      ///< what a reading is in
            static constexpr std::size_t hop_frames    = Hop;                       ///< a result every Hop frames
            static constexpr std::size_t window_frames = Window;                    ///< over the last Window
            static constexpr bool        rests_at_zero = true;                      ///< all zero is at rest

            /// Each channel's last frames, each hop's peaks, the hop so far and the latest result.
            struct state {
                std::array<std::array<value_type, 2 * k_taps>, Ch> history; ///< the last 12 frames, twice
                std::array<result_type, k_hops>                    hops;    ///< the window's hops
                result_type                                        running; ///< the hop so far
                result_type                                        latest;  ///< the latest result
                std::uint32_t                                      at;      ///< where the next frame goes
                std::uint32_t                                      frames;  ///< frames into the hop
                std::uint32_t                                      slot;    ///< where the next hop goes
                std::uint32_t                                      ready;   ///< results since reset, wrapping
            };

            /// Each frame's four points into the hop; a result at each hop's end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                const std::size_t       frames = in.size() / Ch;
                const value_type* const x      = in.data();
                std::size_t             done   = 0;
                while (done < frames) {
                    const std::size_t n = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    for (std::size_t f = done; f < done + n; ++f) {
                        // The frame goes in twice, so its last 12 frames, oldest first, are one
                        // run from the slot after it.
                        const std::uint32_t at = st.at;
                        for (std::size_t c = 0; c < Ch; ++c) {
                            value_type* const h = st.history[c].data();
                            h[at]               = x[f * Ch + c];
                            h[at + k_taps]      = x[f * Ch + c];
                            st.running[c]       = detail::meter_larger(st.running[c], largest_point(h + at + 1));
                        }
                        st.at = at + 1 == k_taps ? 0 : at + 1;
                    }
                    done += n;
                    st.frames += static_cast<std::uint32_t>(n);
                    if (st.frames == Hop) {
                        close_hop(st);
                    }
                }
            }

            /// The latest result.
            const result_type& result(const state& st) const noexcept { return st.latest; }
            /// Results since reset, a count that wraps.
            std::uint32_t results_ready(const state& st) const noexcept { return st.ready; }

            /// 20 log10 of a peak over full scale, in dB TP. Off the audio path.
            static reading_type dbtp(peak_type peak) noexcept {
                if constexpr (format_traits<S>::is_fixed_point) {
                    constexpr reading_type scale =
                        detail::power_of_two<reading_type>(-detail::k_true_peak_fraction_bits);
                    return detail::amplitude_db(detail::over_full_scale<S>(peak) * scale);
                }
                else {
                    return detail::amplitude_db(static_cast<reading_type>(peak));
                }
            }

          private:
            /// The largest magnitude of a frame's four points, from its last 12 frames, oldest first.
            static peak_type largest_point(const value_type* w) noexcept {
                peak_type largest = peak_type{};
                for (std::size_t k = 0; k < 4; ++k) {
                    peak_type point = peak_type{};
                    for (std::size_t m = 0; m < k_taps; ++m) {
                        point += k_weights[k][m] * static_cast<peak_type>(w[m]);
                    }
                    largest = detail::meter_larger(largest, magnitude(point));
                }
                return largest;
            }

            /// |point|: a floating one's, and an integer sum's, which is far inside its type.
            static peak_type magnitude(peak_type point) noexcept {
                if constexpr (std::is_floating_point<peak_type>::value) {
                    return std::fabs(point);
                }
                else {
                    return point < 0 ? static_cast<peak_type>(-point) : point;
                }
            }

            /// The hop's peaks into the window, and the window's largest as the result.
            static void close_hop(state& st) noexcept {
                st.hops[st.slot] = st.running;
                st.running       = result_type{};
                st.slot          = st.slot + 1 == k_hops ? 0 : st.slot + 1;
                for (std::size_t c = 0; c < Ch; ++c) {
                    peak_type largest = peak_type{};
                    for (std::size_t h = 0; h < k_hops; ++h) {
                        const std::size_t at = st.slot + h < k_hops ? st.slot + h : st.slot + h - k_hops;
                        largest              = detail::meter_larger(largest, st.hops[at][c]);
                    }
                    st.latest[c] = largest;
                }
                st.frames = 0;
                ++st.ready;
            }
        };

        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using true_peak_meter = basic_true_peak_meter<float, Ch, Hop, Window>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using true_peak_meter_f64 = basic_true_peak_meter<double, Ch, Hop, Window>; ///< in double
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using true_peak_meter_q15 = basic_true_peak_meter<q15, Ch, Hop, Window>; ///< in Q15
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using true_peak_meter_q31 = basic_true_peak_meter<q31, Ch, Hop, Window>; ///< in Q31
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using true_peak_meter_q4_27 = basic_true_peak_meter<q4_27, Ch, Hop, Window>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t Window>
        struct is_analyzer<basic_true_peak_meter<S, Ch, Hop, Window>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_TRUE_PEAK_METER_H
