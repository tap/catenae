/// @file feeding.h
/// @brief `feeding`: a chain that writes into a junction, on the junction's producer thread
/// (D19, PLAN.md 6.1).
///
/// | Contract   |                                                                                |
/// |------------|--------------------------------------------------------------------------------|
/// | Joins      | a chain's output port to a junction's sink port: the same type (a joint)      |
/// | Memory     | `feeding_state<Chain>`, the chain's state; `feeding_workspace<Chain>`, its workspace and one block in
/// transit | | Pushes     | one a block of the chain's: up to `max_outputs_for(block)` frames each       | | Checks |
/// as `process`: whole frames in, and the chain reset at the audit level         | | Returns    | the frames the
/// junction kept; fewer than the chain wrote is an overrun       | | Rationale  | D19, D27, D52 |
///
/// @req{CAT-FEEDING-001} `feeding` runs the chain over its input a block at a time and pushes
/// what each block wrote, so the junction holds what the chain alone would write, in order; it
/// returns the frames the junction kept. A chain whose output port is not the junction's sink
/// port does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FEEDING_H
#define CATENAE_FEEDING_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/views.h"
#include "catenae/fwd.h"
#include "catenae/junction_traits.h"
#include "catenae/port.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// What `feeding` needs while a call runs: the chain's workspace and one block of its
        /// output, on its way to the junction.
        template <typename Chain>
        struct feeding_scratch {
            using output_port = typename Chain::output_port; ///< the chain's output, the junction's sink
            /// The most frames one block of the chain writes: the largest push it makes.
            static constexpr std::size_t largest_push = Chain::max_outputs_for(Chain::block);

            workspace_of<Chain> chain; ///< the chain's own
            std::array<typename output_port::value_type, largest_push * output_port::values_per_frame>
                made; ///< one block's output, in transit
        };

        /// The producer side's state: the chain's own.
        template <typename Chain>
        using feeding_state = state_of<std::remove_cv_t<Chain>>;
        /// The producer side's workspace.
        template <typename Chain>
        using feeding_workspace = feeding_scratch<std::remove_cv_t<Chain>>;

        /// @brief Runs `chain` over `in` a block at a time and pushes what it writes into
        /// `junction`, on the junction's producer thread. Returns the frames the junction kept.
        template <typename Junction, typename Chain>
        [[nodiscard]] std::size_t feeding(const Junction& junction, state_of<Junction>& junction_state,
                                          const Chain& chain, feeding_state<Chain>& st, feeding_workspace<Chain>& ws,
                                          typename Chain::input_port::input_view in) noexcept {
            using traits    = junction_traits<Junction>;
            using out_port  = typename Chain::output_port;
            using in_views  = detail::views<typename Chain::input_port>;
            using out_views = detail::views<out_port>;
            static_assert(traits::opted_in,
                          "catenae: feeding pushes into a junction, which opts in through is_junction");
            static_assert(joint<0, out_port, typename traits::sink_port>::value, "the chain's output is the sink port");
            constexpr std::size_t cap = feeding_scratch<std::remove_cv_t<Chain>>::largest_push;
            detail::check_always(in_views::whole(in), check::input_not_whole_frames);
            const std::size_t frames = in_views::frames(in);
            std::size_t       kept   = 0;
            for (std::size_t done = 0; done < frames; done += Chain::block) {
                const std::size_t n    = std::min(Chain::block, frames - done);
                const std::size_t made = chain.process(st, ws.chain, in_views::slice(in, done, n),
                                                       out_views::out_of(ws.made.data(), cap, cap));
                kept += junction.push(junction_state, out_views::in_of(ws.made.data(), cap, made));
            }
            return kept;
        }

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FEEDING_H
