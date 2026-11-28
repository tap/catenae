/// @file cross_correlation.h
/// @brief `cross_correlation`: two channels' sums of a x b at lags from -`MaxLag` to `MaxLag`, with
/// each channel's energy, over a window of whole hops, a result every hop; computed directly
/// (C5, D11; PLAN.md 2: two signals meet as channels of one port).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | channels `A` and `B` of a `port<S, Ch>`; an analyzer, put in a chain by      |
/// |            | `tap<Ch, cross_correlation<...>>`                                            |
/// | Results    | every `Hop` frames, over the last `Window` frames n: for each k from 0 to    |
/// |            | `MaxLag`, r[k] = sum of a[n - k] b[n] (b later than a by k) and r[-k] = sum  |
/// |            | of a[n] b[n - k]; and the energies, sums of a[n]^2 and of b[n]^2. A lagged   |
/// |            | sample may precede the window; frames before reset count as silence         |
/// | Sum        | as `rms_meter`'s: each hop in frame order, the hops oldest first, in the     |
/// |            | format's own type; a fixed-point format in 64 bits, Q15 exactly, a 32-bit    |
/// |            | format's products each rounded half up by s = floor(log2 Window) bits        |
/// | Result     | `cross_sums<sum, MaxLag>`: `lags[MaxLag + k]` is r[k], and `energy_a`,       |
/// |            | `energy_b`: raw sums                                                         |
/// | Reading    | `normalized(r, k)`: r[k] / sqrt(energy_a energy_b), in [-1, 1] for the lags' |
/// |            | windows as for one (Pearson's coefficient at lag 0); 0 where an energy is 0; |
/// |            | off the audio path                                                           |
/// | State      | `MaxLag` past frames of each channel (written twice, so a lag reads one      |
/// |            | span), each hop's sums, the hop so far and the latest result; at rest all    |
/// |            | zero (`rests_at_zero`)                                                       |
/// | Formats    | float, double, q15, q31, Q4.27 (D47)                                         |
/// | Cost       | per frame 2 `MaxLag` + 3 products; per hop `Window / Hop` adds a sum         |
/// | Contraction| pinned at contraction off; under the caller's flags a fused multiply-add can |
/// |            | move a float sum by an ULP                                                   |
/// | Rationale  | D11, D24, D36, D47, D49, PLAN.md 2                                           |
///
/// @req{CAT-CROSS-CORRELATION-001} After every `Hop` frames from reset, r[k] for k from -`MaxLag`
/// to `MaxLag` and both energies are the card's sums over the last `Window` frames, frames before
/// reset counting as zero, each hop in frame order and the hops oldest first, in the format's
/// own type, bit for bit; in a 32-bit format each product rounded half up by floor(log2 Window).
/// @req{CAT-CROSS-CORRELATION-002} `normalized` is r[k] / sqrt(energy_a energy_b), 0 where an
/// energy is 0: b as a delayed by d frames reads 1 at k = d, within rounding, and -b reads -1.
/// @req{CAT-CROSS-CORRELATION-003} A channel `A` or `B` not below `Ch`, `A` equal to `B`, a
/// `MaxLag` of 0 or a window that is not a whole number of hops does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_CROSS_CORRELATION_H
#define CATENAE_CROSS_CORRELATION_H

#include <array>
#include <cmath>
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

        /// Two channels' cross-correlation sums: `lags[MaxLag + k]` is r[k], k from -MaxLag to MaxLag.
        template <typename V, std::size_t MaxLag>
        struct cross_sums {
            std::array<V, 2 * MaxLag + 1> lags;     ///< r[-MaxLag] to r[MaxLag]
            V                             energy_a; ///< the sum of a[n]^2
            V                             energy_b; ///< the sum of b[n]^2
        };

        /// @brief Channels `A` and `B` of a `port<S, Ch>`: their cross-correlation sums for lags
        /// -`MaxLag` to `MaxLag` over `Window` frames, every `Hop`.
        template <typename S, std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window = Hop>
        class basic_cross_correlation {
            static_assert(A < Ch && B < Ch && A != B, "catenae: a cross-correlation reads two channels of its port");
            static_assert(Hop >= 1 && Window >= Hop && Window % Hop == 0,
                          "catenae: a meter's window is a whole number of its hops");
            static_assert(Window <= std::size_t{1} << 31u, "catenae: a meter's window is at most 2^31 frames");
            static_assert(MaxLag >= 1 && MaxLag < std::size_t{1} << 30u,
                          "catenae: a cross-correlation has a lag above 0");
            static constexpr std::size_t k_hops  = Window / Hop;
            static constexpr int         k_shift = detail::meter_shift<S, Window>();

          public:
            using input_port                           = port<S, Ch>;                  ///< what it reads
            using value_type                           = storage_t<S>;                 ///< a sample
            using sum_type                             = detail::meter_sum_t<S>;       ///< what products sum in
            using result_type                          = cross_sums<sum_type, MaxLag>; ///< the lags and energies
            using reading_type                         = detail::reading_t<S>;         ///< what a reading is in
            static constexpr std::size_t hop_frames    = Hop;                          ///< a result every Hop frames
            static constexpr std::size_t window_frames = Window;                       ///< over the last Window
            static constexpr std::size_t max_lag       = MaxLag;                       ///< the longest lag either way
            static constexpr bool        rests_at_zero = true;                         ///< all zero is at rest
            /// The bits each product is shifted right by: s for a 32-bit format, else 0.
            static constexpr int shift = k_shift;

            /// The past frames, each hop's sums, the hop so far and the latest result.
            struct state {
                std::array<value_type, 2 * MaxLag> past_a;  ///< a, newest first from `at`, twice
                std::array<value_type, 2 * MaxLag> past_b;  ///< b, the same
                std::array<result_type, k_hops>    hops;    ///< the window's hops
                result_type                        running; ///< the hop so far
                result_type                        latest;  ///< the latest result
                std::uint32_t                      at;      ///< where the newest past frame is
                std::uint32_t                      frames;  ///< frames into the hop
                std::uint32_t                      slot;    ///< where the next hop's sums go
                std::uint32_t                      ready;   ///< results since reset, wrapping
            };

            /// Each frame's products into the hop; a result at each hop's end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                const std::size_t       frames = in.size() / Ch;
                const value_type* const x      = in.data();
                std::size_t             done   = 0;
                while (done < frames) {
                    const std::size_t            n = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    sum_type* CATENAE_RESTRICT   r = st.running.lags.data();
                    value_type* CATENAE_RESTRICT past_a = st.past_a.data();
                    value_type* CATENAE_RESTRICT past_b = st.past_b.data();
                    sum_type                     ea     = st.running.energy_a;
                    sum_type                     eb     = st.running.energy_b;
                    std::uint32_t                at     = st.at;
                    for (std::size_t f = done; f < done + n; ++f) {
                        const value_type a = x[f * Ch + A];
                        const value_type b = x[f * Ch + B];
                        ea += detail::meter_product<S, k_shift>(a, a);
                        eb += detail::meter_product<S, k_shift>(b, b);
                        r[MaxLag] += detail::meter_product<S, k_shift>(a, b);
                        for (std::size_t k = 1; k <= MaxLag; ++k) {
                            r[MaxLag + k] += detail::meter_product<S, k_shift>(past_a[at + k - 1], b);
                            r[MaxLag - k] += detail::meter_product<S, k_shift>(a, past_b[at + k - 1]);
                        }
                        at                  = at == 0 ? static_cast<std::uint32_t>(MaxLag - 1) : at - 1;
                        past_a[at]          = a;
                        past_a[at + MaxLag] = a;
                        past_b[at]          = b;
                        past_b[at + MaxLag] = b;
                    }
                    st.running.energy_a = ea;
                    st.running.energy_b = eb;
                    st.at               = at;
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

            /// r[k] / sqrt(energy_a energy_b), k from -MaxLag to MaxLag; 0 where an energy is 0.
            /// Off the audio path.
            static reading_type normalized(const result_type& r, std::ptrdiff_t k) noexcept {
                const auto ea = static_cast<reading_type>(r.energy_a);
                const auto eb = static_cast<reading_type>(r.energy_b);
                if (!(ea > reading_type{0}) || !(eb > reading_type{0})) {
                    return reading_type{0};
                }
                const auto i = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(MaxLag) + k);
                return static_cast<reading_type>(r.lags[i]) / (std::sqrt(ea) * std::sqrt(eb));
            }

          private:
            /// The hop's sums into the window, and the window's sums as the result.
            static void close_hop(state& st) noexcept {
                st.hops[st.slot] = st.running;
                st.running       = result_type{};
                st.slot          = st.slot + 1 == k_hops ? 0 : st.slot + 1;
                result_type sum{};
                for (std::size_t h = 0; h < k_hops; ++h) {
                    const result_type& hop = st.hops[st.slot + h < k_hops ? st.slot + h : st.slot + h - k_hops];
                    for (std::size_t i = 0; i < 2 * MaxLag + 1; ++i) {
                        sum.lags[i] += hop.lags[i];
                    }
                    sum.energy_a += hop.energy_a;
                    sum.energy_b += hop.energy_b;
                }
                st.latest = sum;
                st.frames = 0;
                ++st.ready;
            }
        };

        template <std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window = Hop>
        using cross_correlation = basic_cross_correlation<float, Ch, A, B, MaxLag, Hop, Window>; ///< in float
        template <std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window = Hop>
        using cross_correlation_f64 = basic_cross_correlation<double, Ch, A, B, MaxLag, Hop, Window>; ///< in double
        template <std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window = Hop>
        using cross_correlation_q15 = basic_cross_correlation<q15, Ch, A, B, MaxLag, Hop, Window>; ///< in Q15
        template <std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window = Hop>
        using cross_correlation_q31 = basic_cross_correlation<q31, Ch, A, B, MaxLag, Hop, Window>; ///< in Q31
        template <std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window = Hop>
        using cross_correlation_q4_27 = basic_cross_correlation<q4_27, Ch, A, B, MaxLag, Hop, Window>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::size_t A, std::size_t B, std::size_t MaxLag, std::size_t Hop,
                  std::size_t Window>
        struct is_analyzer<basic_cross_correlation<S, Ch, A, B, MaxLag, Hop, Window>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_CROSS_CORRELATION_H
