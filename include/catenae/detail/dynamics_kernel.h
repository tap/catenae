/// @file dynamics_kernel.h
/// @brief The dynamics stages' per-frame arithmetic: a frame's level, the gain computers and
/// the smooth decoupled peak detector, in the log domain (nepers).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Giannoulis, Massberg and Reiss ("Digital Dynamic Range Compressor Design: A Tutorial and
// Analysis", JAES 60(6), 2012): the gain computer takes the level x in dB to the reduction
// x - y(x), and the smooth decoupled peak detector smooths that reduction, a release one-pole
// that a rise overrides, then an attack one-pole:
//
//   y1[n] = max(r[n], alpha_R y1[n-1] + (1 - alpha_R) r[n])
//   y[n]  = alpha_A y[n-1] + (1 - alpha_A) y1[n]
//
// The expander's reduction opens quickly and closes slowly, so its detector is the mirror, a
// min, with a hold before the release (Reiss and McPherson, "Audio Effects", 2014, ch. 6).
#ifndef CATENAE_DETAIL_DYNAMICS_KERNEL_H
#define CATENAE_DETAIL_DYNAMICS_KERNEL_H

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "catenae/detail/abi.h"
#include "catenae/dynamics_design.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// The smallest magnitude a level is taken of, 2^-32 (-192.7 dB): silence has a level.
            template <typename T>
            inline constexpr T k_dynamics_floor = T(2.3283064365386962890625e-10);

            /// Below 2^-64 nepers a detector's value is its rest, 0: its state comes to rest
            /// exactly and never holds a subnormal, whatever the host flushes.
            template <typename T>
            inline constexpr T k_dynamics_rest = T(5.42101086242752217003726400434970855712890625e-20);

            /// The larger of a and b; b where they are equal or a is a NaN.
            template <typename T>
            constexpr T larger(T a, T b) noexcept {
                return a > b ? a : b;
            }

            /// The smaller of a and b; b where they are equal or a is a NaN.
            template <typename T>
            constexpr T smaller(T a, T b) noexcept {
                return a < b ? a : b;
            }

            /// 0 below the rest, the value otherwise.
            template <typename T>
            constexpr T to_rest(T x) noexcept {
                return x < k_dynamics_rest<T> ? T{0} : x;
            }

            /// The level of a frame, in nepers: the log of its largest magnitude, the channels
            /// linked, at least the floor's. A NaN sample is not the largest of anything.
            template <typename T, std::size_t Ch>
            T frame_level(const T* frame) noexcept {
                T peak = k_dynamics_floor<T>;
                for (std::size_t c = 0; c < Ch; ++c) {
                    const T v = frame[c];
                    const T a = v < T{0} ? -v : v;
                    peak      = a > peak ? a : peak;
                }
                return std::log(peak);
            }

            /// The compressor's reduction at level x: 0 below the knee, (1 - 1/R) (x - T) above
            /// it, and the paper's quadratic across it, (1 - 1/R) (x - T + W/2)^2 / (2W).
            template <typename T>
            constexpr T compressor_reduction(T x, const compressor_numbers<T>& n) noexcept {
                const T d = x - n.threshold;
                if (d > n.half_knee) {
                    return n.above * d;
                }
                if (d < -n.half_knee) {
                    return T{0};
                }
                const T t = d + n.half_knee;
                return n.knee_scale * t * t;
            }

            /// The expander's reduction at level x: 0 above the knee, (R - 1) (T - x) below it,
            /// (R - 1) (x - T - W/2)^2 / (2W) across it, and never more than the range. At a
            /// ratio of infinity, the range wherever it is below the knee's top.
            template <typename T>
            constexpr T expander_reduction(T x, const expander_numbers<T>& n) noexcept {
                const T d = x - n.threshold;
                if (d >= n.half_knee) {
                    return T{0};
                }
                T r;
                if (d <= -n.half_knee) {
                    r = n.below * -d; // -d > 0 here unless the knee is hard and d is 0, caught above
                }
                else {
                    const T t = d - n.half_knee; // below 0 strictly
                    r         = n.knee_scale * t * t;
                }
                return smaller(r, n.range);
            }

            /// The compressor's detector: a release that a rise overrides, then the attack.
            template <typename T>
            constexpr void detect_rise(T r, T& y1, T& y, const compressor_numbers<T>& n) noexcept {
                y1 = to_rest(larger(r, n.release * y1 + n.release_complement * r));
                y  = to_rest(n.attack * y + n.attack_complement * y1);
            }

            /// The expander's detector: its mirror, a fall overriding the release, with the
            /// hold before the release begins.
            template <typename T>
            constexpr void detect_fall(T r, T& y1, T& y, std::uint32_t& hold, const expander_numbers<T>& n) noexcept {
                if (!(r > y1)) {
                    y1   = r;
                    hold = n.hold;
                }
                else if (hold > 0) {
                    --hold;
                }
                else {
                    y1 = n.release * y1 + n.release_complement * r;
                }
                y1 = to_rest(y1);
                y  = to_rest(n.attack * y + n.attack_complement * y1);
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_DYNAMICS_KERNEL_H
