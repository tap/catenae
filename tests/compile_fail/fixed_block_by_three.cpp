// Copyright 2026 Timothy Place and the catenae contributors
// A decimator by 3 (input group 3) given a fixed block of 64 frames.
// Verifies CAT-CHAIN-004.
// Expect: a fixed block must be a whole number of the chain's input groups; wrap the chain in steady
#include <array>

#include "cf_ports.h"

namespace cf {
    /// Down by 3, writing on a group's first frame.
    struct down3 : between<catenae::port<float, 1>, catenae::port<float, 1>> {
        static constexpr catenae::ratio rate_ratio{1, 3};
        static constexpr std::size_t    input_group = 3;
        static constexpr catenae::ratio max_lead{2, 3};
        static constexpr std::size_t    max_outputs_for(std::size_t n) noexcept { return (n + 2) / 3; }
        static constexpr std::size_t    outputs_from_rest(std::size_t n) noexcept { return (n + 2) / 3; }
    };
} // namespace cf
namespace catenae {
    template <>
    struct is_stage<cf::down3> : std::true_type {};
} // namespace catenae

constexpr auto k_chain = catenae::chain(cf::down3{});

#ifdef CATENAE_CF_CONTROL
constexpr std::size_t k_frames = 63;
#else
constexpr std::size_t k_frames = 64;
#endif

std::size_t by_three(catenae::memory_of<decltype(k_chain)>& m, const std::array<float, k_frames>& in,
                     std::array<float, 32>& out) noexcept;
std::size_t by_three(catenae::memory_of<decltype(k_chain)>& m, const std::array<float, k_frames>& in,
                     std::array<float, 32>& out) noexcept {
    return k_chain.process(m, in, out);
}
