/// @file gain.h
/// @brief `gain`: a level that ramps when it changes, so a change does not click (PLAN.md 9.1).
///
/// | Contract   |                                                                           |
/// |------------|---------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                  |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)       |
/// | Configured | a level and a ramp length in frames; `set_level()` between calls          |
/// | State      | the level applied now, the one it ramps to, the step and the frames left  |
/// | Rest       | at the configured level with no ramp running (`reset`, not zero)          |
/// | Workspace  | none                                                                      |
/// | Latency    | exactly 0                                                                 |
/// | Tail       | 0 frames                                                                  |
/// | Formats    | float, double; q15, q31 and Q4.27 declare the contract, their arithmetic  |
/// |            | is new design and arrives at C6 (D47)                                     |
/// | Limits     | a non-finite level poisons the output until a finite one is set and ramped to |
/// |            | a steady level runs in runs of 16 that the compiler may vectorise: where its  |
/// |            | vector unit flushes subnormals and its scalar unit does not (the M55 under    |
/// |            | QEMU at -O3), subnormal samples and NaN payloads can differ between builds    |
/// |            | of one source, as they could before; the build's setting is AOU-008's         |
/// | Buffers    | `in` and `out` are one buffer or do not overlap (AOU-007), which its loops    |
/// |            | tell the compiler through `CATENAE_RESTRICT` (DEV-003); checked at audit      |
/// | Method     | a linear ramp, each frame's level computed from the goal, not accumulated |
/// | Contraction| pinned at contraction off; a header template, it compiles under the caller's |
/// |            | flags, and a fused multiply-add there can move a ramp's levels by an ULP     |
/// | Rationale  | D24, D34, D47, D49, PLAN.md 9.1                                           |
///
/// @req{CAT-GAIN-001} From reset every frame is multiplied by the configured level, every
/// channel alike, in place or not.
/// @req{CAT-GAIN-002} A level set between calls is reached over the ramp's R frames: the k-th
/// frame of the ramp (k from 1) applies goal - step x (R - k), step = (goal - start) / R, so
/// the R-th applies the goal exactly and the levels move monotonically; R = 0 jumps.
/// @req{CAT-GAIN-003} A gain's output is the same for any chunking of its input, a ramp that
/// spans calls included.
/// @req{CAT-GAIN-004} At the audit level, `run` checks that `out` holds as many values as `in`
/// (`check::index_out_of_range`) and that the two are one buffer or do not overlap
/// (`check::buffers_overlap`), which its loops assume (AOU-007, DEV-003).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_GAIN_H
#define CATENAE_GAIN_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/compiler.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/bits.h"
#include "catenae/detail/convert.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            /// The type a gain's level is held in: the sample's, for a floating format. A
            /// fixed-point gain's level format is part of its profile (C6); float holds the
            /// contract's place until then.
            template <typename S>
            using gain_level_t = std::conditional_t<format_traits<S>::is_fixed_point, float, storage_t<S>>;
        } // namespace detail

        /// @brief A level applied to every channel of a `port<S, Ch>`, ramped when it changes.
        template <typename S, std::size_t Ch>
        class basic_gain {
          public:
            using input_port  = port<S, Ch>;             ///< what it reads
            using output_port = port<S, Ch>;             ///< what it writes
            using level_type  = detail::gain_level_t<S>; ///< what a level is held in

          private:
            using value_type = storage_t<S>;

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};

            /// Where the ramp has got to.
            struct state {
                level_type    current;   ///< the level applied to the last frame
                level_type    goal;      ///< the level being ramped to
                level_type    step;      ///< per frame, while a ramp runs
                std::uint32_t remaining; ///< frames of ramp left
            };
            using workspace = no_workspace; ///< nothing

            /// A gain at `level`, which ramps over `ramp_frames` frames when it is changed.
            constexpr basic_gain(level_type level, std::uint32_t ramp_frames) noexcept
                : m_level(level)
                , m_ramp_frames(ramp_frames) {}

            /// The configured level.
            constexpr level_type level() const noexcept { return m_level; }
            /// A new level, ramped to from the next run(). Between calls only.
            constexpr void set_level(level_type level) noexcept { m_level = level; }
            /// The ramp's length in frames.
            constexpr std::uint32_t ramp_frames() const noexcept { return m_ramp_frames; }

            /// At the configured level, with no ramp running.
            void reset(state& st) const noexcept {
                st.current   = m_level;
                st.goal      = m_level;
                st.step      = level_type{};
                st.remaining = 0;
            }

            /// Every frame times the level, ramping when it has changed.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point gain is new design and arrives with its profile at C6 (D47)");
                if (!detail::same_bits(st.goal, m_level)) {
                    st.goal = m_level;
                    if (m_ramp_frames == 0) {
                        st.current   = m_level;
                        st.remaining = 0;
                    }
                    else {
                        st.step      = (m_level - st.current) / static_cast<level_type>(m_ramp_frames);
                        st.remaining = m_ramp_frames;
                    }
                }
                // A store through `out` may be a store to the state for all the compiler knows, so
                // each frame's level is a local: a ramp's frames take the frame-by-frame path, which
                // reads its counters from the state, and the steady rest reads the state once.
                const std::size_t       frames = in.size() / Ch;
                const value_type* const from   = in.data();
                value_type* const       to     = out.data();
                // The loops index raw pointers, which span's audit check no longer sees, and tell
                // the compiler the two buffers are one or apart: both are checked here at audit.
                detail::check_audit(out.size() >= in.size(), check::index_out_of_range);
                detail::check_audit(one_or_apart(from, to, in.size()), check::buffers_overlap);
                std::size_t frame = 0;
                for (; frame < frames && st.remaining > 0; ++frame) {
                    --st.remaining;
                    st.current             = st.goal - st.step * static_cast<level_type>(st.remaining);
                    const level_type level = st.current;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        to[frame * Ch + c] = from[frame * Ch + c] * level;
                    }
                }
                // The rest is at one level. `in` and `out` are one buffer or do not overlap
                // (AOU-007, `is_in_place`), and each case has a loop that says so.
                const std::size_t done = frame * Ch;
                const std::size_t left = (frames - frame) * Ch;
                if (from == to) {
                    scale_in_place(to + done, left, st.current);
                }
                else {
                    scale(from + done, to + done, left, st.current);
                }
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
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            /// Whether `count` samples at `a` and at `b` are one buffer or do not overlap, by
            /// address (relational operators on unrelated pointers are unspecified).
            static bool one_or_apart(const value_type* a, const value_type* b, std::size_t count) noexcept {
                const auto x     = reinterpret_cast<std::uintptr_t>(a);
                const auto y     = reinterpret_cast<std::uintptr_t>(b);
                const auto bytes = count * sizeof(value_type);
                return x == y || x + bytes <= y || y + bytes <= x;
            }

            /// Samples a steady level is applied to at a time: a fixed count the compiler
            /// unrolls, a whole number of every vector width in use.
            static constexpr std::size_t k_run = 16;

            /// `count` samples times `level`, from one buffer to another that it does not overlap.
            static void scale(const value_type* CATENAE_RESTRICT from, value_type* CATENAE_RESTRICT to,
                              std::size_t count, level_type level) noexcept {
                std::size_t i = 0;
                for (; i + k_run <= count; i += k_run) {
                    for (std::size_t j = 0; j < k_run; ++j) {
                        to[i + j] = from[i + j] * level;
                    }
                }
                for (; i < count; ++i) {
                    to[i] = from[i] * level;
                }
            }

            /// `count` samples times `level`, where they stand.
            static void scale_in_place(value_type* CATENAE_RESTRICT samples, std::size_t count,
                                       level_type level) noexcept {
                std::size_t i = 0;
                for (; i + k_run <= count; i += k_run) {
                    for (std::size_t j = 0; j < k_run; ++j) {
                        samples[i + j] = samples[i + j] * level;
                    }
                }
                for (; i < count; ++i) {
                    samples[i] = samples[i] * level;
                }
            }

            level_type    m_level;
            std::uint32_t m_ramp_frames;
        };

        template <std::size_t Ch>
        using gain = basic_gain<float, Ch>; ///< in float (D24)
        template <std::size_t Ch>
        using gain_f64 = basic_gain<double, Ch>; ///< in double
        template <std::size_t Ch>
        using gain_q15 = basic_gain<q15, Ch>; ///< in Q15: the contract now, the arithmetic at C6
        template <std::size_t Ch>
        using gain_q31 = basic_gain<q31, Ch>; ///< in Q31: the contract now, the arithmetic at C6
        template <std::size_t Ch>
        using gain_q4_27 = basic_gain<q4_27, Ch>; ///< in Q4.27: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch>
        struct is_stage<basic_gain<S, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_GAIN_H
