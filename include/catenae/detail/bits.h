/// @file bits.h
/// @brief Whether two values have the same bits: a floating comparison with no `==` (DEV-006).
// Copyright 2026 Timothy Place and the catenae contributors
//
// A stage that notices a changed parameter compares the stored value's bits, not its value:
// `==` on floating values is a warning under the sets (-Wfloat-equal), and a bit comparison
// is what "unchanged" means for a parameter (a NaN set again is unchanged; -0 is not +0).
#ifndef CATENAE_DETAIL_BITS_H
#define CATENAE_DETAIL_BITS_H

#include <cstring>
#include <type_traits>

#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// Whether a and b have the same object representation (a memcpy bit cast, DEV-006).
            template <typename T>
            inline bool same_bits(const T& a, const T& b) noexcept {
                static_assert(std::is_trivially_copyable<T>::value, "catenae: same_bits compares plain values");
                return std::memcmp(&a, &b, sizeof(T)) == 0;
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_BITS_H
