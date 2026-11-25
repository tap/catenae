/// @file cf_analyzer.h
/// @brief A conforming analyzer for the compile-fail battery, each member a knob a case may turn.
// Copyright 2026 Timothy Place and the catenae contributors
//
//   CF_OMIT_<MEMBER>  REST, RESULTS_READY
//   CF_MEMBERS        members, after the rest
#ifndef CATENAE_TESTS_COMPILE_FAIL_CF_ANALYZER_H
#define CATENAE_TESTS_COMPILE_FAIL_CF_ANALYZER_H

#include <cstddef>
#include <cstdint>

#include "catenae/analyzer_traits.h"
#include "catenae/port.h"
#include "catenae/span.h"

#ifndef CF_MEMBERS
#define CF_MEMBERS
#endif

namespace cf {

    class analyzer {
      public:
        using input_port                           = catenae::port<float, 1>;
        using result_type                          = float;
        static constexpr std::size_t hop_frames    = 4;
        static constexpr std::size_t window_frames = 4;
#ifndef CF_OMIT_REST
        static constexpr bool rests_at_zero = true;
#endif
        struct state {
            float         sum;
            float         last;
            std::uint32_t frames;
            std::uint32_t ready;
        };
        void analyze(state& st, catenae::span<const float> in) const noexcept {
            for (const float v : in) {
                st.sum += v;
                if (++st.frames == hop_frames) {
                    st.last   = st.sum;
                    st.sum    = 0.0f;
                    st.frames = 0;
                    ++st.ready;
                }
            }
        }
        float result(const state& st) const noexcept { return st.last; }
#ifndef CF_OMIT_RESULTS_READY
        std::uint32_t results_ready(const state& st) const noexcept { return st.ready; }
#endif
        CF_MEMBERS
    };

} // namespace cf

static_assert(!catenae::analyzer_traits<cf::analyzer>::opted_in, "the traits instantiate");

#endif // CATENAE_TESTS_COMPILE_FAIL_CF_ANALYZER_H
