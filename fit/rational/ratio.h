/// @file ratio.h
/// @brief `ratio<L, M>`: the rational engine's compile-time ratio, a type (CF skeleton).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Ported from SampleRateTap's rational/include/tap/sr/rational/ratio.h at 4496888: the
// charter's three static_asserts (L and M of the form 2^a 3^b, in lowest terms, not 1), the
// derived numbers of ratio_traits (band, direction) and the named single-stage ratios. The
// C++20 concept became a trait; the k_ prefixes became the public names D30 asks for.
//
// CF finding 1: catenae's ratio vocabulary is a value, `catenae::ratio{up, down}`
// (include/catenae/ratio.h), and a class type cannot be a template argument at C++17 (D2), so
// `stage<S, Ch, Ratio, Design>` (PLAN.md 3.3) has no catenae type to name its Ratio with. This
// type carries the value as `ratio<L, M>::value`, which is what the stage publishes as its
// `rate_ratio`.
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

    /// The ratio L/M of one stage or chain, as a type: L output frames per M input frames. The
    /// charter is the whole definition.
    template <std::uint32_t L, std::uint32_t M>
    struct ratio {
        static_assert(detail::is_two_three_smooth(L) && detail::is_two_three_smooth(M),
                      "catenae_fit::rational: L and M are of the form 2^a 3^b (SampleRateTap's charter, R1)");
        static_assert(catenae::detail::gcd(L, M) == 1, "catenae_fit::rational: L/M is in lowest terms");
        static_assert(L != M, "catenae_fit::rational: L/M == 1 is no conversion");

        static constexpr std::uint32_t  up   = L;    ///< output frames per superblock
        static constexpr std::uint32_t  down = M;    ///< input frames per superblock
        static constexpr catenae::ratio value{L, M}; ///< the same, as catenae's value
    };

    /// Whether a type is a ratio<L, M>.
    template <typename T>
    struct is_ratio : std::false_type {};
    template <std::uint32_t L, std::uint32_t M>
    struct is_ratio<ratio<L, M>> : std::true_type {};

    /// What a stage reads from its ratio: the band of its one Nyquist filter and its direction.
    template <typename R>
    struct ratio_traits {
        static_assert(is_ratio<R>::value, "catenae_fit::rational: a ratio is a ratio<L, M>");
        static constexpr std::uint32_t up              = R::up;
        static constexpr std::uint32_t down            = R::down;
        static constexpr bool          is_up           = up > down;         ///< L > M
        static constexpr bool          is_decimator    = up == 1;           ///< 1/M: the M-branch commutator
        static constexpr bool          is_interpolator = down == 1;         ///< L/1
        static constexpr std::uint32_t band            = is_up ? up : down; ///< max(L, M), the filter's band
    };

    /// The single-stage vocabulary this skeleton instantiates, named as SampleRateTap names it.
    ///
    /// CF finding 8: SampleRateTap's `up2` is a ratio; PLAN.md 9.4 and C3 spell `up2<Ch>` as a
    /// stage. catenae needs one meaning before C3 names its stages.
    using up2         = ratio<2, 1>;
    using down2       = ratio<1, 2>;
    using up3         = ratio<3, 1>;
    using down3       = ratio<1, 3>;
    using ratio3over2 = ratio<3, 2>;
    using ratio2over3 = ratio<2, 3>;

} // namespace catenae_fit::rational

#endif // CATENAE_FIT_RATIONAL_RATIO_H
