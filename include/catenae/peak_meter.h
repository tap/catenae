/// @file peak_meter.h
/// @brief `peak_meter`: each channel's largest sample magnitude over a window of whole hops, a
/// result every hop (C5, D11).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | `port<S, Ch>`; an analyzer, put in a chain by `tap<Ch, peak_meter<...>>`     |
/// | Results    | every `Hop` frames, each channel's largest \|x\| over the last `Window`      |
/// |            | frames, `Window` a whole number of hops; frames before reset count as silence |
/// | Result     | `std::array<storage, Ch>`, in the format's own units                         |
/// | Reading    | `dbfs(peak)`: 20 log10 of the peak over full scale, off the audio path (a    |
/// |            | compiled log10); full scale is 1, or 2^F for a fixed-point format, so Q4.27  |
/// |            | reads above 0 dBFS in its headroom; silence reads minus infinity            |
/// | State      | each hop's peak per channel, the hop so far, the latest result; at rest all  |
/// |            | zero (`rests_at_zero`)                                                       |
/// | Formats    | float, double, q15, q31, Q4.27 (D47): a fixed-point magnitude saturates, so  |
/// |            | the most negative value reads as the most positive                           |
/// | Limits     | a NaN in the window is its channel's result until the window has passed it   |
/// | Cost       | per frame and channel a magnitude and a comparison; per hop the window's     |
/// |            | `Window / Hop` hops compared per channel                                     |
/// | Rationale  | D11, D24, D47, D49                                                           |
///
/// @req{CAT-PEAK-METER-001} After every `Hop` frames from reset, each channel's result is the
/// largest magnitude among that channel's last `Window` frames, frames before reset counting as
/// zero, bit for bit in every format, and `results_ready` has counted one result.
/// @req{CAT-PEAK-METER-002} A fixed-point magnitude saturates: the most negative value reads as
/// the most positive; a NaN in the window is the channel's result.
/// @req{CAT-PEAK-METER-003} `dbfs` is 20 log10 of the peak over full scale: 1 for float and
/// double, 2^F raw for `fixed<S, F>`.
/// @req{CAT-PEAK-METER-004} A window that is not a whole number of hops does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PEAK_METER_H
#define CATENAE_PEAK_METER_H

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

        /// @brief Each channel's largest magnitude over `Window` frames of a `port<S, Ch>`, every `Hop`.
        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        class basic_peak_meter {
            static_assert(Hop >= 1 && Window >= Hop && Window % Hop == 0,
                          "catenae: a meter's window is a whole number of its hops");
            static_assert(Window <= std::size_t{1} << 31u, "catenae: a meter's window is at most 2^31 frames");
            static constexpr std::size_t k_hops = Window / Hop;

          public:
            using input_port                           = port<S, Ch>;                ///< what it reads
            using value_type                           = storage_t<S>;               ///< a sample, and a peak
            using result_type                          = std::array<value_type, Ch>; ///< each channel's peak
            using reading_type                         = detail::reading_t<S>;       ///< what a reading is in
            static constexpr std::size_t hop_frames    = Hop;                        ///< a result every Hop frames
            static constexpr std::size_t window_frames = Window;                     ///< over the last Window
            static constexpr bool        rests_at_zero = true;                       ///< all zero is at rest

            /// Each hop's peaks, the hop so far and the latest result.
            struct state {
                std::array<result_type, k_hops> hops;    ///< the window's hops, slot by slot
                result_type                     running; ///< the hop so far
                result_type                     latest;  ///< the latest result
                std::uint32_t                   frames;  ///< frames into the hop
                std::uint32_t                   slot;    ///< where the next hop's peaks go
                std::uint32_t                   ready;   ///< results since reset, wrapping
            };

            /// Each frame's magnitudes into the hop; a result at each hop's end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                const std::size_t       frames = in.size() / Ch;
                const value_type* const x      = in.data();
                std::size_t             done   = 0;
                while (done < frames) {
                    const std::size_t n = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    for (std::size_t f = done; f < done + n; ++f) {
                        for (std::size_t c = 0; c < Ch; ++c) {
                            st.running[c] =
                                detail::meter_larger(st.running[c], detail::meter_magnitude<S>(x[f * Ch + c]));
                        }
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

            /// 20 log10 of a peak over full scale. Off the audio path.
            static reading_type dbfs(value_type peak) noexcept {
                return detail::amplitude_db(detail::over_full_scale<S>(peak));
            }

          private:
            /// The hop's peaks into the window, and the window's largest as the result.
            static void close_hop(state& st) noexcept {
                st.hops[st.slot] = st.running;
                st.running       = result_type{};
                st.slot          = st.slot + 1 == k_hops ? 0 : st.slot + 1;
                // The oldest hop is in the slot the next will take; each channel's largest is
                // taken oldest first, so a NaN stays wherever in the window it is.
                for (std::size_t c = 0; c < Ch; ++c) {
                    value_type largest = value_type{};
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
        using peak_meter = basic_peak_meter<float, Ch, Hop, Window>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using peak_meter_f64 = basic_peak_meter<double, Ch, Hop, Window>; ///< in double
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using peak_meter_q15 = basic_peak_meter<q15, Ch, Hop, Window>; ///< in Q15
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using peak_meter_q31 = basic_peak_meter<q31, Ch, Hop, Window>; ///< in Q31
        template <std::size_t Ch, std::size_t Hop, std::size_t Window = Hop>
        using peak_meter_q4_27 = basic_peak_meter<q4_27, Ch, Hop, Window>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t Window>
        struct is_analyzer<basic_peak_meter<S, Ch, Hop, Window>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PEAK_METER_H
