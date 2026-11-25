/// @file convert.h
/// @brief The one named helper for conversions between fixed-width types and `int`, `long`
/// and `size_t` (PLAN.md section 7.3).
///
/// `narrow<To>(from)` converts an integer to another integer type, a deliberate conversion at a
/// place a reviewer can find. It is the identity when the two types are one (so no cast is
/// useless on a target where, say, `std::int32_t` is `int`), and at the audit level it checks
/// that the value survives (`check::invariant`).
///
/// @req{CAT-CONVERT-001} `narrow<To>(v)` is `v` as a `To` for every `v` that `To` holds, in a
/// constant expression too; at the audit level a `v` that `To` cannot hold is a check.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DETAIL_CONVERT_H
#define CATENAE_DETAIL_CONVERT_H

#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// Whether an integer is below zero, with no comparison an unsigned type makes trivially.
            template <typename I>
            constexpr bool is_negative(I value) noexcept {
                if constexpr (std::is_signed<I>::value) {
                    return value < I{};
                }
                else {
                    static_cast<void>(value);
                    return false;
                }
            }

            /// `from` as a `To`; the value must be one `To` holds (checked at the audit level).
            template <typename To, typename From>
            constexpr To narrow(From from) noexcept {
                static_assert(std::is_integral<To>::value && std::is_integral<From>::value,
                              "catenae: narrow converts between integer types");
                if constexpr (std::is_same<To, From>::value) {
                    return from;
                }
                else {
                    const auto to = static_cast<To>(from);
                    // Same value, and same sign: the round trip and the comparison with zero.
                    check_audit(static_cast<From>(to) == from && is_negative(to) == is_negative(from),
                                check::invariant);
                    return to;
                }
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_CONVERT_H
