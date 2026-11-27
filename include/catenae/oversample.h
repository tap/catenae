/// @file oversample.h
/// @brief `oversample<Factor, Stage>`: up by Factor, a stage at the inner rate, down by Factor,
/// with the delay that makes the latency a whole number of frames (D39, PLAN.md 4).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | the stage's `port<S, Ch>`, in and out                                        |
/// | Stage      | 1:1 and frame-synchronous, one port in and out, in a resample format          |
/// | Factor     | 2: `up2<Ch>`, the stage, `down2<Ch>`; 4: `up2<Ch>`, `up2<Ch, 2>`, the stage, |
/// |            | `down2<Ch, 2>`, `down2<Ch>`: the inner pair relaxed at design divisor 2      |
/// | Delay      | the fewest frames at an inner rate that make the latency whole, from the parts' |
/// |            | exact latencies (the stage's taken as 0 when it has none): none at 2 times    |
/// |            | around a stage whose latency is even, one frame at the doubled rate at 4 times |
/// |            | around one of none, where the bare chain is 47/2 frames                      |
/// | Latency    | exact and whole when the stage's is exact: 19 frames at 2 times, 24 at 4     |
/// |            | around a stage of none; otherwise the parts' nominal, the rate changers whole |
/// | Ratio      | 1:1, frame-synchronous: the composition of its parts (a chain in blocks of 64) |
/// | Tail       | the chain's: its parts' tails composed                                       |
/// | Rationale  | D35, D39, D43, PLAN.md 4, 9.4                                                  |
///
/// @req{CAT-OVERSAMPLE-001} `oversample<Factor, Stage>` is its parts in order with the card's
/// delay: 1:1, frame-synchronous, its exact latency whole (19 frames at 2 times and 24 at 4
/// around a stage of no latency), and its output the same bits for any chunking.
/// @req{CAT-OVERSAMPLE-002} Its impulse response through a linear stage is symmetric about its
/// latency, so a `parallel` dry arm aligned by type lines up with it.
/// @req{CAT-OVERSAMPLE-003} A factor other than 2 or 4, a stage that changes the rate or is not
/// frame-synchronous, and a latency no inner delay makes whole do not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_OVERSAMPLE_H
#define CATENAE_OVERSAMPLE_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/resample.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// The fewest frames, at the lowest inner rate that can express it, that bring a
            /// latency `t` (base-rate frames) up to a whole number: {frames, rate}; {0, 1} when it
            /// is whole, {0, 0} when no rate from 2 to Factor can.
            struct oversample_delay {
                std::size_t frames;
                std::size_t rate;
            };
            constexpr oversample_delay whole_by(ratio t, std::size_t factor) noexcept {
                const ratio r = reduce(t);
                if (r.down == 1) {
                    return {0, 1};
                }
                const ratio missing = reduce(ratio{(r.up / r.down + 1) * r.down - r.up, r.down});
                for (std::size_t rate = 2; rate <= factor; rate *= 2) {
                    if (rate % missing.down == 0) {
                        return {missing.up * (rate / missing.down), rate};
                    }
                }
                return {0, 0};
            }

            /// An oversampler's parts and its delay.
            template <std::size_t Factor, typename Stage>
            struct oversample_plan {
                static_assert(Factor == 2 || Factor == 4, "catenae: oversample is by 2 or by 4");
                using traits = stage_traits<Stage>;
                static_assert(traits::opted_in, "catenae: oversample runs a stage");
                static_assert(traits::rate_ratio == (ratio{1, 1}) && traits::is_frame_synchronous,
                              "catenae: oversample's stage is 1:1 and frame-synchronous");
                static_assert(std::is_same<typename traits::input_port, typename traits::output_port>::value,
                              "catenae: oversample's stage reads and writes one port");
                using port_t                      = typename traits::input_port;
                using s_t                         = typename port_t::format;
                static constexpr std::size_t k_ch = port_t::channels;
                static_assert(port_t::arrangement == layout::interleaved, "catenae: oversample is interleaved");

                using up_outer   = basic_up2<s_t, k_ch, 1>;
                using up_inner   = basic_up2<s_t, k_ch, 2>;
                using down_inner = basic_down2<s_t, k_ch, 2>;
                using down_outer = basic_down2<s_t, k_ch, 1>;

                /// Part P's exact latency in base-rate frames, P writing at `rate` times the base.
                template <typename P>
                static constexpr ratio at_base(std::uint32_t rate) noexcept {
                    return stage_traits<P>::exact_latency * ratio{1, rate};
                }
                /// The stage's, at the inner rate: 0 when it has none.
                static constexpr ratio k_stage =
                    traits::has_exact_latency ? at_base<Stage>(static_cast<std::uint32_t>(Factor)) : ratio{0, 1};
                /// The bare chain's latency, base-rate frames.
                static constexpr ratio k_bare = Factor == 2 ? at_base<up_outer>(2) + k_stage + at_base<down_outer>(1)
                                                            : at_base<up_outer>(2) + at_base<up_inner>(4) + k_stage
                                                                  + at_base<down_inner>(2) + at_base<down_outer>(1);
                static constexpr oversample_delay k_delay = whole_by(k_bare, Factor);
                static_assert(k_delay.rate != 0,
                              "catenae: no delay at an inner rate makes this oversampler's latency whole: the stage's "
                              "own latency is a fraction of an inner frame");

                /// The delay at an inner rate, when there is one.
                using delay_t                    = basic_delay<s_t, k_ch, (k_delay.frames == 0 ? 1 : k_delay.frames)>;
                static constexpr bool k_at_inner = k_delay.frames != 0 && k_delay.rate == Factor;
                static constexpr bool k_at_two   = k_delay.frames != 0 && k_delay.rate == 2 && Factor == 4;

                /// The chain: the parts in order, the delay after the stage at the full inner
                /// rate, or after the inner down2 at the doubled rate.
                template <bool AtInner, bool AtTwo, std::size_t F = Factor>
                struct chain_of;
                template <bool AtInner, bool AtTwo>
                struct chain_of<AtInner, AtTwo, 2> {
                    using type =
                        std::conditional_t<AtInner, chain_body<default_block, up_outer, Stage, delay_t, down_outer>,
                                           chain_body<default_block, up_outer, Stage, down_outer>>;
                };
                template <bool AtInner, bool AtTwo>
                struct chain_of<AtInner, AtTwo, 4> {
                    using type = std::conditional_t<
                        AtInner, chain_body<default_block, up_outer, up_inner, Stage, delay_t, down_inner, down_outer>,
                        std::conditional_t<
                            AtTwo,
                            chain_body<default_block, up_outer, up_inner, Stage, down_inner, delay_t, down_outer>,
                            chain_body<default_block, up_outer, up_inner, Stage, down_inner, down_outer>>>;
                };
                using body = typename chain_of<k_at_inner, k_at_two>::type;

                /// The chain of the parts around `stage`.
                static constexpr body make(const Stage& stage) noexcept {
                    if constexpr (Factor == 2) {
                        if constexpr (k_at_inner) {
                            return body(up_outer{}, stage, delay_t{}, down_outer{});
                        }
                        else {
                            return body(up_outer{}, stage, down_outer{});
                        }
                    }
                    else if constexpr (k_at_inner) {
                        return body(up_outer{}, up_inner{}, stage, delay_t{}, down_inner{}, down_outer{});
                    }
                    else if constexpr (k_at_two) {
                        return body(up_outer{}, up_inner{}, stage, down_inner{}, delay_t{}, down_outer{});
                    }
                    else {
                        return body(up_outer{}, up_inner{}, stage, down_inner{}, down_outer{});
                    }
                }
            };

        } // namespace detail

        /// @brief `Stage` at `Factor` times its port's rate, between half-bands, its latency
        /// made whole.
        template <std::size_t Factor, typename Stage>
        class oversample : public detail::oversample_plan<Factor, Stage>::body {
            using plan                                 = detail::oversample_plan<Factor, Stage>;
            using body                                 = typename plan::body;
            static constexpr std::size_t k_inner_index = Factor == 2 ? 1 : 2; ///< where the stage is

          public:
            using inner_stage                   = Stage;  ///< the stage it runs at the inner rate
            static constexpr std::size_t factor = Factor; ///< 2 or 4
            /// The frames of delay it adds at `delay_rate` times its port's rate, 0 for none.
            static constexpr std::size_t delay_frames = plan::k_delay.frames;
            /// Where the delay runs, as a multiple of the port's rate.
            static constexpr std::size_t delay_rate = plan::k_delay.rate;
            /// The parts' latency without the delay, base-rate frames.
            static constexpr ratio bare_latency = plan::k_bare;

            /// The oversampler around `stage`.
            explicit constexpr oversample(const Stage& stage) noexcept
                : body(plan::make(stage)) {}

            /// The stage it runs.
            constexpr const Stage& inner() const noexcept { return this->template stage<k_inner_index>(); }
            /// The stage it runs, to change a parameter between calls.
            constexpr Stage& inner() noexcept { return this->template stage<k_inner_index>(); }
        };

        template <std::size_t Factor, typename Stage>
        struct is_stage<oversample<Factor, Stage>> : std::true_type {};
        template <std::size_t Factor, typename Stage>
        struct is_chain<oversample<Factor, Stage>> : std::true_type {};
        /// An oversampler holds a view when its stage does.
        template <std::size_t Factor, typename Stage>
        struct holds_view<oversample<Factor, Stage>> : holds_view<Stage> {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_OVERSAMPLE_H
