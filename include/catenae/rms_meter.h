/// @file rms_meter.h
/// @brief `rms_meter`: each channel's energy, the sum of its squares, over a window of whole
/// hops, a result every hop; its reading is the RMS level (C5, D11, PLAN.md 9.6).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | `port<S, Ch>`; an analyzer, put in a chain by `tap<Ch, rms_meter<...>>`      |
/// | Results    | every `Hop` frames, each channel's sum of x^2 over the last `Window` frames, |
/// |            | `Window` a whole number of hops; frames before reset count as silence        |
/// | Sum        | each hop summed in frame order, then the window's hops oldest first, in the  |
/// |            | format's own type (float in float, D36); a fixed-point format sums in 64     |
/// |            | bits: Q15's squares exactly, a 32-bit format's each rounded half up to s =   |
/// |            | floor(log2 Window) fewer bits, so a window of full-scale squares fits        |
/// | Result     | `std::array<sum, Ch>`: float, double, or `std::int64_t` in units of          |
/// |            | 2^s raw^2                                                                    |
/// | Reading    | `dbfs(energy)`: 10 log10 of the mean square over full scale squared, off the |
/// |            | audio path; a full-scale sine reads -3.01 dBFS, silence minus infinity       |
/// | State      | each hop's sums per channel, the hop so far, the latest result; at rest all  |
/// |            | zero (`rests_at_zero`)                                                       |
/// | Formats    | float, double, q15, q31, Q4.27 (D47)                                         |
/// | Limits     | a non-finite sample is its channel's result until the window has passed it   |
/// | Cost       | per frame and channel a square and an add; per hop `Window / Hop` adds a     |
/// |            | channel                                                                      |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, and a fused multiply-add there can move a float sum by an ULP         |
/// | Rationale  | D11, D24, D36, D47, D49                                                      |
///
/// @req{CAT-RMS-METER-001} After every `Hop` frames from reset, each channel's result is the sum
/// of its last `Window` frames' squares, frames before reset counting as zero: each hop summed
/// in frame order and the hops oldest first, in the format's own type, bit for bit.
/// @req{CAT-RMS-METER-002} In a fixed-point format the squares sum in 64 bits: Q15's exactly, a
/// 32-bit format's each rounded half up by s = floor(log2 Window) bits, and a window of the most
/// negative value does not overflow.
/// @req{CAT-RMS-METER-003} `dbfs` is 10 log10 of the window's mean square over full scale
/// squared: a full-scale sine reads -3.01 dB in every format.
/// @req{CAT-RMS-METER-004} A window that is not a whole number of hops does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_RMS_METER_H
#define CATENAE_RMS_METER_H

#include <array>
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

        /// @brief Each channel's sum of squares over `Window` frames of a `port<S, Ch>`, every `Hop`.
        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        class basic_rms_meter {
            static_assert(Hop >= 1 && Window >= Hop && Window % Hop == 0,
                          "catenae: a meter's window is a whole number of its hops");
            static_assert(Window <= std::size_t{1} << 31u, "catenae: a meter's window is at most 2^31 frames");
            static constexpr std::size_t k_hops  = Window / Hop;
            static constexpr int         k_shift = detail::meter_shift<S, Window>();

          public:
            using input_port                           = port<S, Ch>;              ///< what it reads
            using value_type                           = storage_t<S>;             ///< a sample
            using sum_type                             = detail::meter_sum_t<S>;   ///< what squares sum in
            using result_type                          = std::array<sum_type, Ch>; ///< each channel's energy
            using reading_type                         = detail::reading_t<S>;     ///< what a reading is in
            static constexpr std::size_t hop_frames    = Hop;                      ///< a result every Hop frames
            static constexpr std::size_t window_frames = Window;                   ///< over the last Window
            static constexpr bool        rests_at_zero = true;                     ///< all zero is at rest
            /// The bits each square is shifted right by: s for a 32-bit format, else 0.
            static constexpr int shift = k_shift;

            /// Each hop's sums, the hop so far and the latest result.
            struct state {
                std::array<result_type, k_hops> hops;    ///< the window's hops, slot by slot
                result_type                     running; ///< the hop so far
                result_type                     latest;  ///< the latest result
                std::uint32_t                   frames;  ///< frames into the hop
                std::uint32_t                   slot;    ///< where the next hop's sums go
                std::uint32_t                   ready;   ///< results since reset, wrapping
            };

            /// Each frame's squares into the hop; a result at each hop's end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                const std::size_t       frames = in.size() / Ch;
                const value_type* const x      = in.data();
                std::size_t             done   = 0;
                while (done < frames) {
                    const std::size_t n = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        sum_type sum = st.running[c];
                        for (std::size_t f = done; f < done + n; ++f) {
                            const value_type v = x[f * Ch + c];
                            sum += detail::meter_product<S, k_shift>(v, v);
                        }
                        st.running[c] = sum;
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

            /// The mean square over full scale squared of a window's energy.
            static reading_type mean_square(sum_type energy) noexcept {
                // 2^(s - 2F) / Window, a constant: the units of a sum over the window's frames.
                constexpr reading_type per_unit =
                    detail::power_of_two<reading_type>(k_shift - 2 * format_traits<S>::fraction_bits)
                    / static_cast<reading_type>(Window);
                return static_cast<reading_type>(energy) * per_unit;
            }
            /// 10 log10 of the mean square over full scale squared. Off the audio path.
            static reading_type dbfs(sum_type energy) noexcept { return detail::power_db(mean_square(energy)); }

          private:
            /// The hop's sums into the window, and the window's sums as the result.
            static void close_hop(state& st) noexcept {
                st.hops[st.slot] = st.running;
                st.running       = result_type{};
                st.slot          = st.slot + 1 == k_hops ? 0 : st.slot + 1;
                for (std::size_t c = 0; c < Ch; ++c) {
                    sum_type sum = sum_type{};
                    for (std::size_t h = 0; h < k_hops; ++h) {
                        const std::size_t at = st.slot + h < k_hops ? st.slot + h : st.slot + h - k_hops;
                        sum += st.hops[at][c];
                    }
                    st.latest[c] = sum;
                }
                st.frames = 0;
                ++st.ready;
            }
        };

        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using rms_meter = basic_rms_meter<float, Ch, Hop, Window>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using rms_meter_f64 = basic_rms_meter<double, Ch, Hop, Window>; ///< in double
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using rms_meter_q15 = basic_rms_meter<q15, Ch, Hop, Window>; ///< in Q15
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using rms_meter_q31 = basic_rms_meter<q31, Ch, Hop, Window>; ///< in Q31
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using rms_meter_q4_27 = basic_rms_meter<q4_27, Ch, Hop, Window>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t Window>
        struct is_analyzer<basic_rms_meter<S, Ch, Hop, Window>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_RMS_METER_H
