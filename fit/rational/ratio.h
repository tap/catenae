/// @file ratio.h
/// @brief The rational engine's charter over `catenae::ratio_t<L, M>` (CF skeleton).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Ported from SampleRateTap's rational/include/tap/sr/rational/ratio.h at 4496888: the
// charter's three static_asserts (L and M of the form 2^a 3^b, in lowest terms, not 1), the
// derived numbers of ratio_traits (band, direction). The C++20 concept became a trait; the
// k_ prefixes became the public names D30 asks for.
//
// CF finding F3, made: catenae's ratio is a value, `catenae::ratio{up, down}`, and a class
// type cannot be a template argument at C++17 (D2), so the skeleton first carried its own
// ratio type. It now names its Ratio with `catenae::ratio_t<L, M>` (include/catenae/ratio.h),
// whose `value` is what the stage publishes as its `rate_ratio`; the charter is checked here.
//
// CF finding F17, made: SampleRateTap's `up2` is a ratio, and PLAN.md 9.4 and C3 spell
// `up2<Ch>` as a stage. The skeleton names no ratio `up2`: it writes `catenae::ratio_t<2, 1>`,
// and the stage keeps the name for C3.
#ifndef CATENAE_FIT_RATIONAL_RATIO_H
#define CATENAE_FIT_RATIONAL_RATIO_H

#include <cstdint>
#include <type_traits>

#include "catenae/ratio.h"

namespace catenae_fit::rational {

    namespace detail {

        /// Whether n is of the form 2^a 3^b (n >= 1).
        constexpr bool is_two_three_smooth(std::uint32_t n) noexcept {
            if (n == 0) {
                return false;
            }
            while (n % 2 == 0) {
                n /= 2;
            }
            while (n % 3 == 0) {
                n /= 3;
            }
            return n == 1;
        }

    } // namespace detail

    /// Whether a type is a catenae::ratio_t<L, M>.
    template <typename T>
    struct is_ratio : std::false_type {};
    template <std::uint32_t L, std::uint32_t M>
    struct is_ratio<catenae::ratio_t<L, M>> : std::true_type {};

    /// What a stage reads from its ratio: the band of its one Nyquist filter and its direction.
    /// The charter is checked here: L output frames per M input frames.
    template <typename R>
    struct ratio_traits {
        static_assert(is_ratio<R>::value, "catenae_fit::rational: a ratio is a catenae::ratio_t<L, M>");
        static_assert(std::is_same<R, typename R::type>::value,
                      "catenae_fit::rational: L/M is written in lowest terms");
        static_assert(detail::is_two_three_smooth(R::up) && detail::is_two_three_smooth(R::down),
                      "catenae_fit::rational: L and M are of the form 2^a 3^b (SampleRateTap's charter, R1)");
        static_assert(R::up != R::down, "catenae_fit::rational: L/M == 1 is no conversion");
        static constexpr std::uint32_t up              = R::up;
        static constexpr std::uint32_t down            = R::down;
        static constexpr bool          is_up           = up > down;         ///< L > M
        static constexpr bool          is_decimator    = up == 1;           ///< 1/M: the M-branch commutator
        static constexpr bool          is_interpolator = down == 1;         ///< L/1
        static constexpr std::uint32_t band            = is_up ? up : down; ///< max(L, M), the filter's band
    };

} // namespace catenae_fit::rational

#endif // CATENAE_FIT_RATIONAL_RATIO_H
