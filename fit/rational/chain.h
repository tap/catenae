/// @file chain.h
/// @brief The rational engine's chains as alias templates that compute each stage's design (CF).
// Copyright 2026 Timothy Place and the catenae contributors
//
// SampleRateTap's basic_chain<S, Ch, R...> (rational/include/tap/sr/rational/chain.h at
// 4496888) is a class over DspTap's chain<> whose constructor designs each stage at its
// design divisor from a run-time profile. Here a chain is catenae's own chain<> (D13), and
// `chain_of<S, Ch, Profile, R...>` is an alias template that computes each stage's divisor at
// compile time and names the design the profile pins there: the stage types alone say which
// table each stage runs (PLAN.md 3.3: the two up2 of a four-times oversampler are different
// types). The named chains are aliases of it, with SampleRateTap's names; a one-stage chain is
// its converter.
#ifndef CATENAE_FIT_RATIONAL_CHAIN_H
#define CATENAE_FIT_RATIONAL_CHAIN_H

#include <cstddef>
#include <utility>

#include "catenae/chain.h"
#include "rational/design.h"
#include "rational/ratio.h"
#include "rational/stage.h"

namespace catenae_fit::rational {

    namespace detail {

        template <typename S, std::size_t Ch, typename Profile, typename Indices, typename... Rs>
        struct chain_of;
        template <typename S, std::size_t Ch, typename Profile, std::size_t... I, typename... Rs>
        struct chain_of<S, Ch, Profile, std::index_sequence<I...>, Rs...> {
            using rates = chain_rates<Rs...>;
            using type  = catenae::chain<
                 stage<S, Ch, Rs, design_at<Profile, Rs, rates::divisor(I).up, rates::divisor(I).down>>...>;
        };

    } // namespace detail

    /// The chain of the ratios Rs, in order, each stage at the design its profile pins at its
    /// design divisor.
    template <typename S, std::size_t Ch, typename Profile, typename... Rs>
    using chain_of = typename detail::chain_of<S, Ch, Profile, std::index_sequence_for<Rs...>, Rs...>::type;

    // CF finding F4, made: a named chain is default-constructed from its stages' defaults
    // (catenae::chain_body's constexpr default constructor, CAT-CHAIN-014), so
    // `constexpr up2_ratio3over2<float, 2> c{};` is the chain; the skeleton's make<Chain>() is gone.

    /// One stage at ratio R, as a chain: SampleRateTap's converter.
    template <typename S, std::size_t Ch, typename R, typename Profile = fit_profile>
    using converter = chain_of<S, Ch, Profile, R>;

    /// 3/1: up by 2, then 3/2 (SampleRateTap's up2_ratio3over2). The second stage's lower rate is
    /// twice the chain's lowest, so it runs the design relaxed by 2.
    template <typename S, std::size_t Ch, typename Profile = fit_profile>
    using up2_ratio3over2 = chain_of<S, Ch, Profile, catenae::ratio_t<2, 1>, catenae::ratio_t<3, 2>>;

    /// 1/6: down by 3, then by 2 (SampleRateTap's down3_down2). The first stage's lower rate is
    /// twice the chain's lowest, so it runs the design relaxed by 2.
    template <typename S, std::size_t Ch, typename Profile = fit_profile>
    using down3_down2 = chain_of<S, Ch, Profile, catenae::ratio_t<1, 3>, catenae::ratio_t<1, 2>>;

} // namespace catenae_fit::rational

#endif // CATENAE_FIT_RATIONAL_CHAIN_H
