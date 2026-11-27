/// @file fir.h
/// @brief `fir`: a direct-form FIR over a `port<S, Ch>`, linear phase by its type; `fir_any`,
/// any FIR, its latency its design's (PLAN.md 9.7).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                     |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous)                             |
/// | Configured | a `fir_design<S, Taps, Phase>`: its taps by value while they fit 512 bytes   |
/// |            | (128 in float, 256 in q15, at linear phase), else a view of the caller's   |
/// |            | table, so the stage `holds_view` (D54); `set_design()` between calls        |
/// | State      | per channel a line of 2 Taps values, and one write index; rests at zero    |
/// | Workspace  | none                                                                         |
/// | Arithmetic | the kernel (`detail/fir_kernel.h`): four partial sums in float and double   |
/// |            | (D36); in q15, q31 and Q4.27 `sample_traits`' exact or Q45 sum and its one  |
/// |            | rounding, saturated                                                          |
/// | Redesign   | a new design applies from the next call to the history as it is             |
/// | Latency    | `fir`: exactly (Taps - 1) / 2 frames, a constant of the type (half a frame |
/// |            | at an even count), so `parallel` aligns it by type; `fir_any`: not exact,   |
/// |            | its design's `{nominal, deviation}`                                          |
/// | Tail       | Taps - 1 frames, exact and practical: then the history is silence; no residue |
/// | Formats    | float, double, q15, q31, Q4.27 (Q4.27's arithmetic is q31's: the kernel does |
/// |            | not know the binary point, so its floor is q31's less 24 dB of headroom)    |
/// | Limits     | a non-finite input poisons Taps outputs; the history then holds it no longer |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, where a fused multiply-add moves the last bits (D36: a float FIR has |
/// |            | a floor and a tolerance, not a bit pin across targets)                       |
/// | Rationale  | D24, D29, D36, D37, D43, D47, D49, D54, PLAN.md 3.3, 9.7                    |
///
/// @req{CAT-FIR-001} From reset each channel's output is its input through the design's taps
/// by the kernel's sum; every channel alike, in place or not, the same bits for any chunking,
/// in every format.
/// @req{CAT-FIR-002} `fir` has exact latency (Taps - 1) / 2: a linear-phase design delays an
/// impulse by it, and `latency()` is it with no deviation; `fir_any` has none, and reports its
/// design's.
/// @req{CAT-FIR-003} After Taps - 1 zero frames the output is silence, exactly: both tails are
/// Taps - 1 and the residue 0.
/// @req{CAT-FIR-004} A design set between calls applies from the next call to the history as it
/// stands; the output is that of a stage of the new design run from the same history.
/// @req{CAT-FIR-005} The float stage follows the double one within its stated floor, and each
/// fixed format the double one within its stated floor in LSBs.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIR_H
#define CATENAE_FIR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/fir_kernel.h"
#include "catenae/fir_design.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief A direct-form FIR of `Taps` taps over a `port<S, Ch>`, from a design.
        template <typename S, std::size_t Ch, std::size_t Taps, fir_phase Phase = fir_phase::linear>
        class basic_fir {
            static_assert(Ch >= 1, "catenae: an FIR has at least one channel");

          public:
            using input_port  = port<S, Ch>;                ///< what it reads
            using output_port = port<S, Ch>;                ///< what it writes
            using design_type = fir_design<S, Taps, Phase>; ///< what it runs

          private:
            using value_type = storage_t<S>;

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = Phase == fir_phase::linear; ///< linear phase only
            /// (Taps - 1) / 2 frames for linear phase, reduced; 0 otherwise.
            static constexpr ratio exact_latency =
                has_exact_latency ? reduce(ratio{static_cast<std::uint32_t>(Taps - 1), 2}) : ratio{0, 1};
            static constexpr bool rests_at_zero = true; ///< an empty history

            /// Every channel's line, channel after channel, and the slot the next frame takes.
            struct state {
                std::array<value_type, Ch * 2 * Taps> lines; ///< 2 Taps values per channel
                std::uint32_t                         write; ///< from 0 to Taps - 1
            };
            using workspace = no_workspace; ///< nothing

            /// An FIR running `design`.
            explicit constexpr basic_fir(const design_type& design) noexcept
                : m_design(design) {}

            /// The design it runs.
            constexpr const design_type& design() const noexcept { return m_design; }
            /// A new design, applied from the next run() to the history as it stands. Between
            /// calls only; it cannot fail.
            constexpr void set_design(const design_type& design) noexcept { m_design = design; }

            /// Every frame through the taps.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(out.size() >= in.size(), check::index_out_of_range);
                    detail::check_audit(st.write < Taps, check::invariant);
                }
                const std::size_t frames = in.size() / Ch;
                detail::fir_block<value_type, Taps, Ch>(m_design.data(), st.lines.data(), st.write, in.data(), frames,
                                                        out.data());
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// The design's `{nominal, deviation}`: (Taps - 1) / 2 and 0 for linear phase.
            stage_latency latency() const noexcept { return m_design.latency(); }
            /// Taps - 1.
            std::size_t tail_frames() const noexcept { return Taps - 1; }
            /// Taps - 1.
            std::size_t practical_tail_frames() const noexcept { return Taps - 1; }
            /// None: the history is silence after the tail.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            design_type m_design;
        };

        template <std::size_t Ch, std::size_t Taps>
        using fir = basic_fir<float, Ch, Taps>; ///< linear phase, in float (D24)
        template <std::size_t Ch, std::size_t Taps>
        using fir_f64 = basic_fir<double, Ch, Taps>; ///< linear phase, in double
        template <std::size_t Ch, std::size_t Taps>
        using fir_q15 = basic_fir<q15, Ch, Taps>; ///< linear phase, in Q15 with Q1.14 taps
        template <std::size_t Ch, std::size_t Taps>
        using fir_q31 = basic_fir<q31, Ch, Taps>; ///< linear phase, in Q31 with Q1.30 taps
        template <std::size_t Ch, std::size_t Taps>
        using fir_q4_27 = basic_fir<q4_27, Ch, Taps>; ///< linear phase, in Q4.27 with Q1.30 taps

        template <typename S, std::size_t Ch, std::size_t Taps>
        using basic_fir_any = basic_fir<S, Ch, Taps, fir_phase::any>; ///< any FIR
        template <std::size_t Ch, std::size_t Taps>
        using fir_any = basic_fir_any<float, Ch, Taps>; ///< any FIR, in float (D24)
        template <std::size_t Ch, std::size_t Taps>
        using fir_any_f64 = basic_fir_any<double, Ch, Taps>; ///< any FIR, in double
        template <std::size_t Ch, std::size_t Taps>
        using fir_any_q15 = basic_fir_any<q15, Ch, Taps>; ///< any FIR, in Q15
        template <std::size_t Ch, std::size_t Taps>
        using fir_any_q31 = basic_fir_any<q31, Ch, Taps>; ///< any FIR, in Q31
        template <std::size_t Ch, std::size_t Taps>
        using fir_any_q4_27 = basic_fir_any<q4_27, Ch, Taps>; ///< any FIR, in Q4.27

        template <typename S, std::size_t Ch, std::size_t Taps, fir_phase Phase>
        struct is_stage<basic_fir<S, Ch, Taps, Phase>> : std::true_type {};
        /// An FIR whose taps are too many to hold by value views the caller's.
        template <typename S, std::size_t Ch, std::size_t Taps, fir_phase Phase>
        struct holds_view<basic_fir<S, Ch, Taps, Phase>>
            : std::bool_constant<!fir_design<S, Taps, Phase>::holds_by_value> {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FIR_H
