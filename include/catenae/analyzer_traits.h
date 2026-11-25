/// @file analyzer_traits.h
/// @brief What an analyzer declares, read and checked: `analyzer_traits<T>`, `is_analyzer<T>` (D11).
///
/// | Contract  |                                                                                |
/// |-----------|--------------------------------------------------------------------------------|
/// | Members   | the table of PLAN.md section 6.2; the analyzer contract page                   |
/// | Reads     | an input port, and writes nothing; `tap<Ch, Analyzer>` puts one in a chain (C5) |
/// | Results   | one every `hop_frames`, over the last `window_frames`; a 32-bit count that wraps |
/// | Rest      | as a stage's: `reset(state&) const noexcept` or `rests_at_zero = true`, exactly one |
/// | Rationale | D11, D44, D49                                                                  |
///
/// The requirements are on the analyzer contract's page, CAT-ANALYZER-001 onwards.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_ANALYZER_TRAITS_H
#define CATENAE_ANALYZER_TRAITS_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "catenae/detail/abi.h"
#include "catenae/detail/detect.h"
#include "catenae/port.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Whether a type is an analyzer; its author specialises it.
        template <typename T>
        struct is_analyzer : std::false_type {};

        namespace detail {
            template <typename T>
            using hop_frames_op = decltype(T::hop_frames);
            template <typename T>
            using window_frames_op = decltype(T::window_frames);
            template <typename T>
            using result_type_op = typename T::result_type;
            template <typename T, typename S, typename I>
            using analyze_op =
                std::enable_if_t<noexcept(std::declval<const T&>().analyze(std::declval<S&>(), std::declval<I>())),
                                 decltype(std::declval<const T&>().analyze(std::declval<S&>(), std::declval<I>()))>;
            template <typename T, typename S>
            using result_op = std::decay_t<decltype(std::declval<const T&>().result(std::declval<const S&>()))>;
            template <typename T, typename S>
            using results_ready_op = decltype(std::declval<const T&>().results_ready(std::declval<const S&>()));
        } // namespace detail

        /// @brief An analyzer's contract, read through its members and checked when instantiated.
        template <typename T>
        struct analyzer_traits {
            using input_port  = detail::detected_t<detail::input_port_op, T>;  ///< what it reads
            using state       = detail::detected_t<detail::state_op, T>;       ///< what one instance remembers
            using result_type = detail::detected_t<detail::result_type_op, T>; ///< what a result is

          private:
            static constexpr bool k_state = detail::k_is_detected<detail::state_op, T>;
            // Both ways to rest are looked for, so that exactly one is declared (DEV-014).
            static constexpr bool k_has_reset = detail::k_is_detected_exact<void, detail::reset_op, T, state>;
            static constexpr bool k_declares_rest =
                detail::k_is_detected_exact<const bool, detail::rests_at_zero_op, T>;

          public:
            static_assert(std::is_class<T>::value, "catenae: an analyzer is a class: its configuration");
            static_assert(detail::k_is_detected<detail::input_port_op, T> && is_port<input_port>::value,
                          "catenae: an analyzer declares input_port, the port type it reads");
            static_assert(!detail::k_is_detected<detail::output_port_op, T>,
                          "catenae: an analyzer has no output port: tap<Ch, Analyzer> passes the audio through");
            static_assert(detail::k_is_detected_exact<const std::size_t, detail::hop_frames_op, T>,
                          "catenae: an analyzer declares static constexpr std::size_t hop_frames");
            static_assert(detail::k_is_detected_exact<const std::size_t, detail::window_frames_op, T>,
                          "catenae: an analyzer declares static constexpr std::size_t window_frames");
            static_assert(k_state, "catenae: an analyzer declares state, the plain aggregate one instance remembers");
            static_assert(!k_state || detail::k_is_plain_storage<state>,
                          "catenae: an analyzer's state is a plain aggregate: trivially copyable, standard-layout, "
                          "with no constructor and no default member initialisers");
            static_assert(!k_state
                              || (k_has_reset != k_declares_rest
                                  && (!k_declares_rest || detail::rests_at_zero_of<T>::value)),
                          "catenae: an analyzer writes reset(state&) const noexcept, or declares rests_at_zero = true: "
                          "exactly one");
            static_assert(
                !(k_state && is_port<input_port>::value)
                    || detail::k_is_detected_exact<void, detail::analyze_op, T, state, typename input_port::input_view>,
                "catenae: an analyzer declares void analyze(state&, input_view) const noexcept");
            static_assert(detail::k_is_detected<detail::result_type_op, T>,
                          "catenae: an analyzer declares result_type");
            static_assert(!k_state || detail::k_is_detected_exact<result_type, detail::result_op, T, state>,
                          "catenae: an analyzer declares result(const state&) const, returning its result_type");
            static_assert(
                !k_state || detail::k_is_detected_exact<std::uint32_t, detail::results_ready_op, T, state>,
                "catenae: an analyzer declares std::uint32_t results_ready(const state&) const, a count that wraps");
            static_assert(std::is_trivially_copyable<T>::value,
                          "catenae: an analyzer's configuration is trivially copyable");

            static constexpr bool rests_at_zero = k_declares_rest; ///< D49
            /// Whether the analyzer's author opted it in.
            static constexpr bool opted_in = is_analyzer<T>::value;
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_ANALYZER_TRAITS_H
