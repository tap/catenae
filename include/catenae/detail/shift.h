/// @file shift.h
/// @brief The one helper every shift of a signed value goes through (PLAN.md section 7.3).
///
/// In C++17 a right shift of a negative value is implementation-defined and a left shift of
/// one is undefined. These two functions define both on every compiler: `shift_left` is a
/// multiplication by 2^bits, `shift_right` a division by 2^bits rounded toward negative
/// infinity (the arithmetic shift two's complement hardware performs), each `constexpr`.
/// scripts/check_shifts.py rejects a raw shift of a signed value anywhere else in the library.
///
/// @req{CAT-SHIFT-001} `shift_right(v, b)` is floor(v / 2^b) for every value of a signed type,
/// negative ones included, and `shift_left(v, b)` is v x 2^b, both evaluated in a constant
/// expression; `bits` is from 0 to the type's width less 2, and the product must fit.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DETAIL_SHIFT_H
#define CATENAE_DETAIL_SHIFT_H

#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// value x 2^bits, for a signed integer the result fits.
            template <typename T>
            constexpr T shift_left(T value, int bits) noexcept {
                static_assert(std::is_integral<T>::value && std::is_signed<T>::value,
                              "catenae: shift_left is for signed integers");
                // A multiplication by a power of two: defined for a negative value too.
                return narrow<T>(value * (T{1} << bits));
            }

            /// floor(value / 2^bits), for a signed integer.
            template <typename T>
            constexpr T shift_right(T value, int bits) noexcept {
                static_assert(std::is_integral<T>::value && std::is_signed<T>::value,
                              "catenae: shift_right is for signed integers");
                // Both shifts are of a value that is not negative, so neither is implementation-defined
                // (DEV-007).
                if (value >= 0) {
                    return narrow<T>(value >> bits);
                }
                // -1 - value is not negative and cannot overflow, so its shift is defined.
                return narrow<T>(-1 - ((-1 - value) >> bits));
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_SHIFT_H
