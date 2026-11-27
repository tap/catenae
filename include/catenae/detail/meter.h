/// @file meter.h
/// @brief What the meters and the correlators share: the type a window's sum is held in, the
/// one rounding point of a 32-bit fixed-point product, a magnitude and a largest-of (C5, D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DETAIL_METER_H
#define CATENAE_DETAIL_METER_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/detail/shift.h"
#include "catenae/sample_format.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            // The compiled conversions to decibels (src/meter.cpp), for a reading off the audio
            // path: 20 log10 of an amplitude ratio and 10 log10 of a power ratio, in the reading's
            // own type, so a float reading takes no software double. 0 is minus infinity.

            /// 20 log10(ratio).
            float amplitude_db(float ratio) noexcept;
            /// 20 log10(ratio).
            double amplitude_db(double ratio) noexcept;
            /// 10 log10(ratio).
            float power_db(float ratio) noexcept;
            /// 10 log10(ratio).
            double power_db(double ratio) noexcept;

            /// What a reading is converted in: double for double, float otherwise.
            template <typename S>
            using reading_t = std::conditional_t<std::is_same<S, double>::value, double, float>;

            /// 2^e as T, for an exponent of either sign, at compile time.
            template <typename T>
            constexpr T power_of_two(int e) noexcept {
                T value = T{1};
                for (; e > 0; --e) {
                    value *= T{2};
                }
                for (; e < 0; ++e) {
                    value /= T{2};
                }
                return value;
            }

            /// A value of format S over its full scale: 1 for a floating format, 2^-F x the raw
            /// integer for a fixed-point one, so Q4.27 reads above 1 in its headroom.
            template <typename S, typename V>
            inline reading_t<S> over_full_scale(V value) noexcept {
                using reading = reading_t<S>;
                if constexpr (format_traits<S>::is_fixed_point) {
                    constexpr reading scale = power_of_two<reading>(-format_traits<S>::fraction_bits);
                    return static_cast<reading>(value) * scale;
                }
                else {
                    return static_cast<reading>(value);
                }
            }

            /// What a meter sums products in: a floating format's own type (float in float,
            /// D36), 64 bits for a fixed-point format.
            template <typename S>
            using meter_sum_t = std::conditional_t<format_traits<S>::is_fixed_point, std::int64_t, storage_t<S>>;

            /// floor(log2(n)) for n >= 1.
            constexpr int floor_log2(std::size_t n) noexcept {
                int bits = 0;
                for (; n > 1; n /= 2) {
                    ++bits;
                }
                return bits;
            }

            /// The bits each product is shifted right by, so that a window of `Window` of them sums
            /// in 64 bits: floor(log2 Window) for 32-bit storage, whose products reach 2^62, and
            /// none for 16-bit storage (2^30) or a floating format.
            template <typename S, std::size_t Window>
            constexpr int meter_shift() noexcept {
                return format_traits<S>::is_fixed_point && sizeof(storage_t<S>) == 4 ? floor_log2(Window) : 0;
            }

            /// a x b as a term of a meter's sum. A floating format multiplies in its own type; Q15
            /// is exact in 64 bits; a 32-bit format's product is rounded half up to `Shift` fewer
            /// bits, its one rounding point.
            template <typename S, int Shift>
            inline meter_sum_t<S> meter_product(storage_t<S> a, storage_t<S> b) noexcept {
                if constexpr (!format_traits<S>::is_fixed_point) {
                    return a * b;
                }
                else if constexpr (Shift == 0) {
                    return static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b);
                }
                else {
                    const std::int64_t product = static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b);
                    // |product| <= 2^62, so adding half of the last kept bit cannot overflow.
                    return shift_right<std::int64_t>(product + shift_left<std::int64_t>(1, Shift - 1), Shift);
                }
            }

            /// |x|: a floating format's, and a fixed-point one's saturated, so the most negative
            /// value reads as the most positive.
            template <typename S>
            inline storage_t<S> meter_magnitude(storage_t<S> x) noexcept {
                using value_type = storage_t<S>;
                if constexpr (!format_traits<S>::is_fixed_point) {
                    return std::fabs(x);
                }
                else {
                    if (x >= 0) {
                        return x;
                    }
                    return x == std::numeric_limits<value_type>::min() ? std::numeric_limits<value_type>::max()
                                                                       : static_cast<value_type>(-x);
                }
            }

            /// The larger of two magnitudes, a NaN winning and then staying: a meter shows a NaN in
            /// its window rather than reading past it. A floating magnitude is +0 or above, or a NaN
            /// with its sign clear, and for those the bits order as the values do with every NaN above
            /// infinity: so the bits are compared as integers. A compare and select of the values
            /// would say the same, but a compiler may fold it into the target's floating-point
            /// maximum, which on Hexagon drops a NaN.
            template <typename V>
            inline V meter_larger(V held, V next) noexcept {
                if constexpr (std::is_floating_point<V>::value) {
                    using bits_type = std::conditional_t<sizeof(V) == 4, std::uint32_t, std::uint64_t>;
                    static_assert(sizeof(bits_type) == sizeof(V), "catenae: a float of 32 bits, a double of 64");
                    bits_type a = 0;
                    bits_type b = 0;
                    std::memcpy(&a, &held, sizeof a);
                    std::memcpy(&b, &next, sizeof b);
                    return b > a ? next : held;
                }
                else {
                    return next > held ? next : held;
                }
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_METER_H
