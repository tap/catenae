/// @file stage_traits.h
/// @brief What a stage declares, read and checked: `stage_traits<T>`, `is_stage<T>` (D10, D34, D35, D49).
///
/// | Contract  |                                                                                     |
/// |-----------|-------------------------------------------------------------------------------------|
/// | Members   | the table of PLAN.md section 3.2; the stage contract page (docs/contracts/stage.md) |
/// | Checks    | one static_assert, with one fixed message, per missing or ill-typed member          |
/// | Rest      | `reset(state&) const noexcept` or `rests_at_zero = true`, exactly one (D49)         |
/// | Plain     | state and workspace: plain aggregates, no constructor, no default member initialiser |
/// | Size      | a configuration is trivially copyable, and at most 512 bytes unless it is a chain (D54) |
/// | Derived   | `is_frame_synchronous`, which a stage never declares (D35)                           |
/// | Opt-in    | `is_stage<T>`, specialised by the stage's author; the traits do not require it (D47) |
/// | Rationale | D10, D34, D35, D39, D49, D50, D54                                                    |
///
/// The requirements are on the stage contract's page, CAT-STAGE-001 onwards.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_STAGE_TRAITS_H
#define CATENAE_STAGE_TRAITS_H

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

#include "catenae/detail/abi.h"
#include "catenae/detail/detect.h"
#include "catenae/fwd.h"
#include "catenae/port.h"
#include "catenae/ratio.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Whether a type is a stage that may join a chain; its author specialises it (D10).
        template <typename T>
        struct is_stage : std::false_type {};

        /// Whether a type is a chain, which the configuration-size rule exempts (D54). Arrives with
        /// the chain at C1.
        template <typename T>
        struct is_chain : std::false_type {};

        /// The workspace of a stage that needs none (D50).
        struct no_workspace {};

        /// A stage's latency in output frames: its nominal value and the deviation from it (D39).
        struct stage_latency {
            float nominal;   ///< the stored nominal latency
            float deviation; ///< how far the delay departs from it; 0 when exact
        };

        /// The largest configuration of a stage that is not a chain, in bytes (D54).
        inline constexpr std::size_t max_configuration_bytes = 512;

        namespace detail {

            template <typename T>
            using input_port_op = typename T::input_port;
            template <typename T>
            using output_port_op = typename T::output_port;
            template <typename T>
            using state_op = typename T::state;
            template <typename T>
            using workspace_op = typename T::workspace;
            template <typename T>
            using rate_ratio_op = decltype(T::rate_ratio);
            template <typename T>
            using input_group_op = decltype(T::input_group);
            template <typename T>
            using max_shortfall_op = decltype(T::max_shortfall);
            template <typename T>
            using max_lead_op = decltype(T::max_lead);
            template <typename T>
            using output_quantum_op = decltype(T::output_quantum);
            template <typename T>
            using is_in_place_op = decltype(T::is_in_place);
            template <typename T>
            using is_frame_synchronous_op = decltype(T::is_frame_synchronous);
            template <typename T>
            using has_exact_latency_op = decltype(T::has_exact_latency);
            template <typename T>
            using exact_latency_op = decltype(T::exact_latency);
            template <typename T>
            using rests_at_zero_op = decltype(T::rests_at_zero);
            template <typename T, typename S>
            using reset_op = std::enable_if_t<noexcept(std::declval<const T&>().reset(std::declval<S&>())),
                                              decltype(std::declval<const T&>().reset(std::declval<S&>()))>;
            template <typename T, typename S, typename W, typename I, typename O>
            using run_op =
                std::enable_if_t<noexcept(std::declval<const T&>().run(std::declval<S&>(), std::declval<W&>(),
                                                                       std::declval<I>(), std::declval<O>())),
                                 decltype(std::declval<const T&>().run(std::declval<S&>(), std::declval<W&>(),
                                                                       std::declval<I>(), std::declval<O>()))>;
            template <typename T, typename S>
            using outputs_for_op =
                decltype(std::declval<const T&>().outputs_for(std::declval<const S&>(), std::size_t{}));
            template <typename T, typename S>
            using frames_needed_op =
                decltype(std::declval<const T&>().frames_needed(std::declval<const S&>(), std::size_t{}));
            template <typename T>
            using max_outputs_for_op =
                decltype(std::integral_constant<std::size_t, T::max_outputs_for(std::size_t{1})>{});
            template <typename T>
            using latency_op = decltype(std::declval<const T&>().latency());
            template <typename T>
            using tail_frames_op = decltype(std::declval<const T&>().tail_frames());
            template <typename T>
            using practical_tail_frames_op = decltype(std::declval<const T&>().practical_tail_frames());
            template <typename T>
            using flush_residue_op = decltype(std::declval<const T&>().flush_residue());

            /// Whether a type is a plain aggregate: what a state and a workspace are (D34, D50).
            template <typename T>
            inline constexpr bool k_is_plain_storage =
                std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value
                && std::is_trivially_default_constructible<T>::value && std::is_aggregate<T>::value;

            template <typename T, bool = k_is_detected_exact<const std::size_t, input_group_op, T>>
            struct input_group_of : std::integral_constant<std::size_t, 1> {};
            template <typename T>
            struct input_group_of<T, true> : std::integral_constant<std::size_t, T::input_group> {};
            template <typename T, bool = k_is_detected_exact<const std::size_t, output_quantum_op, T>>
            struct output_quantum_of : std::integral_constant<std::size_t, 1> {};
            template <typename T>
            struct output_quantum_of<T, true> : std::integral_constant<std::size_t, T::output_quantum> {};
            template <typename T, bool = k_is_detected_exact<const bool, is_in_place_op, T>>
            struct is_in_place_of : std::false_type {};
            template <typename T>
            struct is_in_place_of<T, true> : std::bool_constant<T::is_in_place> {};
            template <typename T, bool = k_is_detected_exact<const bool, has_exact_latency_op, T>>
            struct has_exact_latency_of : std::false_type {};
            template <typename T>
            struct has_exact_latency_of<T, true> : std::bool_constant<T::has_exact_latency> {};
            template <typename T, bool = k_is_detected_exact<const bool, rests_at_zero_op, T>>
            struct rests_at_zero_of : std::false_type {};
            template <typename T>
            struct rests_at_zero_of<T, true> : std::bool_constant<T::rests_at_zero> {};

            /// Each ratio member's value, or a stand-in where it is absent or ill-typed.
            template <typename T>
            constexpr ratio rate_ratio_of() noexcept {
                if constexpr (k_is_detected_exact<const ratio, rate_ratio_op, T>) {
                    return T::rate_ratio;
                }
                else {
                    return ratio{1, 1};
                }
            }
            template <typename T>
            constexpr ratio max_shortfall_of() noexcept {
                if constexpr (k_is_detected_exact<const ratio, max_shortfall_op, T>) {
                    return T::max_shortfall;
                }
                else {
                    return ratio{0, 1};
                }
            }
            template <typename T>
            constexpr ratio max_lead_of() noexcept {
                if constexpr (k_is_detected_exact<const ratio, max_lead_op, T>) {
                    return T::max_lead;
                }
                else {
                    return ratio{0, 1};
                }
            }
            template <typename T>
            constexpr ratio exact_latency_of() noexcept {
                if constexpr (k_is_detected_exact<const ratio, exact_latency_op, T>) {
                    return T::exact_latency;
                }
                else {
                    return ratio{0, 1};
                }
            }

            /// Whether max_outputs_for(n) is n, over the sizes a block is likely to be.
            template <typename T>
            constexpr bool max_outputs_is_identity() noexcept {
                if constexpr (k_is_detected<max_outputs_for_op, T>) {
                    constexpr std::size_t sizes[] = {0, 1, 2, 3, 5, 7, 16, 64, 100, 128, 480, 512, 1000, 4096};
                    for (const std::size_t n : sizes) {
                        if (T::max_outputs_for(n) != n) {
                            return false;
                        }
                    }
                    return true;
                }
                else {
                    return false;
                }
            }

        } // namespace detail

        /// @brief A stage's contract, read through its members and checked when instantiated.
        template <typename T>
        struct stage_traits {
            using input_port  = detail::detected_t<detail::input_port_op, T>;  ///< the port it reads
            using output_port = detail::detected_t<detail::output_port_op, T>; ///< the port it writes
            using state       = detail::detected_t<detail::state_op, T>;       ///< what an instance remembers
            using workspace   = detail::detected_t<detail::workspace_op, T>;   ///< what a call needs

          private:
            static constexpr bool k_ports = is_port<input_port>::value && is_port<output_port>::value;
            static constexpr bool k_storage =
                detail::k_is_detected<detail::state_op, T> && detail::k_is_detected<detail::workspace_op, T>;
            // Both ways to rest are looked for, so that exactly one is declared (DEV-014).
            static constexpr bool k_has_reset = detail::k_is_detected_exact<void, detail::reset_op, T, state>;
            static constexpr bool k_declares_rest =
                detail::k_is_detected_exact<const bool, detail::rests_at_zero_op, T>;

          public:
            static_assert(std::is_class<T>::value, "catenae: a stage is a class: its configuration");
            static_assert(detail::k_is_detected<detail::input_port_op, T>,
                          "catenae: a stage declares input_port, the port type it reads");
            static_assert(detail::k_is_detected<detail::output_port_op, T>,
                          "catenae: a stage declares output_port, the port type it writes");
            static_assert(!detail::k_is_detected<detail::input_port_op, T> || is_port<input_port>::value,
                          "catenae: a stage's input_port is a port type: port, spectrum or wire_port");
            static_assert(!detail::k_is_detected<detail::output_port_op, T> || is_port<output_port>::value,
                          "catenae: a stage's output_port is a port type: port, spectrum or wire_port");
            static_assert(detail::k_is_detected_exact<const ratio, detail::rate_ratio_op, T>,
                          "catenae: a stage declares static constexpr ratio rate_ratio, its L/M");
            static_assert(!detail::k_is_detected_exact<const ratio, detail::rate_ratio_op, T>
                              || (is_reduced(detail::rate_ratio_of<T>()) && detail::rate_ratio_of<T>().up != 0),
                          "catenae: a stage's rate_ratio is a reduced ratio{up, down}, neither of them 0");
            static_assert(detail::k_is_detected<detail::state_op, T>,
                          "catenae: a stage declares state, the plain aggregate one instance remembers");
            static_assert(!detail::k_is_detected<detail::state_op, T> || detail::k_is_plain_storage<state>,
                          "catenae: a stage's state is a plain aggregate: trivially copyable, standard-layout, "
                          "with no constructor and no default member initialisers");
            static_assert(detail::k_is_detected<detail::workspace_op, T>,
                          "catenae: a stage declares workspace, what a call needs (no_workspace when it needs none)");
            static_assert(!detail::k_is_detected<detail::workspace_op, T> || detail::k_is_plain_storage<workspace>,
                          "catenae: a stage's workspace is a plain aggregate: trivially copyable, standard-layout, "
                          "with no constructor and no default member initialisers");
            static_assert(!detail::k_is_detected<detail::state_op, T>
                              || (k_has_reset != k_declares_rest
                                  && (!k_declares_rest || detail::rests_at_zero_of<T>::value)),
                          "catenae: a stage writes reset(state&) const noexcept, or declares rests_at_zero = true: "
                          "exactly one");
            static_assert(detail::k_is_detected_exact<const std::size_t, detail::input_group_op, T>,
                          "catenae: a stage declares static constexpr std::size_t input_group");
            static_assert(detail::input_group_of<T>::value >= 1, "catenae: a stage's input_group is at least 1");
            static_assert(detail::k_is_detected_exact<const ratio, detail::max_shortfall_op, T>,
                          "catenae: a stage declares static constexpr ratio max_shortfall");
            static_assert(detail::k_is_detected_exact<const ratio, detail::max_lead_op, T>,
                          "catenae: a stage declares static constexpr ratio max_lead");
            static_assert(detail::k_is_detected_exact<const std::size_t, detail::output_quantum_op, T>,
                          "catenae: a stage declares static constexpr std::size_t output_quantum");
            static_assert(detail::output_quantum_of<T>::value >= 1, "catenae: a stage's output_quantum is at least 1");
            // A chain carries its own, derived from its composed input_group by the library and
            // never written by hand (PLAN.md 9.4, 9.5); every other stage leaves it to the traits.
            static_assert(!detail::k_is_detected<detail::is_frame_synchronous_op, T> || is_chain<T>::value,
                          "catenae: a stage never declares is_frame_synchronous: stage_traits derives it (D35)");
            static_assert(detail::k_is_detected_exact<const bool, detail::is_in_place_op, T>,
                          "catenae: a stage declares static constexpr bool is_in_place");
            static_assert(
                !detail::is_in_place_of<T>::value
                    || (std::is_same<input_port, output_port>::value && detail::input_group_of<T>::value == 1
                        && detail::max_outputs_is_identity<T>()),
                "catenae: a stage is in place only with one port type, input_group 1 and max_outputs_for(n) == n");
            static_assert(detail::k_is_detected_exact<const bool, detail::has_exact_latency_op, T>,
                          "catenae: a stage declares static constexpr bool has_exact_latency");
            static_assert(!detail::has_exact_latency_of<T>::value
                              || detail::k_is_detected_exact<const ratio, detail::exact_latency_op, T>,
                          "catenae: a stage with has_exact_latency declares static constexpr ratio exact_latency");
            static_assert(
                !(k_ports && k_storage)
                    || detail::k_is_detected_exact<std::size_t, detail::run_op, T, state, workspace,
                                                   typename input_port::input_view, typename output_port::output_view>,
                "catenae: a stage declares std::size_t run(state&, workspace&, input_view, output_view) "
                "const noexcept");
            static_assert(!k_storage || detail::k_is_detected_exact<std::size_t, detail::outputs_for_op, T, state>,
                          "catenae: a stage declares std::size_t outputs_for(const state&, std::size_t)");
            static_assert(!k_storage || detail::k_is_detected_exact<std::size_t, detail::frames_needed_op, T, state>,
                          "catenae: a stage declares std::size_t frames_needed(const state&, std::size_t)");
            static_assert(detail::k_is_detected<detail::max_outputs_for_op, T>,
                          "catenae: a stage declares static constexpr std::size_t max_outputs_for(std::size_t)");
            static_assert(detail::k_is_detected_exact<stage_latency, detail::latency_op, T>,
                          "catenae: a stage declares stage_latency latency() const");
            static_assert(detail::k_is_detected_exact<std::size_t, detail::tail_frames_op, T>,
                          "catenae: a stage declares std::size_t tail_frames() const");
            static_assert(detail::k_is_detected_exact<std::size_t, detail::practical_tail_frames_op, T>,
                          "catenae: a stage declares std::size_t practical_tail_frames() const");
            static_assert(detail::k_is_detected_exact<float, detail::flush_residue_op, T>,
                          "catenae: a stage declares float flush_residue() const");
            static_assert(std::is_trivially_copyable<T>::value,
                          "catenae: a stage's configuration is trivially copyable (D54)");
            static_assert(
                is_chain<T>::value || sizeof(T) <= max_configuration_bytes,
                "catenae: a stage's configuration is at most 512 bytes; a larger table is held by view (D54)");

            static constexpr ratio       rate_ratio        = detail::rate_ratio_of<T>();          ///< L/M, reduced
            static constexpr std::size_t input_group       = detail::input_group_of<T>::value;    ///< D35
            static constexpr ratio       max_shortfall     = detail::max_shortfall_of<T>();       ///< D35
            static constexpr ratio       max_lead          = detail::max_lead_of<T>();            ///< D35
            static constexpr std::size_t output_quantum    = detail::output_quantum_of<T>::value; ///< D35
            static constexpr bool        is_in_place       = detail::is_in_place_of<T>::value;    ///< may run in place
            static constexpr bool        has_exact_latency = detail::has_exact_latency_of<T>::value; ///< D39
            static constexpr ratio       exact_latency     = detail::exact_latency_of<T>(); ///< D39, when exact
            static constexpr bool        rests_at_zero     = k_declares_rest;               ///< D49
            /// Derived, never declared: every call of n frames writes exactly n L/M (D35).
            static constexpr bool is_frame_synchronous = input_group == 1;
            /// Whether the stage's author opted it in (D10); a format its battery does not pin is not.
            static constexpr bool opted_in = is_stage<T>::value;

            /// Brings a state to rest in place (D49): the stage's reset, or zero when it rests at zero.
            static void reset(const T& configuration, state& st) noexcept {
                if constexpr (k_declares_rest) {
                    static_cast<void>(configuration);
                    std::memset(&st, 0, sizeof(state)); // the memset behind rests_at_zero (DEV-006)
                }
                else {
                    configuration.reset(st);
                }
            }
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_STAGE_TRAITS_H
