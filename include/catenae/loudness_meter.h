/// @file loudness_meter.h
/// @brief `loudness_meter`: programme loudness after ITU-R BS.1770-4 and EBU Tech 3341, its
/// momentary, short-term and gated integrated powers at every hop (C5).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | `port<S, Ch>`; an analyzer, put in a chain by `tap<Ch, loudness_meter<...>>` |
/// | Filter     | each channel through its `k_weighting_design` by the biquad kernel's cascade, |
/// |            | as a `biquad<Ch, 2>` stage runs it; its squares summed in S per hop from 0   |
/// | Steps      | BS.1770-4's 400 ms blocks overlap by 75 %: a step of 100 ms, `HopsPerStep`   |
/// |            | hops of `Hop` frames; `make(rate)` takes only a rate whose 100 ms, rounded to |
/// |            | a frame, is `Hop x HopsPerStep`                                              |
/// | Results    | every hop, three mean squares of the K-weighted signal, each channel weighed |
/// |            | by its G and summed (eq. 2): `momentary` over the last 4 steps, `short_term` |
/// |            | over the last 30 (3 s), each the hops' sums oldest first from 0 over the     |
/// |            | window's frames, frames before reset silence; `integrated` the gated mean of |
/// |            | every whole block since reset (eq. 5 to 7). Tech 3341's "EBU mode", unsmoothed; |
/// |            | a hop of 20 ms (`HopsPerStep` 5) reads its tests 13 and 14 exactly          |
/// | Gating     | at each step's end from the fourth, the last 4 steps' block, l = -0.691 + 10 |
/// |            | log10 of its power: above -70 LUFS it counts in a histogram of 1,000 bins of |
/// |            | 0.1 LU, bin b the blocks nearest -70 + b / 10 LUFS, the top bin all above. The |
/// |            | integrated power is the mean, each block at its bin's centre, of the bins    |
/// |            | above the absolute-gated mean's loudness less 10 LU; 0 until a block counts  |
/// | Reading    | `lufs(power)`: -0.691 + 10 log10 power, off the audio path (a compiled       |
/// |            | log10); silence reads minus infinity                                         |
/// | Weights    | each channel's G, BS.1770-4 Table 3: 1 for left, right and centre, 1.41 for  |
/// |            | each surround, 0 for an LFE; `make(rate)` weighs every channel 1             |
/// | State      | each channel's filter and hop, 3 s of hops' sums, the histogram's 1,000      |
/// |            | 32-bit counts, the latest result; at rest all zero (`rests_at_zero`). Reset  |
/// |            | starts all three again; a meter not fed is paused                            |
/// | Formats    | float and double; q31 declares its contract (a Q2.30 K-weighting table), and |
/// |            | its recursion is new design that arrives with its profile at C6 (D47)        |
/// | Limits     | each block read at its bin's centre: within 0.05 LU of BS.1770-4's exact     |
/// |            | mean, a block near the relative gate judged by its bin, and the bin chosen   |
/// |            | through the compiled log10, so a block a rounding from a bin's edge may fall |
/// |            | either side on another target. Above +29.95 LUFS a block counts as +29.9; a  |
/// |            | bin saturates at 2^32 - 1 blocks (13 years). A NaN holds its filter until    |
/// |            | reset: the windows read NaN, and no block counts. Subnormals are the host's  |
/// |            | flush-to-zero, stated and audited as `biquad`'s are                          |
/// | Cost       | per frame and channel two sections and a square; per hop the 3 s summed; per |
/// |            | step a log10 and two passes over the histogram                               |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, and a fused multiply-add there can move a result by an ULP            |
/// | Rationale  | D11, D12, D24, D47, D49                                                      |
///
/// @req{CAT-LOUDNESS-001} Each hop's momentary and short-term powers are the card's sums of the
/// K-weighted signal, bit for bit: over a `biquad<Ch, 2>` stage's output of the same design, in
/// calls of any size.
/// @req{CAT-LOUDNESS-002} At 48 kHz EBU Tech 3341's tests 1 to 6, 9 and 12, and 10, 11, 13 and
/// 14 at a hop of 20 ms, read within their 0.1 LU, and tests 1 and 2 at 44.1 and 96 kHz.
/// @req{CAT-LOUDNESS-003} The integrated power is the card's histogram over the meter's blocks,
/// within 0.05 LU of the exact gated mean: a steady block reads its bin's centre, -70 LUFS and
/// 10 LU below the absolute-gated mean gate, nothing counts until a block, and a block above the
/// top bin counts in it.
/// @req{CAT-LOUDNESS-004} `make()` rejects a rate whose 100 ms is not `Hop x HopsPerStep` frames
/// and a weight negative or not finite, with the card's statuses; `from_table` refuses such a
/// weight. In q31 the contract is instantiated; `analyze` does not compile until C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_LOUDNESS_METER_H
#define CATENAE_LOUDNESS_METER_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/biquad_design.h"
#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/biquad_kernel.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/meter.h"
#include "catenae/expected.h"
#include "catenae/loudness_design.h"
#include "catenae/port.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// A loudness meter's result: three mean squares of the K-weighted, channel-weighted
        /// signal; `lufs()` reads each.
        template <typename R>
        struct loudness_powers {
            R momentary;  ///< over the last 400 ms
            R short_term; ///< over the last 3 s
            R integrated; ///< since reset, gated
        };

        /// @brief Programme loudness over a `port<S, Ch>`, every `Hop` frames, its 100 ms step
        /// `HopsPerStep` hops.
        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t HopsPerStep>
        class basic_loudness_meter {
            static_assert(Hop >= 1 && HopsPerStep >= 1, "catenae: a loudness meter's hop and step are whole hops");
            static_assert(Hop <= (std::size_t{1} << 31u) / (30 * HopsPerStep),
                          "catenae: a loudness meter's 3 s is at most 2^31 frames");
            static constexpr std::size_t k_momentary_hops = 4 * HopsPerStep;  // 400 ms
            static constexpr std::size_t k_short_hops     = 30 * HopsPerStep; // 3 s

            /// What a filter's state and a hop's sum are held in: S when it floats; for q31 its
            /// storage and 64 bits, the contract C6's profile fills in.
            using filter_value = storage_t<S>;
            using energy_value = detail::meter_sum_t<S>;

          public:
            static constexpr std::size_t bins = 1000; ///< the histogram's bins of 0.1 LU, from -70 LUFS

            using input_port                           = port<S, Ch>;                   ///< what it reads
            using value_type                           = storage_t<S>;                  ///< a sample
            using design_type                          = biquad_design<S, 2>;           ///< its K-weighting
            using reading_type                         = detail::reading_t<S>;          ///< what a power is in
            using weights_type                         = std::array<reading_type, Ch>;  ///< each channel's G
            using result_type                          = loudness_powers<reading_type>; ///< the three powers
            static constexpr std::size_t hop_frames    = Hop;                           ///< a result every Hop frames
            static constexpr std::size_t step_frames   = Hop * HopsPerStep;             ///< 100 ms
            static constexpr std::size_t window_frames = Hop * k_short_hops;            ///< the short-term's 3 s
            static constexpr bool        rests_at_zero = true;                          ///< all zero is at rest

            /// Each channel's filter and hop, 3 s of hops, the histogram and the latest result.
            struct state {
                std::array<std::array<filter_value, 4>, Ch> filter; ///< each channel's z1[2], z2[2]
                std::array<energy_value, Ch>                energy; ///< each channel's hop so far
                std::array<reading_type, k_short_hops>      hops;   ///< each hop's weighted sum
                std::array<std::uint32_t, bins>             blocks; ///< the gated blocks, by bin
                result_type                                 latest; ///< the latest result
                std::uint32_t                               frames; ///< frames into the hop
                std::uint32_t                               slot;   ///< where the next hop goes
                std::uint32_t                               seen;   ///< hops since reset, up to 400 ms
                std::uint32_t                               phase;  ///< hops into the step
                std::uint32_t                               ready;  ///< results since reset, wrapping
            };

            /// The meter at `sample_rate_hz`, every channel weighed 1 (the card's statuses).
            /// Outside the audio path.
            static expected<basic_loudness_meter, status> make(double sample_rate_hz) noexcept {
                weights_type ones{};
                for (reading_type& g : ones) {
                    g = reading_type{1};
                }
                return make(sample_rate_hz, ones);
            }

            /// The meter at `sample_rate_hz` with each channel's weight (the card's statuses).
            /// Outside the audio path.
            static expected<basic_loudness_meter, status> make(double              sample_rate_hz,
                                                               const weights_type& weights) noexcept {
                const expected<design_type, status> design = k_weighting_design<S>::make(sample_rate_hz);
                if (!design) {
                    return unexpected(design.error());
                }
                if (std::llround(sample_rate_hz / 10.0) != static_cast<long long>(step_frames)) {
                    return unexpected(status::out_of_range);
                }
                if (!valid_weights(weights)) {
                    return unexpected(status::invalid_argument);
                }
                return basic_loudness_meter(*design, weights);
            }

            /// A meter from a K-weighting table and weights, checked: a weight that is negative
            /// or not finite does not initialise a constant, and at run time reaches the handler.
            /// The rate is the table's: no step is checked.
            static constexpr basic_loudness_meter from_table(const design_type&  k_weighting,
                                                             const weights_type& weights) noexcept {
                detail::check_always(valid_weights(weights), check::table_rejected);
                return basic_loudness_meter(k_weighting, weights);
            }

            /// The K-weighting.
            constexpr const design_type& k_weighting() const noexcept { return m_design; }
            /// Each channel's weight.
            constexpr const weights_type& weights() const noexcept { return m_weights; }

            /// Each frame through each channel's filter into the hop; the results at its end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point loudness meter's K-weighting is new design and arrives with "
                              "its profile at C6 (D47)");
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(detail::flush_to_zero_as_stated<value_type>(), check::flush_to_zero);
                }
                const typename design_type::table_type rows   = m_design.table();
                const std::size_t                      frames = in.size() / Ch;
                const value_type* const                x      = in.data();
                std::size_t                            done   = 0;
                while (done < frames) {
                    const std::size_t n = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        std::array<value_type, 4> z = st.filter[c];
                        value_type                e = st.energy[c];
                        for (std::size_t f = done; f < done + n; ++f) {
                            const value_type y =
                                detail::biquad_cascade_frame<value_type, 2>(rows, z.data(), x[f * Ch + c]);
                            e += y * y;
                        }
                        st.filter[c] = z;
                        st.energy[c] = e;
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

            /// -0.691 + 10 log10 power, in LUFS (BS.1770-4 eq. 2). Off the audio path.
            static reading_type lufs(reading_type power) noexcept {
                return static_cast<reading_type>(-0.691) + detail::power_db(power);
            }

          private:
            /// The power of bin 0's centre, -70 LUFS: 10^((-70 + 0.691) / 10).
            static constexpr reading_type k_floor_power = static_cast<reading_type>(1.1724653045822956e-07);
            /// A bin's power over the one below it, 0.1 LU: 10^0.01.
            static constexpr reading_type k_bin_ratio = static_cast<reading_type>(1.023292992280754);

            constexpr basic_loudness_meter(const design_type& k_weighting, const weights_type& weights) noexcept
                : m_design(k_weighting)
                , m_weights(weights) {}

            /// Whether every weight is finite and not negative (false for a NaN).
            static constexpr bool valid_weights(const weights_type& weights) noexcept {
                bool ok = true;
                for (const reading_type g : weights) {
                    ok = ok && g >= reading_type{0} && g <= std::numeric_limits<reading_type>::max();
                }
                return ok;
            }

            /// Where hop `h` of the window ending at the slot is, oldest first.
            static std::size_t at(std::size_t slot, std::size_t h) noexcept {
                return slot + h < k_short_hops ? slot + h : slot + h - k_short_hops;
            }

            /// The hop's weighted sum into the ring, the windows' powers, and at a step's end a
            /// block into the histogram.
            void close_hop(state& st) const noexcept {
                reading_type sum = reading_type{0};
                for (std::size_t c = 0; c < Ch; ++c) {
                    sum += m_weights[c] * static_cast<reading_type>(st.energy[c]);
                    st.energy[c] = energy_value{};
                }
                st.hops[st.slot]       = sum;
                st.slot                = st.slot + 1 == k_short_hops ? 0 : st.slot + 1;
                reading_type momentary = reading_type{0};
                for (std::size_t h = k_short_hops - k_momentary_hops; h < k_short_hops; ++h) {
                    momentary += st.hops[at(st.slot, h)];
                }
                reading_type short_term = reading_type{0};
                for (std::size_t h = 0; h < k_short_hops; ++h) {
                    short_term += st.hops[at(st.slot, h)];
                }
                st.latest.momentary  = momentary / static_cast<reading_type>(Hop * k_momentary_hops);
                st.latest.short_term = short_term / static_cast<reading_type>(Hop * k_short_hops);
                st.seen              = st.seen < k_momentary_hops ? st.seen + 1 : st.seen;
                st.phase             = st.phase + 1 == HopsPerStep ? 0 : st.phase + 1;
                if (st.phase == 0 && st.seen == k_momentary_hops) {
                    count_block(st, st.latest.momentary);
                    st.latest.integrated = integrated(st);
                }
                st.frames = 0;
                ++st.ready;
            }

            /// A block above the absolute gate into its bin.
            static void count_block(state& st, reading_type power) noexcept {
                const reading_type l = lufs(power);
                if (!(l > static_cast<reading_type>(-70))) {
                    return; // below the absolute gate, or a NaN
                }
                const reading_type position = (l + static_cast<reading_type>(70)) * static_cast<reading_type>(10);
                const std::size_t  bin      = position >= static_cast<reading_type>(bins - 1)
                                                  ? bins - 1
                                                  : static_cast<std::size_t>(std::lround(position));
                if (st.blocks[bin] != std::numeric_limits<std::uint32_t>::max()) {
                    ++st.blocks[bin];
                }
            }

            /// The gated mean over the histogram: the absolute-gated mean, then the mean of the
            /// bins whose centre is above it less 10 LU, each block at its bin's centre.
            static reading_type integrated(const state& st) noexcept {
                reading_type count = reading_type{0};
                reading_type sum   = reading_type{0};
                reading_type w     = reading_type{1}; // bin b's power over bin 0's
                for (std::size_t b = 0; b < bins; ++b) {
                    const auto n = static_cast<reading_type>(st.blocks[b]);
                    count += n;
                    sum += n * w;
                    w *= k_bin_ratio;
                }
                if (!(count > reading_type{0})) {
                    return reading_type{0};
                }
                // 10 LU below the absolute-gated mean, in bin 0's units: no bin above the mean is
                // below it, so the gated count is never 0.
                const reading_type gate  = sum / (count * static_cast<reading_type>(10));
                reading_type       kept  = reading_type{0};
                reading_type       total = reading_type{0};
                w                        = reading_type{1};
                for (std::size_t b = 0; b < bins; ++b) {
                    if (w > gate) {
                        const auto n = static_cast<reading_type>(st.blocks[b]);
                        kept += n;
                        total += n * w;
                    }
                    w *= k_bin_ratio;
                }
                return k_floor_power * (total / kept);
            }

            design_type  m_design;
            weights_type m_weights;
        };

        template <std::size_t Ch, std::size_t Hop, std::size_t HopsPerStep>
        using loudness_meter = basic_loudness_meter<float, Ch, Hop, HopsPerStep>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Hop, std::size_t HopsPerStep>
        using loudness_meter_f64 = basic_loudness_meter<double, Ch, Hop, HopsPerStep>; ///< in double
        template <std::size_t Ch, std::size_t Hop, std::size_t HopsPerStep>
        using loudness_meter_q31 =
            basic_loudness_meter<q31, Ch, Hop, HopsPerStep>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch, std::size_t Hop, std::size_t HopsPerStep>
        struct is_analyzer<basic_loudness_meter<S, Ch, Hop, HopsPerStep>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_LOUDNESS_METER_H
