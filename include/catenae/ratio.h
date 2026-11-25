/// @file ratio.h
/// @brief `ratio{up, down}`: a rate, a shortfall, a lead or a latency as an exact fraction.
///
/// | Contract  |                                                                              |
/// |-----------|------------------------------------------------------------------------------|
/// | Fields    | `up`, `down`: unsigned 32-bit; `down` is never 0                             |
/// | Reduced   | `reduce(r)` divides both by their gcd; a stage's `rate_ratio` is reduced   |
/// | Arithmetic| `+`, `-` (never below zero), `*`, comparison, all `constexpr` and exact   |
/// | Rationale | D18, D35, D39                                                                |
///
/// @req{CAT-RATIO-001} Two ratios are equal when they are the same fraction, reduced or not;
/// `reduce` gives the form with gcd(up, down) = 1 and `is_reduced` says whether a ratio is.
/// @req{CAT-RATIO-002} Sums, differences, products and comparisons are exact and `constexpr`,
/// in 64-bit intermediates reduced back to 32 bits.
/// @req{CAT-RATIO-003} A difference below zero is an audit check (`check::invariant`).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_RATIO_H
#define CATENAE_RATIO_H

#include <cstdint>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/fwd.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// An exact non-negative fraction, `up / down`.
        struct ratio {
            std::uint32_t up;   ///< the numerator
            std::uint32_t down; ///< the denominator, never 0
        };

        namespace detail {
            /// The greatest common divisor, with gcd(0, n) = n.
            constexpr std::uint64_t gcd(std::uint64_t a, std::uint64_t b) noexcept {
                while (b != 0) {
                    const std::uint64_t t = a % b;
                    a                     = b;
                    b                     = t;
                }
                return a;
            }
            /// A fraction of 64-bit terms, reduced into a ratio (its terms must then fit).
            constexpr ratio reduce64(std::uint64_t up, std::uint64_t down) noexcept {
                check_audit(down != 0, check::invariant);
                const std::uint64_t g = gcd(up, down) == 0 ? 1 : gcd(up, down);
                return ratio{narrow<std::uint32_t>(up / g), narrow<std::uint32_t>(down / g)};
            }
        } // namespace detail

        /// The same fraction, its terms divided by their greatest common divisor.
        constexpr ratio reduce(ratio r) noexcept {
            return detail::reduce64(r.up, r.down);
        }
        /// Whether a ratio is in its reduced form (0 is 0/1).
        constexpr bool is_reduced(ratio r) noexcept {
            return r.down != 0 && detail::gcd(r.up, r.down) == 1 && (r.up != 0 || r.down == 1);
        }

        /// Whether two ratios are the same fraction, reduced or not.
        constexpr bool operator==(ratio a, ratio b) noexcept {
            return std::uint64_t{a.up} * b.down == std::uint64_t{b.up} * a.down;
        }
        /// Whether two ratios are different fractions.
        constexpr bool operator!=(ratio a, ratio b) noexcept {
            return !(a == b);
        }
        /// Whether `a` is the smaller fraction.
        constexpr bool operator<(ratio a, ratio b) noexcept {
            return std::uint64_t{a.up} * b.down < std::uint64_t{b.up} * a.down;
        }
        /// Whether `a` is the larger fraction.
        constexpr bool operator>(ratio a, ratio b) noexcept {
            return b < a;
        }
        /// Whether `a` is no larger than `b`.
        constexpr bool operator<=(ratio a, ratio b) noexcept {
            return !(b < a);
        }
        /// Whether `a` is no smaller than `b`.
        constexpr bool operator>=(ratio a, ratio b) noexcept {
            return !(a < b);
        }

        /// a + b, reduced.
        constexpr ratio operator+(ratio a, ratio b) noexcept {
            return detail::reduce64(std::uint64_t{a.up} * b.down + std::uint64_t{b.up} * a.down,
                                    std::uint64_t{a.down} * b.down);
        }
        /// a - b, reduced; it must not be below zero.
        constexpr ratio operator-(ratio a, ratio b) noexcept {
            detail::check_audit(!(a < b), check::invariant);
            return detail::reduce64(std::uint64_t{a.up} * b.down - std::uint64_t{b.up} * a.down,
                                    std::uint64_t{a.down} * b.down);
        }
        /// a x b, reduced.
        constexpr ratio operator*(ratio a, ratio b) noexcept {
            return detail::reduce64(std::uint64_t{a.up} * b.up, std::uint64_t{a.down} * b.down);
        }

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_RATIO_H
