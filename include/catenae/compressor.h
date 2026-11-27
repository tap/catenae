/// @file compressor.h
/// @brief `compressor`: a feed-forward compressor with a soft knee, its channels linked by
/// their largest level, from a design (PLAN.md 2: dynamics without a side chain).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                     |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous)                             |
/// | Configured | a `compressor_design<S>` by value (36 bytes in float); `set_design()` between |
/// |            | calls                                                                        |
/// | State      | the detector's two values, y1 and y, nepers of reduction; rests at zero (no  |
/// |            | reduction)                                                                   |
/// | Workspace  | none                                                                         |
/// | Level      | per frame, the log of the largest magnitude of its channels, at least 2^-32  |
/// |            | (-192.7 dB): one level, so every channel takes the same gain                 |
/// | Gain       | the reduction r(x) of the gain computer (`compressor_design`'s card), then  |
/// |            | the smooth decoupled peak detector: y1 = max(r, aR y1 + (1 - aR) r),        |
/// |            | y = aA y + (1 - aA) y1, each value below 2^-64 nepers taken as 0; the frame |
/// |            | times exp(M - y) (Giannoulis, Massberg and Reiss, JAES 60(6), 2012)          |
/// | Arithmetic | in S, with the runtime's `log` and `exp` for S; the stage's own float sums   |
/// |            | stay in float, and where the FPU has single precision only (M4F, M33)        |
/// |            | newlib's `logf` and `expf` do too; where it has double (the M55) they take   |
/// |            | the double path in hardware; no image links a software double               |
/// | Latency    | exactly 0: the gain acts on the frame its level is taken from                 |
/// | Tail       | 0 frames: silence in is silence out                                          |
/// | Limits     | a NaN sample is passed through and not read as a level; an infinite one holds |
/// |            | the gain at 0 until the stage is reset (PLAN.md 3.4: `sanitize` first)       |
/// | Formats    | float and double; q31 declares its contract and its arithmetic arrives with  |
/// |            | its profile at C6 (D47)                                                      |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, where a fused multiply-add moves the detector's last bits             |
/// | Rationale  | D12, D24, D37, D47, D49, PLAN.md 2                                           |
///
/// @req{CAT-COMPRESSOR-001} From reset each frame is multiplied by exp(M - y), y the detector's
/// value after the frame's reduction, by the card's equations in order; every channel takes the
/// same gain, in place or not, and the output is the same bits for any chunking.
/// @req{CAT-COMPRESSOR-002} Settled on a steady level, its output level is the gain computer's
/// static curve: unity below the knee, the ratio above it, the paper's quadratic across it, and
/// the make-up gain added.
/// @req{CAT-COMPRESSOR-003} Its exact latency is 0 and its tail 0; silence after any input
/// brings its state to rest, exactly 0, in finite time.
/// @req{CAT-COMPRESSOR-004} A design set between calls applies from the next call to the state
/// as it stands.
/// @req{CAT-COMPRESSOR-005} The float stage follows the double one within the bound its battery
/// states; in q31 the contract is instantiated and a run does not compile until C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_COMPRESSOR_H
#define CATENAE_COMPRESSOR_H

#include <cmath>
#include <cstddef>
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

        /// @brief A feed-forward compressor over a `port<S, Ch>`, from a design.
        template <typename S, std::size_t Ch>
        class basic_compressor {
            static_assert(Ch >= 1, "catenae: a compressor has at least one channel");

          public:
            using input_port  = port<S, Ch>;          ///< what it reads
            using output_port = port<S, Ch>;          ///< what it writes
            using design_type = compressor_design<S>; ///< what it runs

          private:
            using value_type = storage_t<S>;
            using level_type = typename design_type::value_type;

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< no reduction

            /// The detector.
            struct state {
                level_type held;    ///< y1, the release stage
                level_type reduced; ///< y, the reduction applied, nepers
            };
            using workspace = no_workspace; ///< nothing

            /// A compressor running `design`.
            explicit constexpr basic_compressor(const design_type& design) noexcept
                : m_design(design) {}

            /// The design it runs.
            constexpr const design_type& design() const noexcept { return m_design; }
            /// A new design, applied from the next run() to the state as it stands. Between calls
            /// only; it cannot fail.
            constexpr void set_design(const design_type& design) noexcept { m_design = design; }

            /// Every frame times its gain.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point compressor is new design and arrives with its profile at C6 "
                              "(D47)");
                detail::check_audit(out.size() >= in.size(), check::index_out_of_range);
                const typename design_type::numbers_type& n      = m_design.numbers();
                const std::size_t                         frames = in.size() / Ch;
                level_type                                held   = st.held;    // locals: a store through
                level_type                                y      = st.reduced; // `out` may alias the state
                const value_type* const                   from   = in.data();
                value_type* const                         to     = out.data();
                for (std::size_t f = 0; f < frames; ++f) {
                    const level_type x = detail::frame_level<value_type, Ch>(from + f * Ch);
                    detail::detect_rise(detail::compressor_reduction(x, n), held, y, n);
                    const level_type g = std::exp(n.makeup - y);
                    for (std::size_t c = 0; c < Ch; ++c) {
                        to[f * Ch + c] = from[f * Ch + c] * g;
                    }
                }
                st.held    = held;
                st.reduced = y;
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None: silence in is silence out.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            design_type m_design;
        };

        template <std::size_t Ch>
        using compressor = basic_compressor<float, Ch>; ///< in float (D24)
        template <std::size_t Ch>
        using compressor_f64 = basic_compressor<double, Ch>; ///< in double
        template <std::size_t Ch>
        using compressor_q31 = basic_compressor<q31, Ch>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch>
        struct is_stage<basic_compressor<S, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_COMPRESSOR_H
