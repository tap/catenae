/// @file block_region.h
/// @brief A stage that works in blocks (guide: Bursts and buffering): a stand-in for a
/// transform-domain region, which gathers a hop of frames and writes them all at once.
// Copyright 2026 Timothy Place and the catenae contributors
//
// PLAN.md 9.5's region (stft, a spectral gain, istft) arrives at C5. Its arrival numbers are
// what this chapter is about, and a block stage of 128 frames has the same ones: 1:1 in the long
// run, nothing for 127 frames, then 128 at once.
#ifndef CATENAE_EXAMPLES_BLOCK_REGION_H
#define CATENAE_EXAMPLES_BLOCK_REGION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace example {

    /// @brief Gathers `Hop` mono frames and writes them, halved, when the hop is full.
    ///
    /// | Contract   |                                                                      |
    /// |------------|----------------------------------------------------------------------|
    /// | Ports      | `port<float, 1>` in and out                                          |
    /// | Ratio      | 1:1, input group `Hop`, up to `Hop - 1` frames behind, quantum `Hop` |
    /// | Configured | by `Hop`                                                             |
    /// | State      | the frames of the hop so far; at rest all zero (`rests_at_zero`)     |
    /// | Workspace  | none                                                                 |
    /// | Latency    | exactly 0: each frame comes out as itself, halved, only later        |
    /// | Tail       | `Hop - 1` frames; no residue                                         |
    /// | Formats    | `float`                                                              |
    /// | Method     | a block gathered and written whole, as a transform hop is            |
    /// | Rationale  | D35                                                                  |
    // ANCHOR: region
    template <std::size_t Hop>
    class block_region {
      public:
        using input_port  = catenae::port<float, 1>;
        using output_port = catenae::port<float, 1>;
        // 1:1 in the long run, but only whole hops are owed exactly: a call of fewer frames
        // can write nothing, and the output trails the input by up to Hop - 1 frames.
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = Hop;
        static constexpr catenae::ratio max_shortfall{static_cast<std::uint32_t>(Hop - 1), 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum = Hop;
        // ANCHOR_END: region
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};
        static constexpr bool           rests_at_zero = true;

        /// The hop so far.
        struct state {
            std::array<float, Hop> held;  ///< the frames gathered
            std::uint32_t          count; ///< how many, below Hop
        };
        using workspace = catenae::no_workspace;

        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            std::size_t written = 0;
            for (const float x : in) {
                st.held[st.count] = x;
                if (++st.count == Hop) {
                    for (std::size_t i = 0; i < Hop; ++i) {
                        out[written + i] = st.held[i] * 0.5f;
                    }
                    written += Hop;
                    st.count = 0;
                }
            }
            return written;
        }
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            return (st.count + n) / Hop * Hop;
        }
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            return k == 0 ? 0 : (k + Hop - 1) / Hop * Hop - st.count;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return (Hop - 1 + n) / Hop * Hop; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return Hop - 1; }
        std::size_t                  practical_tail_frames() const noexcept { return Hop - 1; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

} // namespace example

namespace catenae {
    template <std::size_t Hop>
    struct is_stage<example::block_region<Hop>> : std::true_type {};
} // namespace catenae

#endif // CATENAE_EXAMPLES_BLOCK_REGION_H
