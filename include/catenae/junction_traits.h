/// @file junction_traits.h
/// @brief What a junction declares, read and checked: `junction_traits<T>`, `is_junction<T>` (D19).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Members   | the table of PLAN.md section 6.1; the junction contract page                     |
/// | Sides     | `push` on exactly one thread, `pull` on exactly one other; `report` on any       |
/// | State     | a plain aggregate whose rest is all zero, its shared words plain 32-bit words    |
/// | Serves    | `serves(largest_pull, quantum)`: `static constexpr`, what `fed_by` asserts (D51) |
/// | Capacity  | `capacity_frames`, a power of two                                                |
/// | Clocks    | `clocks::shared` or `clocks::independent`; only an independent one has a tolerance |
/// | Rationale | D19, D42, D51, D52                                                               |
///
/// The requirements are on the junction contract's page, CAT-JUNCTION-001 onwards.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_JUNCTION_TRAITS_H
#define CATENAE_JUNCTION_TRAITS_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "catenae/detail/abi.h"
#include "catenae/detail/detect.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Whether a type is a junction; its author specialises it.
        template <typename T>
        struct is_junction : std::false_type {};

        /// How a junction's two sides are clocked.
        enum class clocks : std::uint8_t {
            shared,      ///< two threads, one clock
            independent, ///< two clocks: the junction absorbs their offset and drift
        };

        /// A junction's latency in source frames: nominal, and how far it breathes with the fill.
        struct junction_latency {
            float nominal; ///< the nominal delay
            float range;   ///< how far the delay moves either side of it
        };

        namespace detail {
            template <typename T>
            using sink_port_op = typename T::sink_port;
            template <typename T>
            using source_port_op = typename T::source_port;
            template <typename T>
            using clocking_op = decltype(T::clocking);
            template <typename T>
            using capacity_frames_op = decltype(T::capacity_frames);
            template <typename T>
            using tolerance_ppm_op = decltype(std::declval<const T&>().tolerance_ppm());
            template <typename T, typename S, typename I>
            using push_op =
                std::enable_if_t<noexcept(std::declval<const T&>().push(std::declval<S&>(), std::declval<I>())),
                                 decltype(std::declval<const T&>().push(std::declval<S&>(), std::declval<I>()))>;
            template <typename T, typename S, typename O>
            using pull_op =
                std::enable_if_t<noexcept(std::declval<const T&>().pull(std::declval<S&>(), std::declval<O>())),
                                 decltype(std::declval<const T&>().pull(std::declval<S&>(), std::declval<O>()))>;
            template <typename T>
            using serves_op = decltype(std::integral_constant<bool, T::serves(std::size_t{1}, std::size_t{1})>{});
            template <typename T, typename S>
            using report_op = decltype(std::declval<const T&>().report(std::declval<const S&>()));
            template <typename T, typename S>
            using junction_latency_op = decltype(std::declval<const T&>().latency(std::declval<const S&>()));
            template <typename T, typename S>
            using restart_op = std::enable_if_t<noexcept(std::declval<const T&>().restart(std::declval<S&>())),
                                                decltype(std::declval<const T&>().restart(std::declval<S&>()))>;

            template <typename T, bool = k_is_detected_exact<const clocks, clocking_op, T>>
            struct clocking_of : std::integral_constant<clocks, clocks::shared> {};
            template <typename T>
            struct clocking_of<T, true> : std::integral_constant<clocks, T::clocking> {};
            template <typename T, bool = k_is_detected_exact<const std::size_t, capacity_frames_op, T>>
            struct capacity_of : std::integral_constant<std::size_t, 1> {};
            template <typename T>
            struct capacity_of<T, true> : std::integral_constant<std::size_t, T::capacity_frames> {};
        } // namespace detail

        /// @brief A junction's contract, read through its members and checked when instantiated.
        template <typename T>
        struct junction_traits {
            using sink_port   = detail::detected_t<detail::sink_port_op, T>;   ///< what push takes
            using source_port = detail::detected_t<detail::source_port_op, T>; ///< what pull fills
            using state       = detail::detected_t<detail::state_op, T>;       ///< what its two sides share

          private:
            static constexpr bool k_ports = is_port<sink_port>::value && is_port<source_port>::value;
            static constexpr bool k_state = detail::k_is_detected<detail::state_op, T>;

          public:
            static_assert(std::is_class<T>::value, "catenae: a junction is a class: its configuration");
            static_assert(detail::k_is_detected<detail::sink_port_op, T> && is_port<sink_port>::value,
                          "catenae: a junction declares sink_port, the port type push takes");
            static_assert(detail::k_is_detected<detail::source_port_op, T> && is_port<source_port>::value,
                          "catenae: a junction declares source_port, the port type pull fills");
            static_assert(detail::k_is_detected_exact<const clocks, detail::clocking_op, T>,
                          "catenae: a junction declares static constexpr clocks clocking");
            static_assert(detail::k_is_detected_exact<const ratio, detail::rate_ratio_op, T>,
                          "catenae: a junction declares static constexpr ratio rate_ratio, its nominal ratio");
            static_assert(k_state, "catenae: a junction declares state, the plain storage its two sides share");
            static_assert(!k_state || detail::k_is_plain_storage<state>,
                          "catenae: a junction's state is a plain aggregate, at rest when all zero, its shared words "
                          "plain 32-bit words");
            static_assert(detail::clocking_of<T>::value != clocks::independent
                              || detail::k_is_detected_exact<float, detail::tolerance_ppm_op, T>,
                          "catenae: a junction of independent clocks declares float tolerance_ppm() const");
            static_assert(!(k_ports && k_state)
                              || detail::k_is_detected_exact<std::size_t, detail::push_op, T, state,
                                                             typename sink_port::input_view>,
                          "catenae: a junction declares std::size_t push(state&, input_view) const noexcept");
            static_assert(!(k_ports && k_state)
                              || detail::k_is_detected_exact<std::size_t, detail::pull_op, T, state,
                                                             typename source_port::output_view>,
                          "catenae: a junction declares std::size_t pull(state&, output_view) const noexcept");
            static_assert(detail::k_is_detected<detail::serves_op, T>,
                          "catenae: a junction declares static constexpr bool serves(std::size_t largest_pull, "
                          "std::size_t quantum)");
            static_assert(!k_state || detail::k_is_detected<detail::report_op, T, state>,
                          "catenae: a junction declares report(const state&) const");
            static_assert(!k_state
                              || detail::k_is_detected_exact<junction_latency, detail::junction_latency_op, T, state>,
                          "catenae: a junction declares junction_latency latency(const state&) const");
            static_assert(detail::k_is_detected_exact<const std::size_t, detail::capacity_frames_op, T>,
                          "catenae: a junction declares static constexpr std::size_t capacity_frames");
            static_assert(detail::capacity_of<T>::value != 0
                              && (detail::capacity_of<T>::value & (detail::capacity_of<T>::value - 1)) == 0,
                          "catenae: a junction's capacity_frames is a power of two");
            static_assert(!k_state || detail::k_is_detected_exact<void, detail::reset_op, T, state>,
                          "catenae: a junction declares void reset(state&) const noexcept, which empties it");
            static_assert(!k_state || detail::k_is_detected_exact<void, detail::restart_op, T, state>,
                          "catenae: a junction declares void restart(state&) const noexcept, for its consumer");
            static_assert(std::is_trivially_copyable<T>::value,
                          "catenae: a junction's configuration is trivially copyable");

            static constexpr clocks      clocking        = detail::clocking_of<T>::value; ///< shared or independent
            static constexpr ratio       rate_ratio      = detail::rate_ratio_of<T>();    ///< nominal ratio
            static constexpr std::size_t capacity_frames = detail::capacity_of<T>::value; ///< a power of two
            /// Whether the junction serves pulls of up to `largest_pull` frames in multiples of `quantum`.
            static constexpr bool serves(std::size_t largest_pull, std::size_t quantum) noexcept {
                return T::serves(largest_pull, quantum);
            }
            /// Whether the junction's author opted it in.
            static constexpr bool opted_in = is_junction<T>::value;
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_JUNCTION_TRAITS_H
