/// @file parallel.h
/// @brief `parallel<A, B, Align>`: two chains on two groups of one port's channels (D43).
///
/// | Contract   |                                                                               |
/// |------------|-------------------------------------------------------------------------------|
/// | Ports      | in: A's channels then B's; out: A's output channels then B's; one format and layout a side |
/// | Ratio      | the arms' L/1, input group 1; in place when its two ports are one type and L is 1 |
/// | Configured | by its two arms; `arm<0>()` and `arm<1>()` reach them                         |
/// | State      | the arms' states and the aligning delay's line (`reset`)                      |
/// | Workspace  | the arms' workspaces and a block of each arm's input and output               |
/// | Latency    | `by_type`: the larger arm's, exactly; `by_caller`: not exact, the later arm's nominal |
/// | Tail       | each arm's and the delay its output carries, the larger; the larger residue  |
/// | Formats    | the arms', one on each side                                                   |
/// | Limits     | two arms, each frame-synchronous, at one ratio; audio ports only              |
/// | Method     | each arm on its channels, a block at a time; a fixed delay on the arm ahead   |
/// | Rationale  | D42, D43, D49, D50                                                            |
///
/// @req{CAT-PARALLEL-001} The input port carries A's input channels then B's and the output port
/// A's output channels then B's; each arm reads and writes its own channels as it would alone,
/// and an arm may change its channel count. The arms read one sample format and layout and
/// write one; an arm that is not frame-synchronous, or two arms of different ratios, do not
/// compile.
/// @req{CAT-PARALLEL-002} `parallel<A, B>` (`align::by_type`) needs each arm's exact latency to
/// be a whole number of frames, or does not compile. It delays the arm with the smaller latency
/// by the difference, so `exact_latency` is the larger and each output channel is its arm's
/// output, the earlier arm's that many frames later, bit for bit.
/// @req{CAT-PARALLEL-003} `parallel<A, B, align::by_caller>` inserts no delay: each output
/// channel is its arm's output, bit for bit. It has no exact latency; `latency()` is the larger
/// nominal, with a deviation that is the larger deviation plus the difference of the nominals.
/// @req{CAT-PARALLEL-004} A call of n frames writes n L. The tails are the larger of each arm's
/// own and the input frames the delay on its output holds; the residue is the larger.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PARALLEL_H
#define CATENAE_PARALLEL_H

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
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief Arm A on the first of a port's channels and arm B on the rest, side by side;
        /// with `align::by_type` their latencies aligned by a delay the types size.
        template <typename A, typename B, align Align>
        class parallel {
            using traits_a = stage_traits<A>;
            using traits_b = stage_traits<B>;
            using a_in     = detail::port_facts<typename traits_a::input_port>;
            using b_in     = detail::port_facts<typename traits_b::input_port>;
            using a_out    = detail::port_facts<typename traits_a::output_port>;
            using b_out    = detail::port_facts<typename traits_b::output_port>;
            static_assert(a_in::k_kind == signal_kind::audio && b_in::k_kind == signal_kind::audio
                              && a_out::k_kind == signal_kind::audio && b_out::k_kind == signal_kind::audio,
                          "catenae: parallel<> splits the channels of an audio port");
            static_assert(traits_a::is_frame_synchronous && traits_b::is_frame_synchronous,
                          "catenae: parallel<>'s arms are frame-synchronous: wrap a bursting arm in steady<>");
            static_assert(traits_a::rate_ratio == traits_b::rate_ratio, "catenae: parallel<>'s arms run at one ratio");
            static_assert(std::is_same<typename a_in::format, typename b_in::format>::value
                              && a_in::k_arrangement == b_in::k_arrangement,
                          "catenae: parallel<>'s arms read one sample format and one layout");
            static_assert(std::is_same<typename a_out::format, typename b_out::format>::value
                              && a_out::k_arrangement == b_out::k_arrangement,
                          "catenae: parallel<>'s arms write one sample format and one layout");
            static_assert(Align == align::by_caller || (traits_a::has_exact_latency && traits_b::has_exact_latency),
                          "catenae: parallel<> aligns arms whose latency is exact; beside an arm whose latency is not, "
                          "write parallel<A, B, align::by_caller> and place a delay");
            static_assert(
                Align == align::by_caller
                    || (reduce(traits_a::exact_latency).down == 1 && reduce(traits_b::exact_latency).down == 1),
                "catenae: parallel<> aligns by whole frames: each arm's exact latency is a whole number of frames");

            static constexpr std::size_t k_a_in      = a_in::k_channels;
            static constexpr std::size_t k_b_in      = b_in::k_channels;
            static constexpr std::size_t k_a_out     = a_out::k_channels;
            static constexpr std::size_t k_b_out     = b_out::k_channels;
            static constexpr std::size_t k_up        = reduce(traits_a::rate_ratio).up; // frame-synchronous: L/1
            static constexpr std::size_t k_piece     = default_block;
            static constexpr std::size_t k_latency_a = Align == align::by_type ? reduce(traits_a::exact_latency).up : 0;
            static constexpr std::size_t k_latency_b = Align == align::by_type ? reduce(traits_b::exact_latency).up : 0;
            static constexpr bool        k_delays_a  = k_latency_a < k_latency_b; // the arm that is ahead waits
            static constexpr std::size_t k_delay = k_delays_a ? k_latency_b - k_latency_a : k_latency_a - k_latency_b;
            static constexpr std::size_t k_delayed_channels = k_delays_a ? k_a_out : k_b_out;
            static_assert(k_delay <= 0xFFFFFFFFu, "catenae: parallel<>'s aligning delay is below 2^32 frames");

          public:
            /// A's input channels, then B's.
            using input_port = port<typename a_in::format, k_a_in + k_b_in, a_in::k_arrangement>;
            /// A's output channels, then B's.
            using output_port = port<typename a_out::format, k_a_out + k_b_out, a_out::k_arrangement>;

          private:
            using in_value                     = typename input_port::value_type;
            using out_value                    = typename output_port::value_type;
            using a_in_views                   = detail::views<typename traits_a::input_port>;
            using b_in_views                   = detail::views<typename traits_b::input_port>;
            using a_out_views                  = detail::views<typename traits_a::output_port>;
            using b_out_views                  = detail::views<typename traits_b::output_port>;
            using in_views                     = detail::views<input_port>;
            static constexpr bool k_planar_in  = detail::k_is_planar<input_port>;
            static constexpr bool k_planar_out = detail::k_is_planar<output_port>;

          public:
            static constexpr ratio       rate_ratio  = reduce(traits_a::rate_ratio); ///< the arms'
            static constexpr std::size_t input_group = 1;                            ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};                        ///< never behind
            static constexpr ratio       max_lead{0, 1};                             ///< never ahead
            static constexpr std::size_t output_quantum = k_up;                      ///< L frames a frame
            /// In place when the two ports are one type and the ratio is 1:1: a block is read before
            /// it is written.
            static constexpr bool is_in_place = std::is_same<input_port, output_port>::value && k_up == 1;
            /// Exact when the types align the arms.
            static constexpr bool has_exact_latency = Align == align::by_type;
            /// The larger arm's latency, in output frames; 0 when not exact.
            static constexpr ratio exact_latency{
                static_cast<std::uint32_t>(k_latency_a > k_latency_b ? k_latency_a : k_latency_b), 1};

            /// The arms' states, and the line of the delay on the arm that is ahead.
            struct state {
                state_of<A>                                         a;     ///< A's own
                state_of<B>                                         b;     ///< B's own
                std::array<out_value, k_delay * k_delayed_channels> line;  ///< the delay's last frames
                std::uint32_t                                       write; ///< the frame the next replaces
            };
            /// The arms' workspaces, and a block of each arm's input and output.
            struct workspace {
                workspace_of<A>                                 a;     ///< A's own
                workspace_of<B>                                 b;     ///< B's own
                std::array<in_value, k_piece * k_a_in>          a_in;  ///< A's channels of a block
                std::array<in_value, k_piece * k_b_in>          b_in;  ///< B's channels of a block
                std::array<out_value, k_piece * k_up * k_a_out> a_out; ///< what A wrote
                std::array<out_value, k_piece * k_up * k_b_out> b_out; ///< what B wrote
            };

            /// Arm a on the first channels, b on the rest.
            constexpr parallel(const A& a, const B& b) noexcept
                : m_a(a)
                , m_b(b) {}

            /// Arm I's configuration: 0 is A, 1 is B.
            template <std::size_t I>
            constexpr const auto& arm() const noexcept {
                static_assert(I < 2, "catenae: parallel<> has two arms, 0 and 1");
                if constexpr (I == 0) {
                    return m_a;
                }
                else {
                    return m_b;
                }
            }
            /// Arm I's configuration, to change a parameter between calls.
            template <std::size_t I>
            constexpr auto& arm() noexcept {
                static_assert(I < 2, "catenae: parallel<> has two arms, 0 and 1");
                if constexpr (I == 0) {
                    return m_a;
                }
                else {
                    return m_b;
                }
            }

            /// Each arm at rest, and the delay's line silent.
            void reset(state& st) const noexcept {
                traits_a::reset(m_a, st.a);
                traits_b::reset(m_b, st.b);
                std::fill(st.line.begin(), st.line.end(), out_value{});
                st.write = 0;
            }

            /// A block at a time: each arm on its channels, the one ahead delayed, both written out.
            std::size_t run(state& st, workspace& ws, typename input_port::input_view in,
                            typename output_port::output_view out) const noexcept {
                const std::size_t frames = in_views::frames(in);
                std::size_t       done   = 0;
                while (done < frames) {
                    const std::size_t n = std::min(k_piece, frames - done);
                    gather<k_a_in, 0>(in, done, n, ws.a_in.data());
                    gather<k_b_in, k_a_in>(in, done, n, ws.b_in.data());
                    constexpr std::size_t cap    = k_piece * k_up;
                    const std::size_t     made_a = m_a.run(st.a, ws.a, a_in_views::in_of(ws.a_in.data(), k_piece, n),
                                                           a_out_views::out_of(ws.a_out.data(), cap, cap));
                    const std::size_t     made_b = m_b.run(st.b, ws.b, b_in_views::in_of(ws.b_in.data(), k_piece, n),
                                                           b_out_views::out_of(ws.b_out.data(), cap, cap));
                    // Frame-synchronous arms write n L frames, every call (the traits' promise).
                    detail::check_audit(made_a == n * k_up && made_b == n * k_up, check::invariant);
                    if constexpr (k_delay > 0) {
                        delay_ahead(st, k_delays_a ? ws.a_out.data() : ws.b_out.data(), n * k_up);
                    }
                    scatter<k_a_out, 0>(ws.a_out.data(), out, done * k_up, n * k_up);
                    scatter<k_b_out, k_a_out>(ws.b_out.data(), out, done * k_up, n * k_up);
                    done += n;
                }
                return frames * k_up;
            }

            /// n L, from any state.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n * k_up; }
            /// The fewest frames that write k.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept {
                return (k + k_up - 1) / k_up;
            }
            /// n L.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n * k_up; }

            /// by_type: the larger arm's, aligned. by_caller: the later arm's nominal, and a
            /// deviation that reaches the earlier one.
            stage_latency latency() const noexcept {
                stage_latency a = m_a.latency();
                stage_latency b = m_b.latency();
                if constexpr (k_delay > 0) {
                    (k_delays_a ? a : b).nominal += static_cast<float>(k_delay);
                }
                const float later   = a.nominal > b.nominal ? a.nominal : b.nominal;
                const float earlier = a.nominal > b.nominal ? b.nominal : a.nominal;
                const float spread  = a.deviation > b.deviation ? a.deviation : b.deviation;
                return {later, spread + (later - earlier)};
            }
            /// The larger of each arm's tail and what the delay on its output holds.
            std::size_t tail_frames() const noexcept { return longer(m_a.tail_frames(), m_b.tail_frames()); }
            /// The same, from the practical tails.
            std::size_t practical_tail_frames() const noexcept {
                return longer(m_a.practical_tail_frames(), m_b.practical_tail_frames());
            }
            /// The larger of the arms' residues.
            float flush_residue() const noexcept {
                const float a = m_a.flush_residue();
                const float b = m_b.flush_residue();
                return a > b ? a : b;
            }

          private:
            /// Where frame f, channel c of an arm's block of `cap` frames lies.
            template <bool Planar, std::size_t Channels>
            static constexpr std::size_t slot(std::size_t cap, std::size_t f, std::size_t c) noexcept {
                return Planar ? c * cap + f : f * Channels + c;
            }

            /// Channels [First, First + Count) of input frames [from, from + n) into an arm's block.
            template <std::size_t Count, std::size_t First>
            static void gather(typename input_port::input_view in, std::size_t from, std::size_t n,
                               in_value* block) noexcept {
                for (std::size_t f = 0; f < n; ++f) {
                    for (std::size_t c = 0; c < Count; ++c) {
                        if constexpr (k_planar_in) {
                            block[slot<true, Count>(k_piece, f, c)] = in[First + c][from + f];
                        }
                        else {
                            block[slot<false, Count>(k_piece, f, c)] = in[(from + f) * (k_a_in + k_b_in) + First + c];
                        }
                    }
                }
            }

            /// An arm's block of n frames into output channels [First, First + Count) from frame `at`.
            template <std::size_t Count, std::size_t First>
            static void scatter(const out_value* block, typename output_port::output_view out, std::size_t at,
                                std::size_t n) noexcept {
                constexpr std::size_t cap = k_piece * k_up;
                for (std::size_t f = 0; f < n; ++f) {
                    for (std::size_t c = 0; c < Count; ++c) {
                        if constexpr (k_planar_out) {
                            out[First + c][at + f] = block[slot<true, Count>(cap, f, c)];
                        }
                        else {
                            out[(at + f) * (k_a_out + k_b_out) + First + c] = block[slot<false, Count>(cap, f, c)];
                        }
                    }
                }
            }

            /// The arm that is ahead, n frames of its block through the delay's line.
            static void delay_ahead(state& st, out_value* block, std::size_t n) noexcept {
                constexpr std::size_t cap = k_piece * k_up;
                std::size_t           at  = st.write;
                for (std::size_t f = 0; f < n; ++f) {
                    for (std::size_t c = 0; c < k_delayed_channels; ++c) {
                        out_value&      value                = block[slot<k_planar_out, k_delayed_channels>(cap, f, c)];
                        const out_value arriving             = value;
                        value                                = st.line[at * k_delayed_channels + c];
                        st.line[at * k_delayed_channels + c] = arriving;
                    }
                    at = at + 1 == k_delay ? 0 : at + 1;
                }
                st.write = static_cast<std::uint32_t>(at);
            }

            /// The larger of the arms' tails, each with the input frames its delay holds.
            static constexpr std::size_t longer(std::size_t a, std::size_t b) noexcept {
                constexpr std::size_t held = (k_delay + k_up - 1) / k_up;
                const std::size_t     ta   = a + (k_delay > 0 && k_delays_a ? held : 0);
                const std::size_t     tb   = b + (k_delay > 0 && !k_delays_a ? held : 0);
                return ta > tb ? ta : tb;
            }

            A m_a; // the arms: all this holds (D54)
            B m_b;
        };

        /// `parallel{a, b}` aligns the two by their types.
        template <typename A, typename B>
        parallel(A, B) -> parallel<A, B>;

        template <typename A, typename B, align Align>
        struct is_stage<parallel<A, B, Align>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PARALLEL_H
