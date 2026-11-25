/// @file cf_stage.h
/// @brief A conforming stage for the compile-fail battery, each member a knob a case may turn.
// Copyright 2026 Timothy Place and the catenae contributors
//
// A case defines one or more of these before including this file, and only when
// CATENAE_CF_CONTROL is not defined, so that its control is this stage as it stands:
//
//   CF_OMIT_<MEMBER>  leaves that member out: RATE_RATIO, INPUT_GROUP, MAX_LEAD, IN_PLACE,
//                     STATE, WORKSPACE, REST, RUN
//   CF_TYPES          nested types, before the members that name them
//   CF_MEMBERS        members, after the rest
#ifndef CATENAE_TESTS_COMPILE_FAIL_CF_STAGE_H
#define CATENAE_TESTS_COMPILE_FAIL_CF_STAGE_H

#include <cstddef>

#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

#ifndef CF_TYPES
#define CF_TYPES
#endif
#ifndef CF_MEMBERS
#define CF_MEMBERS
#endif

namespace cf {

    class stage {
      public:
        using input_port  = catenae::port<float, 1>;
        using output_port = catenae::port<float, 1>;
#ifndef CF_OMIT_STATE
        struct state {
            float held;
        };
#endif
#ifndef CF_OMIT_WORKSPACE
        using workspace = catenae::no_workspace;
#endif
        CF_TYPES
#ifndef CF_OMIT_RATE_RATIO
        static constexpr catenae::ratio rate_ratio{1, 1};
#endif
#ifndef CF_OMIT_INPUT_GROUP
        static constexpr std::size_t input_group = 1;
#endif
        static constexpr catenae::ratio max_shortfall{0, 1};
#ifndef CF_OMIT_MAX_LEAD
        static constexpr catenae::ratio max_lead{0, 1};
#endif
        static constexpr std::size_t output_quantum = 1;
#ifndef CF_OMIT_IN_PLACE
        static constexpr bool is_in_place = false;
#endif
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};
#ifndef CF_OMIT_REST
        static constexpr bool rests_at_zero = true;
#endif
#ifndef CF_OMIT_RUN
        std::size_t run(state& st, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                out[i]  = in[i] + st.held;
                st.held = in[i];
            }
            return in.size();
        }
#endif
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 1; }
        std::size_t                  practical_tail_frames() const noexcept { return 1; }
        float                        flush_residue() const noexcept { return 0.0f; }
        CF_MEMBERS
    };

} // namespace cf

// Reading a constant of the traits instantiates them, and with them every assertion.
static_assert(catenae::stage_traits<cf::stage>::input_group >= 1, "the traits instantiate");

#endif // CATENAE_TESTS_COMPILE_FAIL_CF_STAGE_H
