/// @file biquad.h
/// @brief `biquad`: a cascade of biquad sections, run a block at a time, from a design that
/// cannot fail to apply (PLAN.md 9.2); `butterworth` is the cascade of an even order.
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                     |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous)                             |
/// | Configured | a `biquad_design<S, Sections>`, held by value (an order-16 Butterworth in    |
/// |            | double is 320 bytes of coefficients); `set_design()` between calls            |
/// | State      | per channel z1 then z2 of every section, 2 x Sections values; rests at zero |
/// | Workspace  | none                                                                         |
/// | Arithmetic | the floating kernel (`detail/biquad_kernel.h`): transposed direct form II,  |
/// |            | states and products in S, the sections in order, a block at a time          |
/// | Redesign   | a new design applies from the next call to the state as it is: a step under |
/// |            | a live history, a small transient; smoothing it is not this stage's (9.2)    |
/// | Latency    | not exact: the design's `{nominal, deviation}` (`biquad_design.h`), so a    |
/// |            | `parallel` arm with it is aligned by its caller (D43)                        |
/// | Tail       | the design's exact and practical tails and its residue bound                 |
/// | Subnormals | the host's: with `CATENAE_FLUSH_TO_ZERO` on (the default) the host flushes   |
/// |            | them on the thread that runs the stage, and at audit `run` checks it against |
/// |            | the build's statement where a control bit governs S (AOU-008,                |
/// |            | `check::flush_to_zero`); with it off, or where no bit governs S, the state   |
/// |            | decays to a residue of a few subnormals and, on x86, silence runs slower     |
/// | Limits     | a non-finite input poisons the state until it is reset (PLAN.md 3.4); under |
/// |            | flush-to-zero some designs hold a limit cycle a few times S's smallest normal |
/// |            | value after their tail (DspTap's honest limit), inside the residue bound     |
/// | Formats    | float and double; q31 declares its contract and its arithmetic arrives with  |
/// |            | its section scaling at C6 (D47)                                              |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, where a fused multiply-add moves the last bits within the float-double |
/// |            | bound                                                                         |
/// | Rationale  | D24, D37, D43, D47, D49, PLAN.md 3.3, 9.2                                     |
///
/// @req{CAT-BIQUAD-001} From reset each channel's output is its input through the design's
/// sections in order, by the kernel's difference equation; every channel alike, in place or
/// not, and the same bits for any chunking.
/// @req{CAT-BIQUAD-002} A design set between calls applies from the next call to the state as
/// it stands; the output is the same as a stage of the new design run from that state.
/// @req{CAT-BIQUAD-003} The stage reports its design's latency, tails and residue, and has no
/// exact latency.
/// @req{CAT-BIQUAD-004} At the audit level `run` checks that the calling thread's
/// flush-to-zero setting is the build's (`check::flush_to_zero`), where a control bit governs
/// S; at `boundary` it reads nothing.
/// @req{CAT-BIQUAD-005} The float stage follows the double one within the bound its battery
/// states, and a float Butterworth's output after its exact tail from full scale is within its
/// residue bound under the battery's flush-to-zero setting.
/// @req{CAT-BIQUAD-006} In q31 the contract is instantiated; a run does not compile until the
/// fixed-point recursion arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_BIQUAD_H
#define CATENAE_BIQUAD_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/biquad_design.h"
#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/biquad_kernel.h"
#include "catenae/detail/convert.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief A cascade of `Sections` biquads over a `port<S, Ch>`, from a design.
        template <typename S, std::size_t Ch, std::size_t Sections = 1>
        class basic_biquad {
            static_assert(Ch >= 1, "catenae: a biquad has at least one channel");

          public:
            using input_port  = port<S, Ch>;                ///< what it reads
            using output_port = port<S, Ch>;                ///< what it writes
            using design_type = biquad_design<S, Sections>; ///< what it runs

          private:
            using value_type = storage_t<S>;

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = false; ///< a recursion delays each frequency its own way
            static constexpr bool        rests_at_zero     = true;  ///< an empty history

            /// Every channel's history: z1 then z2 of each section, channel after channel.
            struct state {
                std::array<value_type, Ch * 2 * Sections> z; ///< the histories
            };
            using workspace = no_workspace; ///< nothing

            /// A cascade running `design`.
            explicit constexpr basic_biquad(const design_type& design) noexcept
                : m_design(design) {}

            /// The design it runs.
            constexpr const design_type& design() const noexcept { return m_design; }
            /// A new design, applied from the next run() to the state as it stands. Between calls
            /// only; it cannot fail.
            constexpr void set_design(const design_type& design) noexcept { m_design = design; }

            /// Every frame through the sections.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point biquad needs its section scaling, which arrives with its "
                              "profile at C6 (D47)");
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(out.size() >= in.size(), check::index_out_of_range);
                    detail::check_audit(detail::flush_to_zero_as_stated<value_type>(), check::flush_to_zero);
                }
                const std::size_t frames = in.size() / Ch;
                detail::biquad_cascade_block<value_type, Sections, Ch>(m_design.table(), st.z.data(), in.data(), frames,
                                                                       out.data());
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// The design's `{nominal, deviation}`.
            stage_latency latency() const noexcept { return m_design.latency(); }
            /// The design's exact tail.
            std::size_t tail_frames() const noexcept { return m_design.tail_frames(); }
            /// The design's practical tail.
            std::size_t practical_tail_frames() const noexcept { return m_design.practical_tail_frames(); }
            /// The design's residue bound.
            float flush_residue() const noexcept { return m_design.flush_residue(); }

          private:
            design_type m_design;
        };

        namespace detail {
            /// The sections of an even-order Butterworth.
            template <std::size_t Order>
            struct butterworth_sections_of {
                static_assert(Order >= 2 && Order <= 16 && Order % 2 == 0,
                              "catenae: a Butterworth is of even order 2 to 16");
                static constexpr std::size_t k_value = Order / 2;
            };
        } // namespace detail

        template <std::size_t Ch, std::size_t Sections = 1>
        using biquad = basic_biquad<float, Ch, Sections>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Sections = 1>
        using biquad_f64 = basic_biquad<double, Ch, Sections>; ///< in double
        template <std::size_t Ch, std::size_t Sections = 1>
        using biquad_q31 = basic_biquad<q31, Ch, Sections>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch, std::size_t Order>
        using basic_butterworth = basic_biquad<S, Ch, detail::butterworth_sections_of<Order>::k_value>; ///< of order N
        template <std::size_t Ch, std::size_t Order>
        using butterworth = basic_butterworth<float, Ch, Order>; ///< in float (D24)
        template <std::size_t Ch, std::size_t Order>
        using butterworth_f64 = basic_butterworth<double, Ch, Order>; ///< in double

        template <typename S, std::size_t Ch, std::size_t Sections>
        struct is_stage<basic_biquad<S, Ch, Sections>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_BIQUAD_H
