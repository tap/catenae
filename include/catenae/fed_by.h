/// @file fed_by.h
/// @brief `fed_by`: a chain that reads from a junction and delivers exactly the frames asked
/// for, in at most one pull a request (D19, D35, D52, PLAN.md 6.1).
///
/// | Contract   |                                                                                 |
/// |------------|---------------------------------------------------------------------------------|
/// | Joins      | a junction's source port to a chain's input port: the same type (a joint)      |
/// | Memory     | `fed_by_state<Chain>`: the chain's state, the holdover, `starved`;              |
/// |            | `fed_by_workspace<Chain, MaxRequest>`: its workspace and one pull's staging     |
/// | Pulls      | at most one a request, none when the holdover covers it; at most `largest_pull` |
/// |            | frames, the most a request needs from the states fed_by reaches                 |
/// | Checks     | the junction serves `largest_pull` (compile time); whole frames and at most     |
/// |            | `MaxRequest` (boundary)                                                         |
/// | Returns    | the frames asked for, always; input the junction supplied as silence counts in  |
/// |            | `starved`                                                                       |
/// | End        | `flush`: the holdover, then the chain's tail, pulling nothing; then `flushed`   |
/// | Limits     | an interleaved output port: a planar one arrives with a junction that has one  |
/// | Rationale  | D19, D35, D52                                                                   |
///
/// @req{CAT-FED-BY-001} `fed_by` fills its output with exactly the frames asked for, which are
/// what the chain writes from the junction's frames, in order, and holds what one pull's last
/// block writes beyond the request for the next request.
/// @req{CAT-FED-BY-002} A request makes at most one pull on the junction, and none when the
/// holdover covers it: behind a 128-frame block stage, 64-frame requests pull 128, nothing,
/// 128, nothing; behind a chain up by 4, 102-frame requests pull 26, 25, 26, 25; a request of
/// 700 frames up by 4 pulls 175 in one pull.
/// @req{CAT-FED-BY-003} `fed_by_workspace<Chain, MaxRequest>::largest_pull` is the most input
/// frames a request of `MaxRequest` needs from any state `fed_by` reaches. The chain has taken
/// the fewest frames whose output from rest (`outputs_from_rest`) reaches what was delivered,
/// so the pull is T(D + MaxRequest) - T(D) at the frames delivered so far, D, and it repeats
/// with the chain's output per input group above D = 0: enumerated over that one period at
/// compile time (at 64-frame requests, 128 behind a 128-frame block and 256 behind two
/// decimators by 2 that write on a pair's first frame, where the bound from any state is 191
/// and 259). Where the count from rest is not periodic
/// in the input group, it is that bound: the request plus `max_shortfall` and `max_lead`, at the
/// input rate. A junction whose `serves(largest_pull, 1)` is false does not compile; a request
/// above `MaxRequest`, or not whole frames, is a boundary failure; the input frames a pull got
/// as silence are added to `starved`.
/// @req{CAT-FED-BY-004} `flush(chain, st, ws, out)` ends the stream on the pull side and pulls
/// nothing: first what the holdover keeps, then the chain's resumable flush, in calls whose
/// output holds a burst (`check::flush_output_too_small`, every build). `flushed(chain, st)` is
/// true once both are out; the requests and the flush together are what the chain writes over
/// the frames the junction gave, zero-padded by its tail, in calls of any size.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FED_BY_H
#define CATENAE_FED_BY_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/arrival.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/views.h"
#include "catenae/fwd.h"
#include "catenae/junction_traits.h"
#include "catenae/port.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {

            /// T(d): the fewest input frames whose output from rest reaches d frames, by
            /// bisection below `high`, which reaches it.
            template <typename Chain>
            constexpr std::size_t inputs_reaching(std::size_t d, std::size_t high) noexcept {
                if (d == 0) {
                    return 0;
                }
                std::size_t low = 0; // writes fewer than d; `high` writes at least d
                while (high - low > 1) {
                    const std::size_t mid = low + (high - low) / 2;
                    if (Chain::outputs_from_rest(mid) >= d) {
                        high = mid;
                    }
                    else {
                        low = mid;
                    }
                }
                return high;
            }

            /// The most input frames one request of k frames needs behind fed_by. After every
            /// pull the chain has taken T(D), D the frames delivered so far (a pull takes the
            /// fewest that reach the request), so a request of k pulls T(D + k) - T(D), largest at
            /// k. With G the input group and P its output, T(D + P) = T(D) + G for D >= 1 when the
            /// count from rest repeats, O(t + G) = O(t) + P: so D from 0 to P is every case. A
            /// count that does not repeat is bounded from any state instead.
            template <typename Chain>
            constexpr std::size_t reachable_pull(std::size_t k) noexcept {
                const std::size_t bound = most_frames_in(Chain::rate_ratio, Chain::max_shortfall, Chain::max_lead, k);
                constexpr std::size_t group = Chain::input_group;
                constexpr std::size_t per_group =
                    narrow<std::size_t>(std::uint64_t{group} * Chain::rate_ratio.up / Chain::rate_ratio.down);
                if (per_group == 0) {
                    return bound;
                }
                for (std::size_t t = 0; t < group; ++t) {
                    if (Chain::outputs_from_rest(t + group) != Chain::outputs_from_rest(t) + per_group) {
                        return bound;
                    }
                }
                std::size_t largest = 0;
                for (std::size_t d = 0; d <= per_group; ++d) {
                    // O(G j) = P j, so G ceil(d / P) reaches d.
                    const std::size_t from = inputs_reaching<Chain>(d, group * ((d + per_group - 1) / per_group));
                    const std::size_t to = inputs_reaching<Chain>(d + k, group * ((d + k + per_group - 1) / per_group));
                    largest              = std::max(largest, to - from);
                }
                return largest;
            }

        } // namespace detail

        /// What the pull side remembers: the chain's state, what a request's last block wrote
        /// beyond it, and how many input frames the junction has supplied as silence.
        template <typename Chain>
        struct fed_by_memory {
            using output_port = typename Chain::output_port; ///< the chain's output, the request's port
            static_assert(
                !detail::k_is_planar<output_port>,
                "catenae: fed_by writes an interleaved port; a planar one arrives with a junction that has one");
            /// The most frames a request can be overshot by: one more input frame writes at most
            /// max_outputs_for(1), and the request was not yet met without it.
            static constexpr std::size_t holdover_frames = Chain::max_outputs_for(1) - 1;

            state_of<Chain> chain; ///< the chain's own
            /// What the last block wrote beyond its request (at least one frame of room).
            std::array<typename output_port::value_type,
                       (holdover_frames == 0 ? 1 : holdover_frames) * output_port::values_per_frame>
                        holdover;
            std::size_t held;    ///< frames waiting in the holdover
            std::size_t starved; ///< input frames the junction supplied as silence, since reset
        };

        /// What the pull side needs while a call runs: the chain's workspace, the staging for one
        /// pull, and one block of the chain's output.
        template <typename Chain, std::size_t MaxRequest>
        struct fed_by_scratch {
            static_assert(MaxRequest != 0, "catenae: fed_by's largest request is at least one frame");
            using input_port  = typename Chain::input_port;  ///< the junction's source, the chain's input
            using output_port = typename Chain::output_port; ///< the chain's output, the request's port
            /// The most input frames one request needs, from the states fed_by reaches: its single
            /// pull's size (CAT-FED-BY-003).
            static constexpr std::size_t largest_pull = detail::reachable_pull<Chain>(MaxRequest);
            /// The most frames a request asks for.
            static constexpr std::size_t largest_request = MaxRequest;

            workspace_of<Chain> chain; ///< the chain's own
            std::array<typename input_port::value_type, largest_pull * input_port::values_per_frame>
                staging; ///< one pull's frames
            std::array<typename output_port::value_type,
                       Chain::max_outputs_for(Chain::block) * output_port::values_per_frame>
                made; ///< the last block's output
        };

        /// The pull side's state.
        template <typename Chain>
        using fed_by_state = fed_by_memory<std::remove_cv_t<Chain>>;
        /// The pull side's workspace, for requests of up to `MaxRequest` frames.
        template <typename Chain, std::size_t MaxRequest>
        using fed_by_workspace = fed_by_scratch<std::remove_cv_t<Chain>, MaxRequest>;

        namespace detail {
            /// Up to `want` frames from the holdover into `out`, the rest moved to its front;
            /// returns the frames given. It never holds more than its own room, which bounding
            /// `held` by it tells the compiler too: inlined, GCC 13 cannot see the invariant and
            /// calls the copies out of bounds (-Warray-bounds).
            template <typename Chain>
            std::size_t from_holdover(fed_by_memory<Chain>& st, span<typename Chain::output_port::value_type> out,
                                      std::size_t want) noexcept {
                constexpr std::size_t width = Chain::output_port::values_per_frame;
                constexpr std::size_t room =
                    std::tuple_size<std::remove_reference_t<decltype(st.holdover)>>::value / width;
                check_audit(st.held <= room, check::invariant);
                const std::size_t held = std::min(st.held, room);
                const std::size_t done = std::min(held, want);
                std::copy(st.holdover.begin(), st.holdover.begin() + static_cast<std::ptrdiff_t>(done * width),
                          out.begin());
                std::copy(st.holdover.begin() + static_cast<std::ptrdiff_t>(done * width),
                          st.holdover.begin() + static_cast<std::ptrdiff_t>(held * width), st.holdover.begin());
                st.held = held - done;
                return done;
            }
        } // namespace detail

        /// The pull side at rest: the chain reset, nothing held, nothing starved.
        template <typename Chain>
        void reset(const Chain& chain, fed_by_memory<Chain>& st) noexcept {
            chain.reset(st.chain);
            st.held    = 0;
            st.starved = 0;
        }

        /// @brief Fills `out` with exactly the frames asked for: from the holdover, then from at
        /// most one pull on `junction` run through `chain`. Returns the frames asked for.
        template <typename Junction, typename Chain, std::size_t MaxRequest>
        [[nodiscard]] std::size_t fed_by(const Junction& junction, state_of<Junction>& junction_state,
                                         const Chain& chain, fed_by_memory<Chain>& st,
                                         fed_by_scratch<Chain, MaxRequest>&            ws,
                                         span<typename Chain::output_port::value_type> out) noexcept {
            using traits                = junction_traits<Junction>;
            using scratch               = fed_by_scratch<Chain, MaxRequest>;
            using in_port               = typename Chain::input_port;
            using out_value             = typename Chain::output_port::value_type;
            using in_views              = detail::views<in_port>;
            constexpr std::size_t width = Chain::output_port::values_per_frame;
            constexpr std::size_t cap   = Chain::max_outputs_for(Chain::block);
            static_assert(traits::opted_in, "catenae: fed_by pulls from a junction, which opts in through is_junction");
            static_assert(joint<0, typename traits::source_port, in_port>::value,
                          "the source port is the chain's input");
            static_assert(traits::serves(scratch::largest_pull, 1),
                          "catenae: the junction does not serve this pull side: its target is below the largest pull "
                          "plus its largest push, less one");
            detail::check_always(out.size() % width == 0, check::input_not_whole_frames);
            const std::size_t want = out.size() / width;
            detail::check_always(want <= MaxRequest, check::request_too_large);

            // The holdover first: a request it covers makes no pull. It never holds more than its
            // own room, which bounding `held` by it tells the compiler too: inlined, GCC 13 cannot
            // see the invariant and calls the copies out of bounds (-Warray-bounds).
            std::size_t done = detail::from_holdover(st, out, want);
            if (done == want) {
                return want;
            }

            // One pull of exactly the frames the rest of the request needs.
            const std::size_t n = chain.frames_needed(st.chain, want - done);
            detail::check_audit(n <= scratch::largest_pull, check::pull_above_staging);
            const std::size_t real =
                junction.pull(junction_state, in_views::out_of(ws.staging.data(), scratch::largest_pull, n));
            st.starved += n - real;

            // All but the last block's worth goes straight to `out`: n is the fewest that reach the
            // request, so fewer cannot overshoot it. The last block may, into the holdover.
            const std::size_t last  = std::min(n, Chain::block);
            const std::size_t first = n - last;
            done += chain.process(st.chain, ws.chain, in_views::in_of(ws.staging.data(), scratch::largest_pull, first),
                                  out.subspan(done * width, (want - done) * width));
            const std::size_t made = chain.process(
                st.chain, ws.chain,
                in_views::slice(in_views::in_of(ws.staging.data(), scratch::largest_pull, n), first, last),
                span<out_value>(ws.made.data(), cap * width));
            const std::size_t taken = std::min(made, want - done);
            std::copy(ws.made.begin(), ws.made.begin() + static_cast<std::ptrdiff_t>(taken * width),
                      out.begin() + static_cast<std::ptrdiff_t>(done * width));
            st.held = made - taken; // the overshoot waits for the next request
            detail::check_audit(st.held <= fed_by_memory<Chain>::holdover_frames, check::invariant);
            std::copy(ws.made.begin() + static_cast<std::ptrdiff_t>(taken * width),
                      ws.made.begin() + static_cast<std::ptrdiff_t>(made * width), st.holdover.begin());
            detail::check_audit(done + taken == want, check::invariant);
            return want;
        }

        /// @brief The end of the stream on the pull side, resumable, pulling nothing: what the
        /// holdover keeps, then the chain's flush (its tail of zeros), as much as `out` takes.
        /// Returns the frames written; `out` holds at least a burst, max_outputs_for(1) frames.
        template <typename Chain, std::size_t MaxRequest>
        [[nodiscard]] std::size_t
        flush(const Chain& chain, fed_by_memory<Chain>& st, fed_by_scratch<Chain, MaxRequest>& ws,
              span<typename Chain::output_port::value_type> out, tail which = tail::practical) noexcept {
            constexpr std::size_t width = Chain::output_port::values_per_frame;
            constexpr std::size_t burst = Chain::max_outputs_for(1);
            detail::check_always(out.size() % width == 0, check::input_not_whole_frames);
            const std::size_t want = out.size() / width;
            detail::check_always(want >= burst, check::flush_output_too_small);
            const std::size_t done = detail::from_holdover(st, out, want);
            if (st.held != 0 || want - done < burst) {
                return done; // the holdover filled it, or what is left cannot take a burst
            }
            return done + chain.flush(st.chain, ws.chain, out.subspan(done * width), which);
        }

        /// Whether the pull side's flush is finished: nothing held, and the chain's tail fed.
        template <typename Chain>
        [[nodiscard]] bool flushed(const Chain& chain, const fed_by_memory<Chain>& st,
                                   tail which = tail::practical) noexcept {
            return st.held == 0 && chain.flush_remaining(st.chain, which) == 0;
        }

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FED_BY_H
