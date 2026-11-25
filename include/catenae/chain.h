/// @file chain.h
/// @brief A chain: stages composed by type with `a | b | c`, itself a stage (PLAN.md section 4).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Type       | `chain<Stages...>`, written by the caller (D13); `\|` always flattens        |
/// | Joints     | proven when the type is formed: one message per way two ports differ        |
/// | Memory     | configuration (the object), `state` (one instance), `workspace` (one thread) |
/// | Entry      | `process`: the one boundary check, then a block at a time (64 frames, or     |
/// |            | `with_block<N>`); fixed blocks are checked by the compiler                   |
/// | Arrival    | `input_group`, `max_shortfall`, `max_lead`, `output_quantum` composed (D35)  |
/// | Latency    | each stage's carried to the output rate and summed; exact when every one is  |
/// | Flush      | resumable; zero-pads the input by the composed tail                          |
/// | Rationale  | D6, D13, D27, D35, D39, D50, D54                                             |
///
/// The requirements are on the chain contract's page, CAT-CHAIN-001 onwards.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_CHAIN_H
#define CATENAE_CHAIN_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/arrival.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/views.h"
#include "catenae/fwd.h"
#include "catenae/junction_traits.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Which tail a flush feeds: the frames to -120 dB re full scale, or the exact one.
        enum class tail : std::uint8_t {
            practical, ///< each stage's practical_tail_frames()
            exact,     ///< each stage's tail_frames()
        };

        /// The frames of its input a chain works on at a time, unless with_block says otherwise.
        inline constexpr std::size_t default_block = 64;

        /// What one instance of a configuration remembers.
        template <typename Config>
        using state_of = typename std::remove_cv_t<Config>::state;
        /// What a call of a configuration needs: one per thread.
        template <typename Config>
        using workspace_of = typename std::remove_cv_t<Config>::workspace;

        /// A state and a workspace together, for the common case of one instance.
        template <typename State, typename Workspace>
        struct memory {
            State     state;     ///< the instance's state
            Workspace workspace; ///< its workspace
        };
        /// The memory of one instance of a configuration.
        template <typename Config>
        using memory_of = memory<state_of<Config>, workspace_of<Config>>;

        namespace detail {

            /// Things one after another: a plain recursive aggregate, so a chain's
            /// configurations, states and workspaces are trivially copyable and standard layout
            /// when their parts are (std::tuple is neither, and its constructor fills).
            template <typename... Ts>
            struct pack;
            template <>
            struct pack<> {};
            template <typename T>
            struct pack<T> {
                T first;
            };
            template <typename T, typename U, typename... Rest>
            struct pack<T, U, Rest...> {
                T                first;
                pack<U, Rest...> rest;
            };

            template <std::size_t I>
            struct pack_at {
                template <typename P>
                static constexpr auto& get(P& p) noexcept {
                    return pack_at<I - 1>::get(p.rest);
                }
            };
            template <>
            struct pack_at<0> {
                template <typename P>
                static constexpr auto& get(P& p) noexcept {
                    return p.first;
                }
            };

            template <typename T>
            constexpr pack<T> packed(const T& only) noexcept {
                return pack<T>{only};
            }
            template <typename T, typename U, typename... Rest>
            constexpr pack<T, U, Rest...> packed(const T& head, const U& next, const Rest&... rest) noexcept {
                return pack<T, U, Rest...>{head, packed(next, rest...)};
            }

            template <std::size_t I, typename... Ts>
            struct type_at {
                using type = std::tuple_element_t<I, std::tuple<Ts...>>;
            };

            /// A chain's member, admitted with catenae's own message before its traits are read.
            template <typename S>
            struct admit {
                static_assert(
                    !is_junction<S>::value,
                    "catenae: a junction is never inside a chain: feeding and fed_by join a chain to one (D19)");
                static_assert(
                    is_junction<S>::value || is_stage<S>::value,
                    "catenae: every member of a chain is a stage; a type opts in by specialising is_stage (D10)");
                static constexpr bool k_value = is_stage<S>::value && !is_junction<S>::value;
            };

            /// Every joint of a chain, formed when the chain's type is.
            template <std::size_t I, typename... Stages>
            struct joints : std::true_type {};
            template <std::size_t I, typename A, typename B, typename... Rest>
            struct joints<I, A, B, Rest...>
                : std::bool_constant<
                      joint<I, typename stage_traits<A>::output_port, typename stage_traits<B>::input_port>::value
                      && joints<I + 1, B, Rest...>::value> {};

            /// The arrival leaves of a stage: its own numbers, or a chain's stages' composed through.
            template <typename S>
            struct leaves_of {
                static constexpr std::size_t                 k_count = 1;
                static constexpr std::array<arrival_leaf, 1> get() noexcept {
                    using tr = stage_traits<S>;
                    return {{arrival_leaf{tr::rate_ratio, tr::input_group, tr::max_shortfall, tr::max_lead,
                                          tr::output_quantum}}};
                }
            };

            template <std::size_t N, std::size_t K>
            constexpr void append(std::array<arrival_leaf, N>& all, std::size_t& at,
                                  const std::array<arrival_leaf, K>& part) noexcept {
                for (const arrival_leaf& leaf : part) {
                    all[at] = leaf;
                    ++at;
                }
            }
            template <typename... Ss>
            constexpr std::array<arrival_leaf, (leaves_of<Ss>::k_count + ...)> concat_leaves() noexcept {
                std::array<arrival_leaf, (leaves_of<Ss>::k_count + ...)> all{};
                std::size_t                                              at = 0;
                (append(all, at, leaves_of<Ss>::get()), ...);
                return all;
            }

            template <typename... Ss>
            struct leaves_of<chain<Ss...>> {
                static constexpr std::size_t                       k_count = (leaves_of<Ss>::k_count + ...);
                static constexpr std::array<arrival_leaf, k_count> get() noexcept { return concat_leaves<Ss...>(); }
            };
            template <std::size_t Block, typename... Ss>
            struct leaves_of<blocked_chain<Block, Ss...>> {
                static constexpr std::size_t                       k_count = (leaves_of<Ss>::k_count + ...);
                static constexpr std::array<arrival_leaf, k_count> get() noexcept { return concat_leaves<Ss...>(); }
            };

            /// Where each stage's output goes inside one block: into a buffer of the workspace up
            /// to the writer, the writer into the caller's output, and the in-place stages after
            /// it over that output. A run of in-place stages shares the buffer its input is in.
            template <std::size_t N>
            struct buffer_plan {
                std::array<std::size_t, N> slot;  ///< the buffer a stage before the writer writes
                std::array<std::size_t, N> owner; ///< the stage that sizes each buffer
                std::size_t                slots; ///< how many buffers
            };

            template <std::size_t N>
            constexpr std::size_t writer_of(const std::array<bool, N>& in_place) noexcept {
                std::size_t first = N; // the first of the trailing run of in-place stages
                while (first > 0 && in_place[first - 1]) {
                    --first;
                }
                return first == 0 ? 0 : first - 1;
            }

            template <std::size_t N>
            constexpr buffer_plan<N> plan_buffers(const std::array<bool, N>& in_place, std::size_t writer) noexcept {
                buffer_plan<N> plan{};
                for (std::size_t i = 0; i < writer; ++i) {
                    if (i > 0 && in_place[i]) {
                        plan.slot[i] = plan.slot[i - 1];
                    }
                    else {
                        plan.slot[i]           = plan.slots;
                        plan.owner[plan.slots] = i;
                        ++plan.slots;
                    }
                }
                return plan;
            }

            /// The most frames each stage writes while the chain works on one block.
            template <std::size_t Block, typename... Ss>
            constexpr std::array<std::size_t, sizeof...(Ss)> bounds_of() noexcept {
                std::array<std::size_t, sizeof...(Ss)> bound{};
                std::size_t                            n = Block;
                std::size_t                            i = 0;
                ((n = Ss::max_outputs_for(n), bound[i] = n, ++i), ...);
                return bound;
            }

            /// For each stage, the product of the ratios of the stages after it: what carries its
            /// output frames to the chain's.
            template <typename... Ss>
            constexpr std::array<ratio, sizeof...(Ss)> ratios_after() noexcept {
                constexpr std::array<ratio, sizeof...(Ss)> rates = {{stage_traits<Ss>::rate_ratio...}};
                std::array<ratio, sizeof...(Ss)>           after{};
                ratio                                      carried{1, 1};
                for (std::size_t i = sizeof...(Ss); i > 0; --i) {
                    after[i - 1] = carried;
                    carried      = carried * rates[i - 1];
                }
                return after;
            }

            /// A chain's exact latency: each stage's carried to the output rate, summed.
            template <typename... Ss>
            constexpr ratio composed_exact_latency() noexcept {
                constexpr std::array<ratio, sizeof...(Ss)> own   = {{stage_traits<Ss>::exact_latency...}};
                constexpr std::array<ratio, sizeof...(Ss)> after = ratios_after<Ss...>();
                ratio                                      total{0, 1};
                for (std::size_t i = 0; i < sizeof...(Ss); ++i) {
                    total = total + own[i] * after[i];
                }
                return total;
            }

            /// What a chain of these stages, in blocks of Block, needs and holds.
            template <std::size_t Block, typename... Stages>
            struct chain_plan {
                static constexpr std::size_t                      k_count    = sizeof...(Stages);
                static constexpr std::array<bool, k_count>        k_in_place = {{stage_traits<Stages>::is_in_place...}};
                static constexpr std::size_t                      k_writer   = writer_of(k_in_place);
                static constexpr buffer_plan<k_count>             k_buffers  = plan_buffers(k_in_place, k_writer);
                static constexpr std::array<std::size_t, k_count> k_bounds   = bounds_of<Block, Stages...>();
                static constexpr std::array<ratio, k_count>       k_after    = ratios_after<Stages...>();
                static constexpr auto                             k_leaves   = concat_leaves<Stages...>();

                template <std::size_t I>
                using stage_t = typename type_at<I, Stages...>::type;
                template <std::size_t I>
                using in_port_t = typename stage_traits<stage_t<I>>::input_port;
                template <std::size_t I>
                using out_port_t = typename stage_traits<stage_t<I>>::output_port;
                template <std::size_t I>
                using buffer_t =
                    std::array<typename out_port_t<I>::value_type, k_bounds[I] * out_port_t<I>::values_per_frame>;

                /// The buffer of slot K, sized by the stage that owns it. An alias of one K, so the
                /// expansion below is of the alias alone: MSVC does not expand a pack inside a
                /// subscript of a static member (C3546).
                template <std::size_t K>
                using slot_buffer_t = buffer_t<k_buffers.owner[K]>;

                template <typename Seq>
                struct buffers_of;
                template <std::size_t... K>
                struct buffers_of<std::index_sequence<K...>> {
                    using type = pack<slot_buffer_t<K>...>;
                };
                using buffers = typename buffers_of<std::make_index_sequence<k_buffers.slots>>::type;
            };

        } // namespace detail

        /// @brief What a chain and a chain with its own block share: everything but the block.
        template <std::size_t Block, typename... Stages>
        class chain_body {
            static_assert(sizeof...(Stages) >= 1, "catenae: a chain has at least one stage");
            static_assert(Block >= 1, "catenae: a chain's block is at least one frame");
            static_assert((detail::admit<Stages>::k_value && ...), "catenae: a chain's members are stages");
            static_assert(detail::joints<0, Stages...>::value, "catenae: a chain's joints hold");

            using plan                           = detail::chain_plan<Block, Stages...>;
            static constexpr std::size_t k_count = sizeof...(Stages);
            template <std::size_t I>
            using stage_t = typename plan::template stage_t<I>;
            template <std::size_t I>
            using in_views_t = detail::views<typename plan::template in_port_t<I>>;
            template <std::size_t I>
            using out_views_t                      = detail::views<typename plan::template out_port_t<I>>;
            static constexpr std::uint32_t k_ready = 0x52454459u;

          public:
            /// The port the chain reads: its first stage's.
            using input_port = typename stage_traits<stage_t<0>>::input_port;
            /// The port the chain writes: its last stage's.
            using output_port = typename stage_traits<stage_t<k_count - 1>>::output_port;

          private:
            using in_views                             = detail::views<input_port>;
            using out_views                            = detail::views<output_port>;
            using in_value                             = typename input_port::value_type;
            using out_value                            = typename output_port::value_type;
            using in_view_t                            = typename input_port::input_view;
            using out_view_t                           = typename output_port::output_view;
            static constexpr std::size_t  k_leaf_count = plan::k_leaves.size();
            static constexpr detail::pace k_pace       = detail::composed_pace(plan::k_leaves.data(), k_leaf_count, 1);

          public:
            /// L/M: the product of the stages' ratios.
            static constexpr ratio rate_ratio = reduce(detail::composed_rate(plan::k_leaves.data(), k_leaf_count));
            /// The smallest n for which the frames reaching every stage are whole groups of it.
            static constexpr std::size_t input_group    = detail::composed_group(plan::k_leaves.data(), k_leaf_count);
            static constexpr ratio       max_shortfall  = k_pace.shortfall; ///< composed (D35)
            static constexpr ratio       max_lead       = k_pace.lead;      ///< composed (D35)
            static constexpr std::size_t output_quantum = k_pace.quantum;   ///< the q leaving the last stage
            /// n frames in always write exactly n L/M: derived from input_group, never declared.
            static constexpr bool is_frame_synchronous = input_group == 1;
            /// In place when every stage is.
            static constexpr bool is_in_place = (stage_traits<Stages>::is_in_place && ...);
            /// Exact when every stage's latency is.
            static constexpr bool has_exact_latency = (stage_traits<Stages>::has_exact_latency && ...);
            /// The exact latency in output frames, when has_exact_latency; 0 otherwise.
            static constexpr ratio exact_latency =
                has_exact_latency ? detail::composed_exact_latency<Stages...>() : ratio{0, 1};
            /// The frames of its input the chain works on at a time.
            static constexpr std::size_t block = Block;

            /// What one instance remembers: its stages' states, whether it was reset, and how far
            /// a flush has gone. Plain storage, so a static one is in .bss.
            struct state {
                detail::pack<state_of<Stages>...> stages;  ///< each stage's own
                std::uint32_t                     ready;   ///< set by reset, checked at the audit level
                std::uint32_t                     flushed; ///< zero frames a flush has fed so far
            };
            /// What a call needs and nothing keeps: the stages' workspaces, the buffers between
            /// them, and a block of the zeros a flush feeds. One per thread; never initialised.
            struct workspace {
                detail::pack<workspace_of<Stages>...>                      stages;  ///< each stage's own
                typename plan::buffers                                     buffers; ///< between the stages
                std::array<in_value, Block * input_port::values_per_frame> zeros;   ///< what a flush feeds
            };

            /// The chain of these configurations.
            explicit constexpr chain_body(const Stages&... stages) noexcept
                : m_stages(detail::packed(stages...)) {}

            /// Stage I's configuration.
            template <std::size_t I>
            constexpr const stage_t<I>& stage() const noexcept {
                return detail::pack_at<I>::get(m_stages);
            }
            /// Stage I's configuration, to change a parameter between calls.
            template <std::size_t I>
            constexpr stage_t<I>& stage() noexcept {
                return detail::pack_at<I>::get(m_stages);
            }
            /// Stage I's part of a state: where a meter's reading is read.
            template <std::size_t I>
            static constexpr const state_of<stage_t<I>>& stage_state(const state& st) noexcept {
                return detail::pack_at<I>::get(st.stages);
            }
            /// Stage I's part of a state.
            template <std::size_t I>
            static constexpr state_of<stage_t<I>>& stage_state(state& st) noexcept {
                return detail::pack_at<I>::get(st.stages);
            }

            /// Each stage resets its own part, in place, and the state is ready. Before the first
            /// process, and afterwards only between calls, on the thread that runs the chain.
            void reset(state& st) const noexcept {
                reset_from<0>(st);
                st.ready   = k_ready;
                st.flushed = 0;
            }

            /// The most frames a call of n frames writes, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return most_out<0>(n); }
            /// Exactly what the next call of n frames writes.
            constexpr std::size_t outputs_for(const state& st, std::size_t n) const noexcept {
                return out_from<0>(st, n);
            }
            /// The smallest n with outputs_for(n) >= k.
            constexpr std::size_t frames_needed(const state& st, std::size_t k) const noexcept {
                return need_from<k_count - 1>(st, k);
            }

            /// Each stage's latency carried to the output rate, summed.
            stage_latency latency() const noexcept {
                stage_latency total{0.0f, 0.0f};
                latency_from<0>(total);
                return total;
            }
            /// Zero input frames after which the output is silence within each stage's residue:
            /// each stage's tail plus what it needs to drain the stages after it.
            std::size_t tail_frames() const noexcept { return tail_back<k_count - 1>(0, tail::exact); }
            /// The same, from each stage's practical tail.
            std::size_t practical_tail_frames() const noexcept { return tail_back<k_count - 1>(0, tail::practical); }
            /// The largest of the stages' own residues, each at its own output: a chain does not
            /// carry one through later stages' gains (PLAN.md section 4).
            float flush_residue() const noexcept {
                float largest = 0.0f;
                residue_from<0>(largest);
                return largest;
            }

            /// The trusted form: the work a block at a time, no check. What an enclosing chain calls.
            std::size_t run(state& st, workspace& ws, in_view_t in, out_view_t out) const noexcept {
                const std::size_t frames = in_views::frames(in);
                std::size_t       done   = 0;
                std::size_t       made   = 0;
                while (done < frames) {
                    const std::size_t n = std::min(Block, frames - done);
                    made += step<0>(st, ws, in_views::slice(in, done, n), out, made);
                    done += n;
                }
                return made;
            }

            /// The caller's entry: whole frames in, room for what they write, then run.
            [[nodiscard]] std::size_t process(state& st, workspace& ws, in_view_t in, out_view_t out) const noexcept {
                detail::check_always(in_views::whole(in), check::input_not_whole_frames);
                const std::size_t frames = in_views::frames(in);
                detail::check_always(out_views::frames(out) >= outputs_for(st, frames), check::output_too_small);
                return enter(st, ws, in, out, frames);
            }
            /// The caller's entry, over one instance's memory.
            [[nodiscard]] std::size_t process(memory<state, workspace>& m, in_view_t in,
                                              out_view_t out) const noexcept {
                return process(m.state, m.workspace, in, out);
            }

            /// The caller's entry for fixed blocks: every size checked by the compiler.
            template <std::size_t N, std::size_t M, typename P = input_port,
                      std::enable_if_t<!detail::k_is_planar<P> && !detail::k_is_planar<output_port>, int> = 0>
            [[nodiscard]] std::size_t process(state& st, workspace& ws, const std::array<in_value, N>& in,
                                              std::array<out_value, M>& out) const noexcept {
                fixed_block<N, M>();
                return enter(st, ws, in_view_t(in), out_view_t(out), N / input_port::values_per_frame);
            }
            /// A fixed block in built-in arrays.
            template <std::size_t N, std::size_t M, typename P = input_port,
                      std::enable_if_t<!detail::k_is_planar<P> && !detail::k_is_planar<output_port>, int> = 0>
            [[nodiscard]] std::size_t process(state& st, workspace& ws, const in_value (&in)[N],
                                              out_value (&out)[M]) const noexcept {
                fixed_block<N, M>();
                return enter(st, ws, in_view_t(in), out_view_t(out), N / input_port::values_per_frame);
            }
            /// A fixed block in spans of static extent.
            template <std::size_t N, std::size_t M, typename P = input_port,
                      std::enable_if_t<N != dynamic_extent && M != dynamic_extent
                                           && !detail::k_is_planar<P> && !detail::k_is_planar<output_port>,
                                       int> = 0>
            [[nodiscard]] std::size_t process(state& st, workspace& ws, span<const in_value, N> in,
                                              span<out_value, M> out) const noexcept {
                fixed_block<N, M>();
                return enter(st, ws, in_view_t(in), out_view_t(out), N / input_port::values_per_frame);
            }
            /// A fixed block, over one instance's memory.
            template <std::size_t N, std::size_t M, typename P = input_port,
                      std::enable_if_t<!detail::k_is_planar<P> && !detail::k_is_planar<output_port>, int> = 0>
            [[nodiscard]] std::size_t process(memory<state, workspace>& m, const std::array<in_value, N>& in,
                                              std::array<out_value, M>& out) const noexcept {
                return process(m.state, m.workspace, in, out);
            }

            /// The end of the stream, resumable: feeds zeros until the output cannot take another
            /// burst or the tail is fed, and returns the frames written (which may be 0 while a
            /// block stage fills). It is finished when flush_remaining is 0.
            [[nodiscard]] std::size_t flush(state& st, workspace& ws, out_view_t out,
                                            tail which = tail::practical) const noexcept {
                detail::check_always(out_views::frames(out) >= max_outputs_for(1), check::flush_output_too_small);
                return drain(st, ws, out, which);
            }
            /// The end of the stream, over one instance's memory.
            [[nodiscard]] std::size_t flush(memory<state, workspace>& m, out_view_t out,
                                            tail which = tail::practical) const noexcept {
                return flush(m.state, m.workspace, out, which);
            }
            /// The end of the stream into a fixed block, which the compiler checks holds a burst.
            template <std::size_t M, typename P = output_port, std::enable_if_t<!detail::k_is_planar<P>, int> = 0>
            [[nodiscard]] std::size_t flush(state& st, workspace& ws, std::array<out_value, M>& out,
                                            tail which = tail::practical) const noexcept {
                static_assert(M >= max_outputs_for(1) * output_port::values_per_frame,
                              "catenae: a flush's output holds at least one burst, max_outputs_for(1) frames");
                return drain(st, ws, out_view_t(out), which);
            }
            /// Zero frames still to feed before the flush is finished.
            std::size_t flush_remaining(const state& st, tail which = tail::practical) const noexcept {
                const std::size_t total = which == tail::exact ? tail_frames() : practical_tail_frames();
                const std::size_t fed   = st.flushed;
                return fed >= total ? 0 : total - fed;
            }

          private:
            template <std::size_t N, std::size_t M>
            static constexpr void fixed_block() noexcept {
                static_assert(N % input_port::values_per_frame == 0,
                              "catenae: a fixed input block is a whole number of frames");
                static_assert((N / input_port::values_per_frame) % input_group == 0,
                              "catenae: a fixed block must be a whole number of the chain's input groups; "
                              "wrap the chain in steady<>");
                static_assert(M >= max_outputs_for(N / input_port::values_per_frame) * output_port::values_per_frame,
                              "catenae: a fixed output block holds max_outputs_for of the input block");
            }

            std::size_t enter(state& st, workspace& ws, in_view_t in, out_view_t out,
                              std::size_t frames) const noexcept {
                detail::check_audit(st.ready == k_ready, check::state_not_reset);
                if (frames > 0) {
                    st.flushed = 0; // new input: a later flush starts its tail again
                }
                return run(st, ws, in, out);
            }

            std::size_t drain(state& st, workspace& ws, out_view_t out, tail which) const noexcept {
                detail::check_audit(st.ready == k_ready, check::state_not_reset);
                std::fill(ws.zeros.begin(), ws.zeros.end(), in_value{});
                const std::size_t total = which == tail::exact ? tail_frames() : practical_tail_frames();
                const std::size_t room  = out_views::frames(out);
                std::size_t       made  = 0;
                while (st.flushed < total) {
                    // The most zeros whose output fits: outputs_for is exact and non-decreasing.
                    std::size_t       lo  = 0;
                    const std::size_t fed = st.flushed;
                    std::size_t       hi  = std::min(Block, total - fed);
                    while (lo < hi) {
                        const std::size_t mid = lo + (hi - lo + 1) / 2;
                        if (outputs_for(st, mid) <= room - made) {
                            lo = mid;
                        }
                        else {
                            hi = mid - 1;
                        }
                    }
                    if (lo == 0) {
                        break;
                    }
                    made += step<0>(st, ws, in_views::in_of(ws.zeros.data(), Block, lo), out, made);
                    st.flushed += detail::narrow<std::uint32_t>(lo);
                }
                return made;
            }

            /// One block through stage I and those after it; `at` is where the block's output
            /// starts in the caller's output.
            template <std::size_t I>
            std::size_t step(state& st, workspace& ws, typename in_views_t<I>::in_view in, out_view_t out,
                             std::size_t at) const noexcept {
                const stage_t<I>&     configuration = stage<I>();
                state_of<stage_t<I>>& mine          = detail::pack_at<I>::get(st.stages);
                auto&                 work          = detail::pack_at<I>::get(ws.stages);
                const std::size_t     arriving      = in_views_t<I>::frames(in);
                if constexpr (I < plan::k_writer) {
                    // Into a buffer of the workspace: its own, or the one its input is in.
                    constexpr std::size_t cap    = plan::k_bounds[plan::k_buffers.owner[plan::k_buffers.slot[I]]];
                    auto&                 buffer = detail::pack_at<plan::k_buffers.slot[I]>::get(ws.buffers);
                    const std::size_t     made =
                        configuration.run(mine, work, in, out_views_t<I>::out_of(buffer.data(), cap, cap));
                    detail::check_audit(made <= stage_t<I>::max_outputs_for(arriving), check::outputs_exceeded);
                    return step<I + 1>(st, ws, out_views_t<I>::in_of(buffer.data(), cap, made), out, at);
                }
                else {
                    // The writer into the caller's output; an in-place stage after it over that output.
                    const std::size_t room = I == plan::k_writer ? out_views::frames(out) - at : arriving;
                    const std::size_t made = configuration.run(mine, work, in, out_views::slice(out, at, room));
                    detail::check_audit(made <= stage_t<I>::max_outputs_for(arriving), check::outputs_exceeded);
                    if constexpr (I + 1 < k_count) {
                        return step<I + 1>(st, ws, out_views::as_in(out_views::slice(out, at, made)), out, at);
                    }
                    else {
                        return made;
                    }
                }
            }

            template <std::size_t I>
            void reset_from(state& st) const noexcept {
                stage_traits<stage_t<I>>::reset(stage<I>(), detail::pack_at<I>::get(st.stages));
                if constexpr (I + 1 < k_count) {
                    reset_from<I + 1>(st);
                }
            }
            template <std::size_t I>
            static constexpr std::size_t most_out(std::size_t n) noexcept {
                const std::size_t made = stage_t<I>::max_outputs_for(n);
                if constexpr (I + 1 < k_count) {
                    return most_out<I + 1>(made);
                }
                else {
                    return made;
                }
            }
            template <std::size_t I>
            constexpr std::size_t out_from(const state& st, std::size_t n) const noexcept {
                const std::size_t made = stage<I>().outputs_for(detail::pack_at<I>::get(st.stages), n);
                if constexpr (I + 1 < k_count) {
                    return out_from<I + 1>(st, made);
                }
                else {
                    return made;
                }
            }
            template <std::size_t I>
            constexpr std::size_t need_from(const state& st, std::size_t k) const noexcept {
                const std::size_t n = stage<I>().frames_needed(detail::pack_at<I>::get(st.stages), k);
                if constexpr (I > 0) {
                    return need_from<I - 1>(st, n);
                }
                else {
                    return n;
                }
            }
            template <std::size_t I>
            void latency_from(stage_latency& total) const noexcept {
                const stage_latency own    = stage<I>().latency();
                constexpr ratio     after  = plan::k_after[I];
                const float         factor = static_cast<float>(after.up) / static_cast<float>(after.down);
                total.nominal += own.nominal * factor;
                total.deviation += own.deviation * factor;
                if constexpr (I + 1 < k_count) {
                    latency_from<I + 1>(total);
                }
            }
            template <std::size_t I>
            std::size_t tail_back(std::size_t after, tail which) const noexcept {
                using tr = stage_traits<stage_t<I>>;
                const std::size_t own =
                    which == tail::exact ? stage<I>().tail_frames() : stage<I>().practical_tail_frames();
                const std::size_t total =
                    own + detail::most_frames_in(tr::rate_ratio, tr::max_shortfall, tr::max_lead, after);
                if constexpr (I > 0) {
                    return tail_back<I - 1>(total, which);
                }
                else {
                    return total;
                }
            }
            template <std::size_t I>
            void residue_from(float& largest) const noexcept {
                const float own = stage<I>().flush_residue();
                largest         = own > largest ? own : largest;
                if constexpr (I + 1 < k_count) {
                    residue_from<I + 1>(largest);
                }
            }

            detail::pack<Stages...> m_stages; // the configurations: all a chain object holds (D54)
        };

        /// @brief A chain of stages, in blocks of default_block frames. `a | b | c` builds one.
        template <typename... Stages>
        class chain : public chain_body<default_block, Stages...> {
          public:
            using chain_body<default_block, Stages...>::chain_body;
        };
        /// `chain(s)` is a chain of one; `chain(a, b)` of two.
        template <typename... Stages>
        chain(Stages...) -> chain<Stages...>;

        /// @brief A chain that works in blocks of its own length; with_block makes one. As an
        /// operand of `|` it is one stage, not flattened, and keeps its block.
        template <std::size_t Block, typename... Stages>
        class blocked_chain : public chain_body<Block, Stages...> {
          public:
            using chain_body<Block, Stages...>::chain_body;
        };

        template <typename... Stages>
        struct is_stage<chain<Stages...>> : std::true_type {};
        template <typename... Stages>
        struct is_chain<chain<Stages...>> : std::true_type {};
        template <std::size_t Block, typename... Stages>
        struct is_stage<blocked_chain<Block, Stages...>> : std::true_type {};
        template <std::size_t Block, typename... Stages>
        struct is_chain<blocked_chain<Block, Stages...>> : std::true_type {};

        namespace detail {
            template <typename T>
            struct is_flat_chain : std::false_type {};
            template <typename... Stages>
            struct is_flat_chain<chain<Stages...>> : std::true_type {};
            /// An operand that `|` keeps whole: a stage that is not a plain chain.
            template <typename T>
            inline constexpr bool k_is_whole = is_stage<T>::value && !is_flat_chain<T>::value;

            template <typename... As, typename... Bs, std::size_t... I, std::size_t... J>
            constexpr chain<As..., Bs...> joined(const chain<As...>& a, const chain<Bs...>& b,
                                                 std::index_sequence<I...>, std::index_sequence<J...>) noexcept {
                return chain<As..., Bs...>(a.template stage<I>()..., b.template stage<J>()...);
            }
        } // namespace detail

        /// Two chains, flattened: `(a | b) | (c | d)` is `chain<A, B, C, D>`.
        template <typename... As, typename... Bs>
        constexpr chain<As..., Bs...> operator|(const chain<As...>& a, const chain<Bs...>& b) noexcept {
            return detail::joined(a, b, std::index_sequence_for<As...>{}, std::index_sequence_for<Bs...>{});
        }
        /// A chain and a stage after it.
        template <typename... As, typename B, std::enable_if_t<detail::k_is_whole<B>, int> = 0>
        constexpr chain<As..., B> operator|(const chain<As...>& a, const B& b) noexcept {
            return a | chain<B>(b);
        }
        /// A stage and a chain after it.
        template <typename A, typename... Bs, std::enable_if_t<detail::k_is_whole<A>, int> = 0>
        constexpr chain<A, Bs...> operator|(const A& a, const chain<Bs...>& b) noexcept {
            return chain<A>(a) | b;
        }
        /// Two stages.
        template <typename A, typename B, std::enable_if_t<detail::k_is_whole<A> && detail::k_is_whole<B>, int> = 0>
        constexpr chain<A, B> operator|(const A& a, const B& b) noexcept {
            return chain<A, B>(a, b);
        }
        /// One side a stage and the other not: refused with catenae's own message.
        template <typename A, typename B, std::enable_if_t<is_stage<A>::value != is_stage<B>::value, int> = 0>
        constexpr void operator|(const A&, const B&) noexcept {
            static_assert(!(is_junction<A>::value || is_junction<B>::value) || detail::k_never<A, B>,
                          "catenae: a junction is never inside a chain: feeding and fed_by join a chain to one (D19)");
            static_assert(is_junction<A>::value || is_junction<B>::value || detail::k_never<A, B>,
                          "catenae: both sides of | are stages; a type opts in by specialising is_stage (D10)");
        }

        namespace detail {
            template <std::size_t Block, std::size_t Old, typename... Ss, std::size_t... I>
            constexpr blocked_chain<Block, Ss...> reblock_from(const chain_body<Old, Ss...>& c,
                                                               std::index_sequence<I...>) noexcept {
                return blocked_chain<Block, Ss...>(c.template stage<I>()...);
            }
        } // namespace detail

        /// A chain that works in blocks of Block frames of its input.
        template <std::size_t Block, typename... Ss>
        constexpr blocked_chain<Block, Ss...> with_block(const chain<Ss...>& c) noexcept {
            return detail::reblock_from<Block>(c, std::index_sequence_for<Ss...>{});
        }
        /// The same chain in another block.
        template <std::size_t Block, std::size_t Old, typename... Ss>
        constexpr blocked_chain<Block, Ss...> with_block(const blocked_chain<Old, Ss...>& c) noexcept {
            return detail::reblock_from<Block>(c, std::index_sequence_for<Ss...>{});
        }
        /// One stage, as a chain of one in blocks of Block frames.
        template <std::size_t Block, typename S, std::enable_if_t<detail::k_is_whole<S>, int> = 0>
        constexpr blocked_chain<Block, S> with_block(const S& s) noexcept {
            return blocked_chain<Block, S>(s);
        }

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_CHAIN_H
