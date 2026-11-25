/// @file detect.h
/// @brief Member detection for the traits: whether an expression or a type is well-formed.
// Copyright 2026 Timothy Place and the catenae contributors
//
// The traits read a stage's, a junction's or an analyzer's members through these, so that a
// missing member fails with its own static_assert and never with a cascade of template errors
// (PLAN.md section 3.2): every query is well-formed whether or not the member exists.
#ifndef CATENAE_DETAIL_DETECT_H
#define CATENAE_DETAIL_DETECT_H

#include <type_traits>

#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// The type an absent member stands in for.
            struct absent {};

            template <typename Default, typename Void, template <typename...> class Op, typename... Args>
            struct detector {
                using value_t = std::false_type;
                using type    = Default;
            };
            template <typename Default, template <typename...> class Op, typename... Args>
            struct detector<Default, std::void_t<Op<Args...>>, Op, Args...> {
                using value_t = std::true_type;
                using type    = Op<Args...>;
            };

            /// Whether `Op<Args...>` is well-formed.
            template <template <typename...> class Op, typename... Args>
            inline constexpr bool k_is_detected = detector<absent, void, Op, Args...>::value_t::value;
            /// `Op<Args...>`, or `absent` where it is not well-formed.
            template <template <typename...> class Op, typename... Args>
            using detected_t = typename detector<absent, void, Op, Args...>::type;
            /// Whether `Op<Args...>` is well-formed and is `Expected`.
            template <typename Expected, template <typename...> class Op, typename... Args>
            inline constexpr bool k_is_detected_exact = std::is_same<Expected, detected_t<Op, Args...>>::value;

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_DETECT_H
