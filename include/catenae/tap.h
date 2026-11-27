/// @file tap.h
/// @brief `tap<Ch, Analyzer>`: an analyzer in a chain, its input passed through unchanged
/// (D11, PLAN.md 6.2).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | the analyzer's input port, in and out: interleaved audio or a spectrum, with |
/// |            | `Ch` channels                                                               |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)          |
/// | Configured | the analyzer, a trivially copyable constant; `analyzer()` reaches it         |
/// | State      | the analyzer's, reached in a chain by `stage_state<I>(state)`; `reset` is    |
/// |            | the analyzer's, or zero where the analyzer rests at zero                     |
/// | Workspace  | none                                                                         |
/// | Latency    | exactly 0                                                                    |
/// | Tail       | 0 frames                                                                     |
/// | Results    | `result(state)` and `results_ready(state)`, the analyzer's, read on the      |
/// |            | chain's thread between calls; they leave it through a mailbox (D44)          |
/// | Method     | each call's input is analyzed, then copied to `out` unless they are one      |
/// |            | buffer, so the output is the input bit for bit                               |
/// | Formats    | the analyzer's                                                               |
/// | Rationale  | D11, D44, D49                                                                |
///
/// @req{CAT-TAP-001} A tap's output is its input, bit for bit, in place or not, for any
/// chunking; it is 1:1 with no latency and no tail.
/// @req{CAT-TAP-002} A tap's results are its analyzer's over the same input, whatever the
/// chunking: `result` and `results_ready` read the analyzer's from the tap's state, which its
/// reset brings to the analyzer's rest.
/// @req{CAT-TAP-003} A tap whose channel count is not its analyzer's port's, or over a type that
/// is not an analyzer, does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TAP_H
#define CATENAE_TAP_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "catenae/analyzer_traits.h"
#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief An analyzer over a `Ch`-channel port in a chain: the input passes through.
        template <std::size_t Ch, typename Analyzer>
        class tap {
            using traits = analyzer_traits<Analyzer>;
            static_assert(traits::opted_in, "catenae: a tap holds an analyzer: one that specialises is_analyzer");
            using port_type = typename traits::input_port;
            static_assert(port_type::channels == Ch, "catenae: a tap's channel count is its analyzer's port's");
            static_assert(
                std::is_same<typename port_type::input_view, span<const typename port_type::value_type>>::value,
                "catenae: a tap passes one span through: interleaved audio or a spectrum");
            using value_type = typename port_type::value_type;

          public:
            using input_port    = port_type;                    ///< the analyzer's
            using output_port   = port_type;                    ///< the same
            using analyzer_type = Analyzer;                     ///< what it holds
            using result_type   = typename traits::result_type; ///< the analyzer's
            using state         = typename traits::state;       ///< the analyzer's
            using workspace     = no_workspace;                 ///< nothing

            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};

            /// A tap over `analyzer`.
            explicit constexpr tap(const Analyzer& analyzer) noexcept
                : m_analyzer(analyzer) {}
            /// A tap over a default analyzer, for one with nothing to configure.
            constexpr tap() noexcept
                : m_analyzer(Analyzer{}) {}

            /// The analyzer.
            constexpr const Analyzer& analyzer() const noexcept { return m_analyzer; }
            /// The analyzer, to change between calls.
            constexpr Analyzer& analyzer() noexcept { return m_analyzer; }

            /// At rest as the analyzer is: its own reset, or zero where it rests at zero.
            void reset(state& st) const noexcept {
                if constexpr (traits::rests_at_zero) {
                    st = state{};
                }
                else {
                    m_analyzer.reset(st);
                }
            }

            /// The input analyzed, then passed through.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                m_analyzer.analyze(st, in);
                if (in.data() != out.data() && !in.empty()) {
                    std::memcpy(out.data(), in.data(), in.size() * sizeof(value_type));
                }
                return in.size() / port_type::values_per_frame;
            }

            /// The analyzer's latest result.
            decltype(auto) result(const state& st) const noexcept { return m_analyzer.result(st); }
            /// The analyzer's results since reset, a count that wraps.
            std::uint32_t results_ready(const state& st) const noexcept { return m_analyzer.results_ready(st); }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            Analyzer m_analyzer;
        };

        template <std::size_t Ch, typename Analyzer>
        struct is_stage<tap<Ch, Analyzer>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_TAP_H
