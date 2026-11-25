/// @file steady.h
/// @brief `steady<Config>`: a bursting 1:1 configuration made frame-synchronous by a FIFO (D35).
///
/// | Contract   |                                                                            |
/// |------------|----------------------------------------------------------------------------|
/// | Ports      | the configuration's                                                        |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)        |
/// | Configured | by the configuration it wraps; `inner()` reaches it                       |
/// | State      | the configuration's, a FIFO and two counts; at rest the FIFO holds the priming (`reset`) |
/// | Workspace  | the configuration's, and one burst of its output                         |
/// | Latency    | the configuration's plus the priming, `ceil(max_shortfall)` frames       |
/// | Tail       | the configuration's, the priming and the lead rounded up; its residue      |
/// | Formats    | the configuration's                                                        |
/// | Limits     | a configuration of ratio 1:1 only; it evens out the frames, not the work   |
/// | Method     | a FIFO primed with silence by the shortfall, with room for the lead and a burst |
/// | Rationale  | D35, D49, D50                                                              |
///
/// It runs the configuration 64 frames (`default_block`) at a time; the FIFO holds
/// `ceil(max_shortfall) + ceil(max_lead) + max_outputs_for(64)` frames.
///
/// @req{CAT-STEADY-001} `steady<Config>` presents input group 1, no shortfall, no lead and an
/// output quantum of 1: every call of n frames writes exactly n. A configuration whose ratio is
/// not 1:1 does not compile.
/// @req{CAT-STEADY-002} Its output is the configuration's output after `ceil(max_shortfall)`
/// frames of silence, bit for bit, in every pattern of calls: 191 frames for a 96-frame block
/// stage before a 128-frame one, 959 for 480 before 512, 10 for 7 before 5, 1 for a 2:1 that
/// writes on a pair's last frame before a 1:2, and 0 for one that writes on its first.
/// @req{CAT-STEADY-003} `exact_latency` is the configuration's plus that priming when the
/// configuration's is exact, and `latency()` adds the priming to its nominal. The tails are the
/// configuration's plus the priming plus `ceil(max_lead)`, the frames a configuration that leads
/// can have written ahead into the FIFO; the residue is the configuration's.
/// @req{CAT-STEADY-004} The FIFO never runs short and never runs over: checked at the audit
/// level (`check::fifo_short`, `check::fifo_over`).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_STEADY_H
#define CATENAE_STEADY_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/views.h"
#include "catenae/fwd.h"
#include "catenae/ratio.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// The whole frames that cover a ratio of frames.
            constexpr std::size_t whole_frames(ratio r) noexcept {
                return (std::size_t{r.up} + r.down - 1) / r.down;
            }
        } // namespace detail

        /// @brief A 1:1 configuration that writes in bursts, presented frame by frame: n frames
        /// out for every n in, the configuration's worst shortfall later.
        template <typename Config>
        class steady {
            using inner_traits = stage_traits<Config>;
            static_assert(inner_traits::rate_ratio == (ratio{1, 1}),
                          "catenae: steady<> wraps a 1:1 configuration: it evens out frames, not rates");

          public:
            using input_port  = typename inner_traits::input_port;  ///< the configuration's
            using output_port = typename inner_traits::output_port; ///< the configuration's

          private:
            using in_views                          = detail::views<input_port>;
            using out_views                         = detail::views<output_port>;
            using out_value                         = typename output_port::value_type;
            static constexpr std::size_t k_width    = output_port::values_per_frame;
            static constexpr std::size_t k_piece    = default_block;
            static constexpr std::size_t k_primed   = detail::whole_frames(inner_traits::max_shortfall);
            static constexpr std::size_t k_ahead    = detail::whole_frames(inner_traits::max_lead);
            static constexpr std::size_t k_burst    = Config::max_outputs_for(k_piece);
            static constexpr std::size_t k_capacity = k_primed + k_ahead + k_burst;
            static_assert(k_capacity <= 0xFFFFFFFFu, "catenae: steady<>'s FIFO holds fewer than 2^32 frames");

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum = 1;    ///< a frame at a time
            static constexpr bool        is_in_place    = true; ///< each piece is read before it is written
            static constexpr bool has_exact_latency = inner_traits::has_exact_latency; ///< when the configuration's is
            /// The configuration's exact latency and the priming; 0 when it has none.
            static constexpr ratio exact_latency =
                has_exact_latency ? inner_traits::exact_latency + ratio{static_cast<std::uint32_t>(k_primed), 1}
                                  : ratio{0, 1};
            /// The frames of silence the FIFO starts with: what this adds to the latency.
            static constexpr std::size_t added_latency_frames = k_primed;

            /// The configuration's state, the FIFO, where it is read, and how many frames it holds.
            struct state {
                state_of<Config>                            inner; ///< the configuration's own
                std::array<out_value, k_capacity * k_width> fifo;  ///< circular, a frame per row
                std::uint32_t                               read;  ///< the frame the next call pays out first
                std::uint32_t                               held;  ///< frames waiting; the priming at rest
            };
            /// The configuration's workspace and one burst of its output.
            struct workspace {
                workspace_of<Config>                     inner; ///< the configuration's own
                std::array<out_value, k_burst * k_width> burst; ///< what one piece wrote
            };

            /// Wraps this configuration.
            explicit constexpr steady(const Config& configuration) noexcept
                : m_inner(configuration) {}

            /// The configuration it wraps.
            constexpr const Config& inner() const noexcept { return m_inner; }
            /// The configuration it wraps, to change a parameter between calls.
            constexpr Config& inner() noexcept { return m_inner; }
            /// The configuration's part of a state.
            static constexpr const state_of<Config>& inner_state(const state& st) noexcept { return st.inner; }
            /// The configuration's part of a state.
            static constexpr state_of<Config>& inner_state(state& st) noexcept { return st.inner; }

            /// The configuration at rest, and the FIFO holding the priming as silence.
            void reset(state& st) const noexcept {
                inner_traits::reset(m_inner, st.inner);
                std::fill(st.fifo.begin(), st.fifo.end(), out_value{});
                st.read = 0;
                st.held = static_cast<std::uint32_t>(k_primed);
            }

            /// The configuration a piece at a time; what it writes joins the FIFO, and as many
            /// frames as came in leave it.
            std::size_t run(state& st, workspace& ws, typename input_port::input_view in,
                            typename output_port::output_view out) const noexcept {
                const std::size_t frames = in_views::frames(in);
                std::size_t       done   = 0;
                while (done < frames) {
                    const std::size_t n    = std::min(k_piece, frames - done);
                    const std::size_t made = m_inner.run(st.inner, ws.inner, in_views::slice(in, done, n),
                                                         out_views::out_of(ws.burst.data(), k_burst, k_burst));
                    detail::check_audit(made <= k_burst, check::outputs_exceeded);
                    // Never further ahead than the lead, so the burst fits; never further behind
                    // than the shortfall the FIFO was primed with, so it holds the n frames owed.
                    detail::check_audit(st.held + made <= k_capacity, check::fifo_over);
                    detail::check_audit(st.held + made >= n, check::fifo_short);
                    put(st, ws, made);
                    take(st, out, done, n);
                    done += n;
                }
                return frames;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// The configuration's latency and the priming.
            stage_latency latency() const noexcept {
                const stage_latency own = m_inner.latency();
                return {own.nominal + static_cast<float>(k_primed), own.deviation};
            }
            /// The configuration's tail, then the FIFO emptied: the priming, and the frames a
            /// configuration that leads wrote ahead.
            std::size_t tail_frames() const noexcept { return m_inner.tail_frames() + k_primed + k_ahead; }
            /// The configuration's practical tail, then the FIFO emptied.
            std::size_t practical_tail_frames() const noexcept {
                return m_inner.practical_tail_frames() + k_primed + k_ahead;
            }
            /// The configuration's: the FIFO adds none.
            float flush_residue() const noexcept { return m_inner.flush_residue(); }

          private:
            /// The burst joins the FIFO after the frames it holds.
            static void put(state& st, const workspace& ws, std::size_t made) noexcept {
                std::size_t at = (std::size_t{st.read} + st.held) % k_capacity;
                for (std::size_t f = 0; f < made; ++f) {
                    for (std::size_t v = 0; v < k_width; ++v) {
                        if constexpr (detail::k_is_planar<output_port>) {
                            st.fifo[at * k_width + v] = ws.burst[v * k_burst + f];
                        }
                        else {
                            st.fifo[at * k_width + v] = ws.burst[f * k_width + v];
                        }
                    }
                    at = at + 1 == k_capacity ? 0 : at + 1;
                }
                st.held = static_cast<std::uint32_t>(st.held + made);
            }
            /// n frames leave the FIFO into out, from frame `from`.
            static void take(state& st, typename output_port::output_view out, std::size_t from,
                             std::size_t n) noexcept {
                std::size_t at = st.read;
                for (std::size_t f = 0; f < n; ++f) {
                    for (std::size_t v = 0; v < k_width; ++v) {
                        if constexpr (detail::k_is_planar<output_port>) {
                            out[v][from + f] = st.fifo[at * k_width + v];
                        }
                        else {
                            out[(from + f) * k_width + v] = st.fifo[at * k_width + v];
                        }
                    }
                    at = at + 1 == k_capacity ? 0 : at + 1;
                }
                st.read = static_cast<std::uint32_t>(at);
                st.held = static_cast<std::uint32_t>(st.held - n);
            }

            Config m_inner; // the configuration: all this holds (D54)
        };

        /// `steady{c}` wraps c.
        template <typename Config>
        steady(Config) -> steady<Config>;

        template <typename Config>
        struct is_stage<steady<Config>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_STEADY_H
