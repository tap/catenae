/// @file ppm_meter.h
/// @brief `ppm_meter`: a peak programme meter, each channel's quasi-peak reading at every hop,
/// with the ballistics of its design (C5, D11, IEC 60268-10).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Reads      | `port<S, Ch>`; an analyzer, put in a chain by `tap<Ch, ppm_meter<...>>`      |
/// | Detector   | per channel and frame, y += attack (\|x\| - y) where \|x\| > y, else y x     |
/// |            | release (Zolzer, "Digital Audio Signal Processing", 2008, ch. 7); a y below  |
/// |            | S's smallest normal value is set to 0, so silence holds no subnormal         |
/// | Configured | a `ppm_design<S>`: `ppm_design<S>::make(ebu_ppm(rate))` for the EBU's Type   |
/// |            | IIb meter, or a caller's times; `set_design()` between calls                 |
/// | Results    | every `Hop` frames, each channel's y at the hop's last frame; `window_frames` |
/// |            | is `Hop`, the ballistics' memory being their own                             |
/// | Result     | `std::array<S, Ch>`                                                          |
/// | Reading    | `dbfs(y)`: 20 log10 y, off the audio path; silence reads minus infinity      |
/// | State      | y per channel and the latest result; at rest all zero (`rests_at_zero`)      |
/// | Formats    | float and double; q31 declares its contract, and its recursion is new design |
/// |            | that arrives with its profile at C6 (D47)                                    |
/// | Limits     | Zolzer's detector meets EBU Tech 3205-E's burst table (its Table 2) at 100,  |
/// |            | 10 and 5 ms, and reads low for shorter bursts: at 48 kHz a 1.5 ms burst      |
/// |            | reads -11.1 dB (the table: -9 +- 1) and 0.5 ms -19.3 (-17 +- 2); a test pins |
/// |            | the five rows. A steady sine reads below its peak by the ripple between its  |
/// |            | peaks: 0.21 dB for 1 kHz at 48 kHz. A NaN holds its channel's y until reset  |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, and a fused multiply-add there can move y by an ULP                   |
/// | Rationale  | D11, D12, D24, D47, D49                                                      |
///
/// @req{CAT-PPM-001} From reset each channel's y follows the card's detector frame by frame,
/// bit for bit, and after every `Hop` frames the result is each channel's y.
/// @req{CAT-PPM-002} With `ebu_ppm` at 48 kHz, isolated 5 kHz bursts of 100, 10, 5, 1.5 and
/// 0.5 ms read the card's numbers below the steady reading, within 0.05 dB, a steady 1 kHz sine
/// reads 0.21 dB below its peak, and a steady tone removed falls 24 dB in 2.8 s; a y below the
/// smallest normal value is 0; a NaN holds.
/// @req{CAT-PPM-003} In q31 the contract is instantiated; `analyze` does not compile until its
/// profile arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PPM_METER_H
#define CATENAE_PPM_METER_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/meter.h"
#include "catenae/port.h"
#include "catenae/ppm_design.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief A peak programme meter over a `port<S, Ch>`, a reading every `Hop` frames.
        template <typename S, std::size_t Ch, std::size_t Hop>
        class basic_ppm_meter {
            static_assert(Hop >= 1 && Hop <= std::size_t{1} << 31u, "catenae: a meter's hop is 1 to 2^31 frames");

          public:
            using input_port                           = port<S, Ch>;                      ///< what it reads
            using value_type                           = storage_t<S>;                     ///< a sample
            using design_type                          = ppm_design<S>;                    ///< its ballistics
            using level_type                           = typename design_type::value_type; ///< what y is held in
            using result_type                          = std::array<level_type, Ch>;       ///< each channel's reading
            using reading_type                         = detail::reading_t<S>;             ///< what a reading is in
            static constexpr std::size_t hop_frames    = Hop;  ///< a result every Hop frames
            static constexpr std::size_t window_frames = Hop;  ///< the ballistics hold the rest
            static constexpr bool        rests_at_zero = true; ///< all zero is at rest

            /// Each channel's detector, and the latest result.
            struct state {
                result_type   level;  ///< y, per channel
                result_type   latest; ///< the latest result
                std::uint32_t frames; ///< frames into the hop
                std::uint32_t ready;  ///< results since reset, wrapping
            };

            /// A meter with `design`'s ballistics.
            explicit constexpr basic_ppm_meter(const design_type& design) noexcept
                : m_design(design) {}

            /// The design.
            constexpr const design_type& design() const noexcept { return m_design; }
            /// A new design, from the next frame on. Between calls only.
            constexpr void set_design(const design_type& design) noexcept { m_design = design; }

            /// Each frame through each channel's detector; a result at each hop's end.
            void analyze(state& st, span<const value_type> in) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point peak programme meter is new design and arrives with its "
                              "profile at C6 (D47)");
                const level_type        attack  = m_design.numbers().attack;
                const level_type        release = m_design.numbers().release;
                const std::size_t       frames  = in.size() / Ch;
                const value_type* const x       = in.data();
                std::size_t             done    = 0;
                while (done < frames) {
                    const std::size_t n = frames - done < Hop - st.frames ? frames - done : Hop - st.frames;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        level_type y = st.level[c];
                        for (std::size_t f = done; f < done + n; ++f) {
                            const level_type a = std::fabs(x[f * Ch + c]);
                            if (a > y) {
                                y += attack * (a - y);
                            }
                            else if (std::isnan(a)) {
                                y = a;
                            }
                            else {
                                y *= release;
                            }
                            // Either branch can leave a subnormal where nothing flushes it (a
                            // software double, or a host without flush-to-zero): it reads as 0.
                            y = y < std::numeric_limits<level_type>::min() ? level_type{} : y;
                        }
                        st.level[c] = y;
                    }
                    done += n;
                    st.frames += static_cast<std::uint32_t>(n);
                    if (st.frames == Hop) {
                        st.latest = st.level;
                        st.frames = 0;
                        ++st.ready;
                    }
                }
            }

            /// The latest result.
            const result_type& result(const state& st) const noexcept { return st.latest; }
            /// Results since reset, a count that wraps.
            std::uint32_t results_ready(const state& st) const noexcept { return st.ready; }

            /// 20 log10 of a reading. Off the audio path.
            static reading_type dbfs(level_type y) noexcept {
                return detail::amplitude_db(static_cast<reading_type>(y));
            }

          private:
            design_type m_design;
        };

        template <std::size_t Ch, std::size_t Hop>
        using ppm_meter = basic_ppm_meter<float, Ch, Hop>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Hop>
        using ppm_meter_f64 = basic_ppm_meter<double, Ch, Hop>; ///< in double
        template <std::size_t Ch, std::size_t Hop>
        using ppm_meter_q31 = basic_ppm_meter<q31, Ch, Hop>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch, std::size_t Hop>
        struct is_analyzer<basic_ppm_meter<S, Ch, Hop>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PPM_METER_H
