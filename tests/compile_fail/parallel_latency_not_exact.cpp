// Copyright 2026 Timothy Place and the catenae contributors
// An arm whose latency is not exact, which only the caller can align.
// Verifies CAT-PARALLEL-002.
// Expect: catenae: parallel<> aligns arms whose latency is exact
#include <type_traits>

#include "catenae/gain.h"
#include "catenae/parallel.h"

namespace cf {
    /// A gain that says its latency is not exact, as a designed recursive filter would.
    struct recursive_arm : catenae::gain<1> {
        static constexpr bool has_exact_latency = false;
    };
} // namespace cf
namespace catenae {
    template <>
    struct is_stage<cf::recursive_arm> : std::true_type {};
} // namespace catenae

#ifdef CATENAE_CF_CONTROL
constexpr catenae::align k_align = catenae::align::by_caller;
#else
constexpr catenae::align k_align = catenae::align::by_type;
#endif
static_assert(catenae::parallel<cf::recursive_arm, catenae::gain<1>, k_align>::input_group == 1, "the type is formed");
