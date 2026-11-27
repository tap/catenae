/// @file autocorrelation.h
/// @brief `autocorrelation`: each channel's sums of x[n] x[n - k] for lags 0 to `MaxLag`, over a
/// window of whole hops, a result every hop; computed directly (C5, D11).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | `port<S, Ch>`; an analyzer, put in a chain by `tap<Ch, autocorrelation<...>>` |
/// | Results    | every `Hop` frames, per channel and lag k from 0 to `MaxLag`, r[k] = the sum |
/// |            | over the last `Window` frames n of x[n] x[n - k]; x[n - k] may precede the   |
/// |            | window; frames before reset count as silence                                 |
/// | Sum        | as `rms_meter`'s: each hop in frame order, the hops oldest first, in the     |
/// |            | format's own type; a fixed-point format in 64 bits, Q15 exactly, a 32-bit    |
/// |            | format's products each rounded half up by s = floor(log2 Window) bits        |
/// | Result     | `std::array<std::array<sum, MaxLag + 1>, Ch>`, raw sums: r[c][0] is the      |
/// |            | channel's energy                                                             |
/// | Reading    | `normalized(r, c, k)`: r[c][k] / r[c][0], in [-1, 1]; 0 for a silent channel; |
/// |            | off the audio path                                                           |
/// | State      | `MaxLag` past frames per channel (written twice, so a lag reads one span),   |
/// |            | each hop's sums, the hop so far and the latest result; at rest all zero      |
/// |            | (`rests_at_zero`)                                                            |
/// | Formats    | float, double, q15, q31, Q4.27 (D47)                                         |
/// | Cost       | per frame and channel `MaxLag + 1` products; per hop `Window / Hop` adds a   |
/// |            | lag and channel                                                              |
/// | Contraction| pinned at contraction off; under the caller's flags a fused multiply-add can |
/// |            | move a float sum by an ULP                                                   |
/// | Rationale  | D11, D24, D36, D47, D49; lag 0 alone is `rms_meter`                          |
///
/// @req{CAT-AUTOCORRELATION-001} After every `Hop` frames from reset, each channel's r[k] for k
/// from 0 to `MaxLag` is the sum over its last `Window` frames n of x[n] x[n - k], frames before
/// reset counting as zero, each hop in frame order and the hops oldest first, in the format's
/// own type, bit for bit; in a 32-bit format each product rounded half up by floor(log2 Window).
/// @req{CAT-AUTOCORRELATION-002} `normalized` is r[k] / r[0], and 0 where r[0] is 0; a sine's
/// normalized r at its period is within 0.01 of 1.
/// @req{CAT-AUTOCORRELATION-003} A window that is not a whole number of hops, or a `MaxLag` of 0,
/// does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_AUTOCORRELATION_H
#define CATENAE_AUTOCORRELATION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/compiler.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/meter.h"
#include "catenae/port.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief Each channel's autocorrelation sums for lags 0 to `MaxLag` over `Window` frames,
        /// every `Hop`.
        template <typename S, std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window = Hop>
        class basic_autocorrelation {
            static_assert(Hop >= 1 && Window >= Hop && Window % Hop == 0,
                          "catenae: a meter's window is a whole number of its hops");
            static_assert(Window <= std::size_t{1} << 31u, "catenae: a meter's window is at most 2^31 frames");
            static_assert(MaxLag >= 1 && MaxLag < std::size_t{1} << 31u,
                          "catenae: an autocorrelation has a lag above 0; lag 0 alone is rms_meter");
            static constexpr std::size_t k_hops  = Window / Hop;
            static constexpr int         k_shift = detail::meter_shift<S, Window>();

          public:
            using input_port                           = port<S, Ch>;                      ///< what it reads
            using value_type                           = storage_t<S>;                     ///< a sample
            using sum_type                             = detail::meter_sum_t<S>;           ///< what products sum in
            using lags_type                            = std::array<sum_type, MaxLag + 1>; ///< r[0] to r[MaxLag]
            using result_type                          = std::array<lags_type, Ch>;        ///< each channel's
            using reading_type                         = detail::reading_t<S>;             ///< what a reading is in
            static constexpr std::size_t hop_frames    = Hop;    ///< a result every Hop frames
            static constexpr std::size_t window_frames = Window; ///< over the last Window
            static constexpr std::size_t max_lag       = MaxLag; ///< the longest lag
            static constexpr bool        rests_at_zero = true;   ///< all zero is at rest
            /// The bits each product is shifted right by: s for a 32-bit format, else 0.
            static constexpr int shift = k_shift;

            /// The past frames, each hop's sums, the hop so far and the latest result.
            struct state {
                std::array<std::array<value_type, 2 * MaxLag>, Ch> past;    ///< newest first from `at`, twice
                std::array<result_type, k_hops>                    hops;    ///< the window's hops
                result_type                                        running; ///< the hop so far
                result_type                                        latest;  ///< the latest result
                std::uint32_t                                      at;      ///< where the newest past frame is
                std::uint32_t                                      frames;  ///< frames into the hop
                std::uint32_t                                      slot;    ///< where the next hop's sums go
                std::uint32_t                                      ready;   ///< results since reset, wrapping
            };

            /// Each frame's products into the hop; a result at each hop's end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                const std::size_t       frames = in.size() / Ch;
                const value_type* const x      = in.data();
                std::size_t             done   = 0;
                while (done < frames) {
                    const std::size_t n  = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    std::uint32_t     at = st.at;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        sum_type* CATENAE_RESTRICT   r    = st.running[c].data();
                        value_type* CATENAE_RESTRICT past = st.past[c].data();
                        at                                = st.at;
                        for (std::size_t f = done; f < done + n; ++f) {
                            const value_type v = x[f * Ch + c];
                            r[0] += detail::meter_product<S, k_shift>(v, v);
                            for (std::size_t k = 1; k <= MaxLag; ++k) {
                                r[k] += detail::meter_product<S, k_shift>(v, past[at + k - 1]);
                            }
                            at                = at == 0 ? static_cast<std::uint32_t>(MaxLag - 1) : at - 1;
                            past[at]          = v;
                            past[at + MaxLag] = v;
                        }
                    }
                    st.at = at;
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

            /// r[c][k] / r[c][0]; 0 for a silent channel. Off the audio path.
            static reading_type normalized(const result_type& r, std::size_t c, std::size_t k) noexcept {
                const auto energy = static_cast<reading_type>(r[c][0]);
                return energy > reading_type{0} ? static_cast<reading_type>(r[c][k]) / energy : reading_type{0};
            }

          private:
            /// The hop's sums into the window, and the window's sums as the result.
            static void close_hop(state& st) noexcept {
                st.hops[st.slot] = st.running;
                st.running       = result_type{};
                st.slot          = st.slot + 1 == k_hops ? 0 : st.slot + 1;
                for (std::size_t c = 0; c < Ch; ++c) {
                    for (std::size_t k = 0; k <= MaxLag; ++k) {
                        sum_type sum = sum_type{};
                        for (std::size_t h = 0; h < k_hops; ++h) {
                            const std::size_t i = st.slot + h < k_hops ? st.slot + h : st.slot + h - k_hops;
                            sum += st.hops[i][c][k];
                        }
                        st.latest[c][k] = sum;
                    }
                }
                st.frames = 0;
                ++st.ready;
            }
        };

        template <std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window = Hop>
        using autocorrelation = basic_autocorrelation<float, Ch, MaxLag, Hop, Window>; ///< in float (D24)
        template <std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window = Hop>
        using autocorrelation_f64 = basic_autocorrelation<double, Ch, MaxLag, Hop, Window>; ///< in double
        template <std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window = Hop>
        using autocorrelation_q15 = basic_autocorrelation<q15, Ch, MaxLag, Hop, Window>; ///< in Q15
        template <std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window = Hop>
        using autocorrelation_q31 = basic_autocorrelation<q31, Ch, MaxLag, Hop, Window>; ///< in Q31
        template <std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window = Hop>
        using autocorrelation_q4_27 = basic_autocorrelation<q4_27, Ch, MaxLag, Hop, Window>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::size_t MaxLag, std::size_t Hop, std::size_t Window>
        struct is_analyzer<basic_autocorrelation<S, Ch, MaxLag, Hop, Window>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_AUTOCORRELATION_H
