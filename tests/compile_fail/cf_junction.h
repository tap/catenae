/// @file cf_junction.h
/// @brief A conforming junction for the compile-fail battery, each member a knob a case may turn.
// Copyright 2026 Timothy Place and the catenae contributors
//
//   CF_OMIT_<MEMBER>  CLOCKING, CAPACITY, STATE
//   CF_OPT_IN         specialise is_junction for it, before the traits below first read it
//   CF_TYPES          nested types, before the members that name them
//   CF_MEMBERS        members, after the rest
#ifndef CATENAE_TESTS_COMPILE_FAIL_CF_JUNCTION_H
#define CATENAE_TESTS_COMPILE_FAIL_CF_JUNCTION_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/junction_traits.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/span.h"

#ifndef CF_TYPES
#define CF_TYPES
#endif
#ifndef CF_MEMBERS
#define CF_MEMBERS
#endif

namespace cf {

    class junction {
      public:
        using sink_port   = catenae::port<float, 1>;
        using source_port = catenae::port<float, 1>;
#ifndef CF_OMIT_STATE
        struct state {
            float         held[8];
            std::uint32_t written;
            std::uint32_t read;
        };
#endif
        CF_TYPES
#ifndef CF_OMIT_CLOCKING
        static constexpr catenae::clocks clocking = catenae::clocks::shared;
#endif
        static constexpr catenae::ratio rate_ratio{1, 1};
#ifndef CF_OMIT_CAPACITY
        static constexpr std::size_t capacity_frames = 8;
#endif
        std::size_t push(state& st, catenae::span<const float> in) const noexcept {
            const std::size_t n = in.size() < 8 - (st.written - st.read) ? in.size() : 8 - (st.written - st.read);
            for (std::size_t i = 0; i < n; ++i) {
                st.held[(st.written + i) % 8] = in[i];
            }
            st.written += static_cast<std::uint32_t>(n);
            return n;
        }
        std::size_t pull(state& st, catenae::span<float> out) const noexcept {
            const std::size_t n = out.size() < st.written - st.read ? out.size() : st.written - st.read;
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = st.held[(st.read + i) % 8];
            }
            st.read += static_cast<std::uint32_t>(n);
            return n;
        }
        static constexpr bool     serves(std::size_t largest_pull, std::size_t) noexcept { return largest_pull <= 8; }
        std::uint32_t             report(const state& st) const noexcept { return st.written - st.read; }
        catenae::junction_latency latency(const state&) const noexcept { return {0.0f, 0.0f}; }
        void                      reset(state& st) const noexcept { st.written = st.read = 0; }
        void                      restart(state& st) const noexcept { st.read = st.written; }
        CF_MEMBERS
    };

} // namespace cf

#ifdef CF_OPT_IN
// Here, and not in a case: the traits below instantiate is_junction<cf::junction>, and a
// specialisation after that is ill-formed (MSVC refuses it, C2908).
namespace catenae {
    template <>
    struct is_junction<cf::junction> : std::true_type {};
} // namespace catenae
#endif

static_assert(catenae::junction_traits<cf::junction>::capacity_frames >= 1, "the traits instantiate");

#endif // CATENAE_TESTS_COMPILE_FAIL_CF_JUNCTION_H
