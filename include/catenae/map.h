/// @file map.h
/// @brief `map`: a function applied to every sample on its own, as a 1:1 stage (PLAN.md 9.1).
///
/// | Contract   |                                                                         |
/// |------------|-------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                |
/// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)     |
/// | Configured | by `F`, a trivially copyable callable `value_type(value_type) const noexcept` |
/// | State      | none (`rests_at_zero`)                                                  |
/// | Workspace  | none                                                                    |
/// | Latency    | exactly 0                                                               |
/// | Tail       | 0 frames                                                                |
/// | Formats    | any: `F` maps the port's storage, so its arithmetic is the caller's     |
/// | Limits     | `F` sees one value at a time and no channel index                       |
/// | Method     | `std::transform` over the block's values                                |
/// | Rationale  | D24, D34, PLAN.md 9.1                                                   |
///
/// @req{CAT-MAP-001} Every value of every frame is replaced by `F` of it, in order, and a
/// map in place (input and output the same values) writes the same as one that is not.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_MAP_H
#define CATENAE_MAP_H

#include <algorithm>
#include <cstddef>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief `F` applied to every value of a `port<S, Ch>`.
        template <typename S, std::size_t Ch, typename F>
        class basic_map {
          public:
            using input_port  = port<S, Ch>; ///< what it reads
            using output_port = port<S, Ch>; ///< what it writes

          private:
            using value_type = storage_t<S>;
            static_assert(std::is_trivially_copyable<F>::value,
                          "catenae: a map's function is part of its configuration, so it is trivially copyable");
            static_assert(std::is_nothrow_invocable_r<value_type, const F&, value_type>::value,
                          "catenae: a map's function is value_type(value_type) const noexcept");

          public:
            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = true;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< it remembers nothing

            /// Nothing: a map remembers nothing.
            struct state {};
            using workspace = no_workspace; ///< nothing

            /// The map of `f`.
            constexpr explicit basic_map(const F& f) noexcept
                : m_function(f) {}

            /// The function.
            constexpr const F& function() const noexcept { return m_function; }

            /// Each value replaced by the function of it.
            std::size_t run(state&, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                std::transform(in.begin(), in.end(), out.begin(), m_function);
                return in.size() / Ch;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            F m_function;
        };

        template <std::size_t Ch, typename F>
        using map = basic_map<float, Ch, F>; ///< in float (D24)
        template <std::size_t Ch, typename F>
        using map_f64 = basic_map<double, Ch, F>; ///< in double
        template <std::size_t Ch, typename F>
        using map_q15 = basic_map<q15, Ch, F>; ///< over Q15 storage
        template <std::size_t Ch, typename F>
        using map_q31 = basic_map<q31, Ch, F>; ///< over Q31 storage
        template <std::size_t Ch, typename F>
        using map_q4_27 = basic_map<q4_27, Ch, F>; ///< over Q4.27 storage

        template <typename S, std::size_t Ch, typename F>
        struct is_stage<basic_map<S, Ch, F>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_MAP_H
