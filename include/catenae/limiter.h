/// @file limiter.h
/// @brief `limiter`: a look-ahead peak limiter whose output never exceeds its ceiling, the
/// look-ahead a frame count in its type and its exact latency (PLAN.md 2).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                     |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous)                             |
/// | Look-ahead | `Lookahead` frames, 1 to 4,096: the signal is delayed by it                  |
/// | Configured | a `limiter_design<S>` by value (16 bytes in float); `set_design()` between    |
/// |            | calls                                                                        |
/// | State      | the delay line, the window's maxima and its running sum, Lemire's wedge of  |
/// |            | candidates, the release's value and the counters; rests at zero              |
/// | Workspace  | none                                                                         |
/// | Level      | per frame, the log of the largest magnitude of its channels, at least 2^-32: |
/// |            | one level, so every channel takes the same gain                              |
/// | Required   | r = max(0, x - T), at most 128 nepers (1,112 dB), rounded up to a multiple of |
/// |            | 2^-12 neper (0.0021 dB), so the window's arithmetic is exact in integers     |
/// | Gain       | the maximum of r over the last L + 1 frames (Lemire, "Streaming maximum-     |
/// |            | minimum filter using no more than three comparisons per element", Nordic J.  |
/// |            | Computing 13(4), 2006), then the mean of the last L + 1 maxima rounded up,   |
/// |            | then the release y = max(a, aR y + (1 - aR) a), below 2^-64 nepers 0; the    |
/// |            | frame L frames back times exp(-y), held within the ceiling                   |
/// | Ceiling    | no output sample exceeds the design's ceiling in magnitude: each of the L + 1 |
/// |            | maxima the mean takes covers the frame it is applied to, so the mean is at   |
/// |            | least that frame's r; the last hold at the ceiling is for the rounding of   |
/// |            | `log` and `exp`, which the grid's rounding up already covers: it moved no    |
/// |            | sample in the battery, and is pinned at 4 ULP at most under one design       |
/// | Arithmetic | in S, with the runtime's `log` and `exp` for S, the window in 32-bit        |
/// |            | integers; the stage's own float sums stay in float, and where the FPU has   |
/// |            | single precision only (M4F, M33) newlib's `logf` and `expf` do too; where it |
/// |            | has double (the M55) they take the double path in hardware; no image links a |
/// |            | software double                                                              |
/// | Latency    | exactly `Lookahead` frames                                                    |
/// | Tail       | `Lookahead` frames: the line empties                                         |
/// | Limits     | a non-finite sample's own output is not finite (`sanitize` first, PLAN.md    |
/// |            | 3.4); a double louder than 128 nepers above the threshold is held at the     |
/// |            | ceiling by the last step alone; after `set_design()` the window carries the  |
/// |            | old design's reductions for L + 1 frames, and the last step holds the ceiling |
/// | Formats    | float and double; q31 declares its contract and its arithmetic arrives with  |
/// |            | its profile at C6 (D47)                                                      |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, where a fused multiply-add moves the release's last bits              |
/// | Rationale  | D12, D24, D37, D39, D47, D49, PLAN.md 2                                      |
///
/// @req{CAT-LIMITER-001} From reset each output frame is the input frame `Lookahead` frames
/// earlier times exp(-y), y the card's gain after the current frame, held within the ceiling;
/// every channel takes the same gain, in place or not, and the output is the same bits for any
/// chunking.
/// @req{CAT-LIMITER-002} No output sample exceeds the ceiling in magnitude, and under one design
/// the last hold moves no sample by more than 4 ULP.
/// @req{CAT-LIMITER-003} Below the threshold the output is the input delayed by `Lookahead`
/// frames, bit for bit; its exact latency and its tail are `Lookahead` frames.
/// @req{CAT-LIMITER-004} A design set between calls applies from the next call to the state as
/// it stands.
/// @req{CAT-LIMITER-005} The float stage follows the double one within the bound its battery
/// states; in q31 the contract is instantiated and a run does not compile until C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_LIMITER_H
#define CATENAE_LIMITER_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/dynamics_kernel.h"
#include "catenae/dynamics_design.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// A limiter's grid: 2^12 steps a neper.
            inline constexpr std::uint32_t k_limiter_grid = 4096;
            /// The most a limiter's required reduction is taken as, nepers.
            inline constexpr std::uint32_t k_limiter_max_reduction = 128;

            /// A candidate for the window's maximum: the frame it arrived in and its value.
            struct limiter_candidate {
                std::uint32_t frame; ///< frames since reset, modulo 2^32
                std::uint32_t value; ///< the required reduction, in grid steps
            };
        } // namespace detail

        /// @brief A look-ahead peak limiter over a `port<S, Ch>`, `Lookahead` frames ahead.
        template <typename S, std::size_t Ch, std::size_t Lookahead>
        class basic_limiter {
            static_assert(Ch >= 1, "catenae: a limiter has at least one channel");
            static_assert(Lookahead >= 1 && Lookahead <= 4096, "catenae: a limiter looks 1 to 4,096 frames ahead");

          public:
            using input_port  = port<S, Ch>;       ///< what it reads
            using output_port = port<S, Ch>;       ///< what it writes
            using design_type = limiter_design<S>; ///< what it runs

          private:
            using value_type                        = storage_t<S>;
            using level_type                        = typename design_type::value_type;
            static constexpr std::size_t k_window   = Lookahead + 1; ///< frames a maximum is taken over
            static constexpr auto        k_ahead_32 = static_cast<std::uint32_t>(Lookahead);
            /// A grid step, nepers: 2^-12.
            static constexpr level_type k_step = level_type{1} / static_cast<level_type>(detail::k_limiter_grid);

          public:
            static constexpr std::size_t lookahead = Lookahead; ///< frames
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{k_ahead_32, 1};
            static constexpr bool        rests_at_zero = true; ///< an empty line, no reduction

            /// The line, the window and the release.
            struct state {
                std::array<value_type, Lookahead * Ch>          line;     ///< the delayed frames
                std::array<std::uint32_t, k_window>             maxima;   ///< the last L + 1 maxima
                std::array<detail::limiter_candidate, k_window> wedge;    ///< Lemire's candidates
                std::uint32_t                                   sum;      ///< of the maxima, grid steps
                level_type                                      released; ///< y, nepers
                std::uint32_t                                   frame;    ///< frames since reset
                std::uint32_t                                   at;       ///< the line's oldest frame
                std::uint32_t                                   slot;     ///< the maxima's next slot
                std::uint32_t                                   head;     ///< the wedge's oldest
                std::uint32_t                                   count;    ///< candidates in the wedge
            };
            using workspace = no_workspace; ///< nothing

            /// A limiter running `design`.
            explicit constexpr basic_limiter(const design_type& design) noexcept
                : m_design(design) {}

            /// The design it runs.
            constexpr const design_type& design() const noexcept { return m_design; }
            /// A new design, applied from the next run() to the state as it stands. Between calls
            /// only; it cannot fail.
            constexpr void set_design(const design_type& design) noexcept { m_design = design; }

            /// Every frame, `Lookahead` frames late, times its gain.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point limiter is new design and arrives with its profile at C6 (D47)");
                detail::check_audit(out.size() >= in.size(), check::index_out_of_range);
                const typename design_type::numbers_type& n       = m_design.numbers();
                const std::size_t                         frames  = in.size() / Ch;
                const level_type                          ceiling = n.ceiling;
                const value_type* const                   from    = in.data();
                value_type* const                         to      = out.data();
                for (std::size_t f = 0; f < frames; ++f) {
                    const level_type y = step(st, detail::frame_level<value_type, Ch>(from + f * Ch), n);
                    const level_type g = std::exp(-y);
                    value_type*      d = st.line.data() + st.at * Ch;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        const value_type v = d[c] * g;
                        d[c]               = from[f * Ch + c];
                        to[f * Ch + c]     = v > ceiling ? ceiling : (v < -ceiling ? -ceiling : v);
                    }
                    st.at = st.at + 1 == k_ahead_32 ? 0 : st.at + 1;
                    ++st.frame;
                }
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// The look-ahead, exactly.
            stage_latency latency() const noexcept { return {static_cast<float>(Lookahead), 0.0f}; }
            /// The line empties.
            std::size_t tail_frames() const noexcept { return Lookahead; }
            /// The same.
            std::size_t practical_tail_frames() const noexcept { return Lookahead; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            /// The next slot of a ring of the window's length.
            static constexpr std::uint32_t next(std::uint32_t i) noexcept {
                return i + 1 == static_cast<std::uint32_t>(k_window) ? 0 : i + 1;
            }

            /// One frame of level x through the window and the release: the reduction to apply to
            /// the frame `Lookahead` back.
            static level_type step(state& st, level_type x, const typename design_type::numbers_type& n) noexcept {
                using detail::k_limiter_grid;
                const level_type r = detail::smaller(detail::larger(x - n.threshold, level_type{0}),
                                                     static_cast<level_type>(detail::k_limiter_max_reduction));
                const auto       q = static_cast<std::uint32_t>(std::ceil(r * static_cast<level_type>(k_limiter_grid)));
                // Lemire's wedge: drop what has left the window, then what the new value outranks.
                while (st.count > 0 && st.frame - st.wedge[st.head].frame > k_ahead_32) {
                    st.head = next(st.head);
                    --st.count;
                }
                while (st.count > 0 && st.wedge[(st.head + st.count - 1) % k_window].value <= q) {
                    --st.count;
                }
                st.wedge[(st.head + st.count) % k_window] = {st.frame, q};
                ++st.count;
                const std::uint32_t m = st.wedge[st.head].value;
                // The mean of the last L + 1 maxima, rounded up: exact, the sum below 2^32.
                st.sum                   = st.sum + m - st.maxima[st.slot];
                st.maxima[st.slot]       = m;
                st.slot                  = next(st.slot);
                const std::uint32_t a    = (st.sum + k_ahead_32) / static_cast<std::uint32_t>(k_window);
                const level_type    mean = static_cast<level_type>(a) * k_step; // exact: a power of two
                st.released =
                    detail::to_rest(detail::larger(mean, n.release * st.released + n.release_complement * mean));
                return st.released;
            }

            design_type m_design;
        };

        template <std::size_t Ch, std::size_t Lookahead>
        using limiter = basic_limiter<float, Ch, Lookahead>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Lookahead>
        using limiter_f64 = basic_limiter<double, Ch, Lookahead>; ///< in double
        template <std::size_t Ch, std::size_t Lookahead>
        using limiter_q31 = basic_limiter<q31, Ch, Lookahead>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch, std::size_t Lookahead>
        struct is_stage<basic_limiter<S, Ch, Lookahead>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_LIMITER_H
