/// @file cf_ports.h
/// @brief A conforming stage of any two ports, for the joint cases of the compile-fail battery.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TESTS_COMPILE_FAIL_CF_PORTS_H
#define CATENAE_TESTS_COMPILE_FAIL_CF_PORTS_H

#include <cstddef>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/stage_traits.h"

namespace cf {

    /// A wire format as unpack and pack will name one.
    struct s24 {
        static constexpr std::size_t bytes_per_sample = 3;
    };

    /// Reads In and writes Out, and does nothing: what a joint needs of a stage.
    template <typename In, typename Out>
    class between {
      public:
        using input_port  = In;
        using output_port = Out;
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = 1;
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};
        static constexpr bool           rests_at_zero = true;
        struct state {};
        using workspace = catenae::no_workspace;

        std::size_t run(state&, workspace&, typename In::input_view, typename Out::output_view) const noexcept {
            return 0;
        }
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

    /// The chain of a stage writing A and one reading B: forming its type forms the joint.
    template <typename A, typename B>
    using joined = catenae::chain<between<catenae::port<float, 1>, A>, between<B, catenae::port<float, 1>>>;

} // namespace cf

namespace catenae {
    template <typename In, typename Out>
    struct is_stage<cf::between<In, Out>> : std::true_type {};
} // namespace catenae

#endif // CATENAE_TESTS_COMPILE_FAIL_CF_PORTS_H
