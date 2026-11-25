/// @file bridge.h
/// @brief CF skeleton: `basic_bridge`, bridge's 44.1 -> 48 kHz machine as one catenae stage.
///
/// | Contract   |                                                                                  |
/// |------------|----------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out; S = float only (the skeleton's battery pins no other)  |
/// | Ratio      | 160/147, not in place, input group 147, shortfall 0, lead 146/147, quantum 1:    |
/// |            | an output is written when the first frame it needs arrives (PLAN.md 12)          |
/// | Configured | by a `design<S, Taps>`, a view of a table in the caller's storage or in ROM;    |
/// |            | from a ROM table directly, never from a temporary one; `set_design()` between    |
/// |            | calls                                                                            |
/// | State      | per channel a history of Taps + 64 frames, its end, the schedule's position and  |
/// |            | the frames pending; at rest: silence, end Taps, position 0, one pending (`reset`) |
/// | Workspace  | none                                                                             |
/// | Latency    | exactly (L Taps - 1) / 2M output frames: the prototype's linear-phase delay      |
/// | Tail       | Taps - 1 frames; no residue                                                      |
/// | Method     | the polyphase machine: one row dot per output, driven by the schedule           |
/// | Accumulates| in float, taps in ascending order from the oldest frame (D36: not yet a floor)   |
///
/// Output n is y[n] = sum_k x[floor(nM/L) - k] h[phase(n) + kL] with x[<0] = 0: upfirdn's
/// streaming prefix, sample for sample from n = 0.
///
/// Carried from SampleRateTap 4496888 bridge/include/tap/sr/bridge/converter.h: the stream
/// position's reset values (restart), outputs_for and frames_needed, the superblock walk of
/// process_walk (its append and emit order, the history compaction, the mirrored dot) without
/// its per-channel-count specialisations or committed trip-count dispatch.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_BRIDGE_BRIDGE_H
#define CATENAE_FIT_BRIDGE_BRIDGE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "bridge/design.h"
#include "bridge/schedule.h"
#include "catenae/chain.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae_fit::bridge {

    namespace detail {
        /// A row against a window, oldest frame first: the float accumulation, taps ascending.
        template <std::size_t Taps, typename V>
        V dot_forward(catenae::span<const V> row, catenae::span<const V> window) noexcept {
            V acc{};
            for (std::size_t t = 0; t < Taps; ++t) {
                acc += row[t] * window[t];
            }
            return acc;
        }
        /// The same with the row read backward: a mirrored phase, the same products in the
        /// same order as dotting the mirrored row itself.
        template <std::size_t Taps, typename V>
        V dot_reversed(catenae::span<const V> row, catenae::span<const V> window) noexcept {
            V acc{};
            for (std::size_t t = 0; t < Taps; ++t) {
                acc += row[Taps - 1 - t] * window[t];
            }
            return acc;
        }
    } // namespace detail

    /// @brief 44.1 -> 48 kHz for `Ch` interleaved channels, through a `Design`'s table.
    template <typename S, std::size_t Ch, typename Design>
    class basic_bridge {
        static_assert(std::is_same<typename Design::format, S>::value,
                      "catenae_fit: a bridge stage's design is for its sample format (D37)");

      public:
        using input_port  = catenae::port<S, Ch>;        ///< 44.1 kHz frames
        using output_port = catenae::port<S, Ch>;        ///< 48 kHz frames
        using design_type = Design;                      ///< what configures it
        using table_type  = typename Design::table_type; ///< a ROM table of that design

      private:
        using value_type                         = catenae::storage_t<S>;
        static constexpr std::size_t   k_taps    = Design::taps;
        static constexpr std::size_t   k_slack   = 64; // appends between compactions (bridge's)
        static constexpr std::size_t   k_history = k_taps + k_slack;
        static constexpr std::uint32_t k_l       = static_cast<std::uint32_t>(phases);
        static constexpr std::uint32_t k_m       = static_cast<std::uint32_t>(decimation);

      public:
        static constexpr catenae::ratio rate_ratio{k_l, k_m}; ///< 160/147
        /// 147: only whole superblocks of input give exactly L/M out.
        static constexpr std::size_t    input_group = decimation;
        static constexpr catenae::ratio max_shortfall{0, 1};       ///< it never trails
        static constexpr catenae::ratio max_lead{k_m - 1, k_m};    ///< writes on a group's first frame
        static constexpr std::size_t    output_quantum    = 1;     ///< a frame at a time
        static constexpr bool           is_in_place       = false; ///< it writes in bursts
        static constexpr bool           has_exact_latency = true;  ///< a linear-phase prototype
        /// (L Taps - 1) / 2M output frames: the prototype's centre, at the output rate.
        static constexpr catenae::ratio exact_latency =
            catenae::reduce(catenae::ratio{k_l * static_cast<std::uint32_t>(k_taps) - 1, 2 * k_m});

        /// One instance: bridge's planar histories and its stream position.
        struct state {
            std::array<std::array<value_type, k_history>, Ch> history;  ///< per channel, oldest first
            std::uint32_t                                     end;      ///< one past the newest frame
            std::uint32_t                                     position; ///< the schedule's, in [0, L)
            std::uint32_t                                     pending;  ///< frames before the next output
        };
        using workspace = catenae::no_workspace; ///< nothing

        /// A stage through a design: a view, so the design object itself may be a temporary.
        explicit constexpr basic_bridge(const Design& d) noexcept
            : m_design(d) {}
        /// A stage through a ROM table, checked by the design's from_table.
        explicit constexpr basic_bridge(const table_type& rom) noexcept
            : m_design(Design::from_table(rom)) {}
        /// Never through a temporary table: the view would outlive it (PLAN.md 3.3).
        // CF finding: written over the table's own type, not span<const T>, which binds a
        // temporary array as std::span does; so a ROM table must be a std::array of exactly this
        // type, and every view-taking constructor repeats the pair.
        basic_bridge(const table_type&&) = delete;

        /// The design it runs.
        constexpr const Design& design() const noexcept { return m_design; }
        /// Another design, from the next run(). Between calls only.
        constexpr void set_design(const Design& d) noexcept { m_design = d; }

        /// Silence in every history, the schedule at its start, one frame pending: bridge's
        /// restart, the pre-advance that puts x[0] under output 0.
        void reset(state& st) const noexcept {
            for (auto& channel : st.history) {
                channel.fill(value_type{});
            }
            st.end      = static_cast<std::uint32_t>(k_taps);
            st.position = 0;
            st.pending  = 1;
        }

        /// The superblock walk: takes every frame of `in`, writes every output that is ready.
        std::size_t run(state& st, workspace&, catenae::span<const value_type> in,
                        catenae::span<value_type> out) const noexcept {
            const std::size_t frames   = in.size() / Ch;
            const std::size_t total    = outputs_for(st, frames);
            std::size_t       taken    = 0;
            std::size_t       end      = st.end;
            std::size_t       position = st.position;
            std::size_t       pending  = st.pending;
            for (std::size_t made = 0; made < total; ++made) {
                while (pending != 0) { // never past the input: total is exact
                    end = append(st, end, in.subspan(taken * Ch, Ch));
                    ++taken;
                    --pending;
                }
                emit(st, end, position, out.subspan(made * Ch, Ch));
                pending  = schedule<phases, decimation>[position].advance;
                position = position + 1 == phases ? 0 : position + 1;
            }
            while (taken < frames) { // short of the next output: into the history
                end = append(st, end, in.subspan(taken * Ch, Ch));
                ++taken;
                --pending;
            }
            st.end      = catenae::detail::narrow<std::uint32_t>(end);
            st.position = catenae::detail::narrow<std::uint32_t>(position);
            st.pending  = catenae::detail::narrow<std::uint32_t>(pending);
            return total;
        }

        /// Exactly what the next call of n frames writes: the largest N whose need is within n.
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            if (n < st.pending) {
                return 0;
            }
            const std::uint64_t base = std::uint64_t{st.position} * k_m / k_l;
            const std::uint64_t more = std::uint64_t{n} - st.pending;
            return catenae::detail::narrow<std::size_t>((k_l * (base + more + 1) - 1) / k_m - st.position + 1);
        }
        /// The frames the next k outputs need: the pending gap, then the schedule's advances.
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            if (k == 0) {
                return 0;
            }
            const std::uint64_t at = st.position;
            return catenae::detail::narrow<std::size_t>(st.pending + (at + k - 1) * k_m / k_l - at * k_m / k_l);
        }
        /// ceil(n L / M): what a call of n frames writes from reset, the most from any state.
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept {
            return catenae::detail::narrow<std::size_t>((std::uint64_t{n} * k_l + k_m - 1) / k_m);
        }
        /// Exact, as exact_latency.
        catenae::stage_latency latency() const noexcept {
            return {static_cast<float>(k_l * k_taps - 1) / static_cast<float>(2 * k_m), 0.0f};
        }
        /// Taps - 1: after them every window is zeros.
        std::size_t tail_frames() const noexcept { return k_taps - 1; }
        /// The same: an FIR has no residue to wait out.
        std::size_t practical_tail_frames() const noexcept { return k_taps - 1; }
        /// None.
        float flush_residue() const noexcept { return 0.0f; }

      private:
        /// One frame into every channel's history, compacting first when it is full; returns the
        /// new end.
        static std::size_t append(state& st, std::size_t end, catenae::span<const value_type> frame) noexcept {
            if (end == k_history) {
                for (auto& channel : st.history) {
                    // The newest Taps - 1 frames to the front.
                    std::copy(channel.begin() + static_cast<std::ptrdiff_t>(end - (k_taps - 1)),
                              channel.begin() + static_cast<std::ptrdiff_t>(end), channel.begin());
                }
                end = k_taps - 1;
            }
            for (std::size_t c = 0; c < Ch; ++c) {
                st.history[c][end] = frame[c];
            }
            return end + 1;
        }

        /// One output frame at schedule position `position`.
        void emit(const state& st, std::size_t end, std::size_t position,
                  catenae::span<value_type> frame) const noexcept {
            const std::size_t                     phase    = schedule<phases, decimation>[position].phase;
            const catenae::span<const value_type> row      = m_design.row(phase);
            const bool                            mirrored = Design::is_mirrored(phase);
            for (std::size_t c = 0; c < Ch; ++c) {
                const catenae::span<const value_type> window =
                    catenae::span<const value_type>(st.history[c]).subspan(end - k_taps, k_taps);
                frame[c] =
                    mirrored ? detail::dot_reversed<k_taps>(row, window) : detail::dot_forward<k_taps>(row, window);
            }
        }

        Design m_design; // a view of the table (D54: the table is never in the configuration)
    };

    /// 44.1 -> 48 kHz in float, `Taps` per phase.
    template <std::size_t Ch, std::size_t Taps>
    using bridge_to_48k = basic_bridge<float, Ch, design<float, Taps>>;

} // namespace catenae_fit::bridge

namespace catenae {
    inline namespace CATENAE_ABI {
        /// The skeleton's battery pins float only (D29).
        template <std::size_t Ch, typename Design>
        struct is_stage<catenae_fit::bridge::basic_bridge<float, Ch, Design>> : std::true_type {};
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FIT_BRIDGE_BRIDGE_H
