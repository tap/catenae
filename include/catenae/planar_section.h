/// @file planar_section.h
/// @brief `planar_section<Ch, Stages...>`: mono stages run over each channel of an interleaved
/// port, the channels de-interleaved once a piece and re-interleaved after (D7, D42).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in, `port<S', Ch>` out: the mono chain's formats at Ch channels |
/// | Stages     | a chain of mono stages, `chain<Stages...>`: one channel in and one out, time- |
/// |            | domain audio; any ratio, any input group (a rate changer, a block stage)    |
/// | Arrival    | the mono chain's: ratio, input group, shortfall, lead, quantum; in place    |
/// |            | when the mono chain is                                                       |
/// | Configured | the mono chain, one configuration for every channel; `mono()` reaches it;   |
/// |            | a chain of stages, so exempt from 512 bytes as a chain is (D54)              |
/// | State      | Ch states of the mono chain, each reset by it                               |
/// | Workspace  | the mono chain's, shared by the channels in turn; a piece of `block` frames |
/// |            | of each channel's input and of its output                                    |
/// | Method     | in pieces of `block` (64) frames: each channel's samples copied out, the    |
/// |            | mono chain run over them on that channel's state, its output copied back    |
/// |            | interleaved; every channel writes the same count, as one configuration from |
/// |            | the same reset does                                                          |
/// | Latency    | the mono chain's; exact when it is                                           |
/// | Tail       | the mono chain's, and its residue                                            |
/// | Formats    | the mono chain's                                                             |
/// | Rationale  | D7, D35, D42, D49, D50, D54, PLAN.md 4                                       |
///
/// @req{CAT-PLANAR-001} Each channel's output is the mono chain's output over that channel alone,
/// from its own reset, bit for bit, in place or not, for any chunking; with stages whose kernels
/// do not depend on the channel count it is the Ch-channel stage's output bit for bit.
/// @req{CAT-PLANAR-002} Its ratio, input group, shortfall, lead, quantum, latency and tails are
/// the mono chain's, rate changers and block stages included, and it is in place exactly when the
/// mono chain is.
/// @req{CAT-PLANAR-003} A stage of more than one channel, or a port that is not time-domain
/// audio, does not compile as a mono stage.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PLANAR_SECTION_H
#define CATENAE_PLANAR_SECTION_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// Whether a port is one channel of interleaved time-domain audio.
            template <typename P>
            struct mono_audio : std::false_type {};
            template <typename S>
            struct mono_audio<port<S, 1, layout::interleaved>> : std::true_type {};
        } // namespace detail

        /// @brief Mono stages over each channel of an interleaved `Ch`-channel port: a chain of
        /// them, one state per channel, presented as one interleaved stage.
        template <std::size_t Ch, typename... Stages>
        class planar_section {
          public:
            using mono_type = chain<Stages...>; ///< what every channel runs

          private:
            using mono_traits = stage_traits<mono_type>;
            using mono_in     = typename mono_traits::input_port;
            using mono_out    = typename mono_traits::output_port;
            static_assert(Ch >= 1, "catenae: a planar_section has at least one channel");
            static_assert(detail::mono_audio<mono_in>::value && detail::mono_audio<mono_out>::value,
                          "catenae: a planar_section runs mono stages: one channel of time-domain audio in and out");

            using in_value                       = typename mono_in::value_type;
            using out_value                      = typename mono_out::value_type;
            static constexpr std::size_t k_piece = mono_type::block;
            static constexpr std::size_t k_most  = mono_type::max_outputs_for(k_piece);

          public:
            using input_port  = port<typename mono_in::format, Ch>;  ///< the mono input at Ch channels
            using output_port = port<typename mono_out::format, Ch>; ///< the mono output at Ch channels

            static constexpr ratio       rate_ratio        = mono_traits::rate_ratio;        ///< the mono chain's
            static constexpr std::size_t input_group       = mono_traits::input_group;       ///< the mono chain's
            static constexpr ratio       max_shortfall     = mono_traits::max_shortfall;     ///< the mono chain's
            static constexpr ratio       max_lead          = mono_traits::max_lead;          ///< the mono chain's
            static constexpr std::size_t output_quantum    = mono_traits::output_quantum;    ///< the mono chain's
            static constexpr bool        is_in_place       = mono_traits::is_in_place;       ///< the mono chain's
            static constexpr bool        has_exact_latency = mono_traits::has_exact_latency; ///< the mono chain's
            static constexpr ratio       exact_latency     = mono_traits::exact_latency;     ///< the mono chain's
            /// The frames of each channel copied out at a time.
            static constexpr std::size_t block = k_piece;

            /// What one instance remembers: each channel's state of the mono chain.
            struct state {
                std::array<state_of<mono_type>, Ch> channels; ///< channel after channel
            };
            /// What a call needs: the mono chain's workspace, and one piece of each channel.
            struct workspace {
                workspace_of<mono_type>            mono; ///< shared by the channels in turn
                std::array<in_value, Ch * k_piece> in;   ///< each channel's piece of input
                std::array<out_value, Ch * k_most> out;  ///< each channel's piece of output
            };

            /// The mono stages, in order.
            explicit constexpr planar_section(const Stages&... stages) noexcept
                : m_mono(stages...) {}
            /// The mono chain.
            explicit constexpr planar_section(const mono_type& mono) noexcept
                : m_mono(mono) {}

            /// What every channel runs.
            constexpr const mono_type& mono() const noexcept { return m_mono; }
            /// What every channel runs, to change a parameter between calls.
            constexpr mono_type& mono() noexcept { return m_mono; }

            /// Each channel's state, reset by the mono chain.
            void reset(state& st) const noexcept {
                for (state_of<mono_type>& channel : st.channels) {
                    m_mono.reset(channel);
                }
            }

            /// In pieces of `block` frames: each channel out, through the mono chain, back in.
            std::size_t run(state& st, workspace& ws, span<const in_value> in, span<out_value> out) const noexcept {
                const std::size_t frames = in.size() / Ch;
                std::size_t       done   = 0;
                std::size_t       made   = 0;
                while (done < frames) {
                    const std::size_t n = std::min(k_piece, frames - done);
                    for (std::size_t c = 0; c < Ch; ++c) {
                        in_value* const to = ws.in.data() + c * k_piece;
                        for (std::size_t f = 0; f < n; ++f) {
                            to[f] = in[(done + f) * Ch + c];
                        }
                    }
                    std::size_t written = 0;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        const std::size_t w =
                            m_mono.run(st.channels[c], ws.mono, span<const in_value>(ws.in.data() + c * k_piece, n),
                                       span<out_value>(ws.out.data() + c * k_most, k_most));
                        detail::check_audit(c == 0 || w == written, check::invariant);
                        written = w;
                    }
                    for (std::size_t c = 0; c < Ch; ++c) {
                        const out_value* const from = ws.out.data() + c * k_most;
                        for (std::size_t f = 0; f < written; ++f) {
                            out[(made + f) * Ch + c] = from[f];
                        }
                    }
                    done += n;
                    made += written;
                }
                return made;
            }

            /// The mono chain's count, from channel 0's state: every channel's is the same.
            constexpr std::size_t outputs_for(const state& st, std::size_t n) const noexcept {
                return m_mono.outputs_for(st.channels[0], n);
            }
            /// The mono chain's.
            constexpr std::size_t frames_needed(const state& st, std::size_t k) const noexcept {
                return m_mono.frames_needed(st.channels[0], k);
            }
            /// The mono chain's.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept {
                return mono_type::max_outputs_for(n);
            }
            /// The mono chain's.
            static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept {
                return mono_type::outputs_from_rest(n);
            }
            /// The mono chain's.
            stage_latency latency() const noexcept { return m_mono.latency(); }
            /// The mono chain's.
            std::size_t tail_frames() const noexcept { return m_mono.tail_frames(); }
            /// The mono chain's.
            std::size_t practical_tail_frames() const noexcept { return m_mono.practical_tail_frames(); }
            /// The mono chain's.
            float flush_residue() const noexcept { return m_mono.flush_residue(); }

          private:
            mono_type m_mono;
        };

        /// The planar_section over a mono chain: `planar<2>(a | b)`.
        template <std::size_t Ch, typename... Stages>
        constexpr planar_section<Ch, Stages...> planar(const chain<Stages...>& mono) noexcept {
            return planar_section<Ch, Stages...>(mono);
        }

        template <std::size_t Ch, typename... Stages>
        struct is_stage<planar_section<Ch, Stages...>> : std::true_type {};
        /// It holds a chain of stages, and is sized as one (D54).
        template <std::size_t Ch, typename... Stages>
        struct is_chain<planar_section<Ch, Stages...>> : std::true_type {};
        /// It holds a view when one of its stages does.
        template <std::size_t Ch, typename... Stages>
        struct holds_view<planar_section<Ch, Stages...>> : holds_view<chain<Stages...>> {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PLANAR_SECTION_H
