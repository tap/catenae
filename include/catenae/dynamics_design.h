/// @file dynamics_design.h
/// @brief Dynamics designs: a compressor's, a look-ahead limiter's and an expander's numbers,
/// made from a spec in dB and frames, or checked from a table (D37, PLAN.md 2).
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | A design    | `compressor_design<S>`, `limiter_design<S>`, `expander_design<S>`: the numbers |
/// |             | its stage runs, in S; made only by `make()` or `from_table()`                |
/// | Levels      | in nepers, the natural logarithm of an amplitude: dB x ln 10 / 20 (1 Np is    |
/// |             | 8.686 dB), so the per-frame path takes one `log` and one `exp` in S           |
/// | Compressor  | threshold T, ratio R from 1 to infinity, knee W >= 0 (dB), attack and release |
/// |             | time constants (frames), make-up gain M (dB): Giannoulis, Massberg and Reiss,  |
/// |             | "Digital Dynamic Range Compressor Design: A Tutorial and Analysis", JAES 60(6), |
/// |             | 2012, its gain computer and smooth decoupled peak detector                     |
/// | Limiter     | threshold T (dB), the ceiling no sample exceeds, and a release (frames); the   |
/// |             | look-ahead is its stage's type                                                 |
/// | Expander    | threshold T, ratio R from 1 to infinity (infinity: a gate), knee W, range      |
/// |             | (the most it cuts, dB, above 0), attack, hold (whole frames) and release:     |
/// |             | Reiss and McPherson, "Audio Effects: Theory, Implementation and Application",  |
/// |             | CRC Press, 2014, ch. 6, its downward expander and gate                        |
/// | Times       | frames, as a time constant tau >= 0: a one-pole of coefficient exp(-1 / tau),  |
/// |             | its complement -expm1(-1 / tau), both made in double; tau = 0 is instant       |
/// | Rejects     | `status::invalid_argument`: a NaN, an infinite threshold, gain, knee, range or |
/// |             | time, a ratio below 1, a negative knee or time, a range not above 0;          |
/// |             | `status::out_of_range`: a threshold, make-up gain, knee or range beyond 200 dB |
/// |             | in magnitude, or a time constant whose coefficient rounds to 1 in S            |
/// | Table       | `from_table(numbers)` checks the numbers' ranges and finiteness in S: a table |
/// |             | that fails does not initialise a constant, and at run time reaches             |
/// |             | `check::table_rejected`                                                       |
/// | Cost        | `make()` runs in the caller's frame, outside the audio path: a few `exp` and   |
/// |             | `expm1` in double. Cortex-M33, worst call (Bench run 46001165800): the         |
/// |             | compressor's 11,225 instructions, the limiter's 6,617, the expander's 11,262;  |
/// |             | 452, 300 and 476 B of stack                                                    |
/// | Formats     | float and double; q31 declares its contract, its numbers held in float until   |
/// |             | its profile arrives at C6 (D47)                                                |
/// | Rationale   | D12, D24, D37, D47, D54, PLAN.md 2                                             |
///
/// @req{CAT-DYNAMICS-DESIGN-001} Each design's numbers are its spec's: the threshold, knee,
/// gains and range in nepers, the knee's half width and quadratic scale, the ratio's slopes,
/// and each time constant's coefficient and complement, within rounding to S.
/// @req{CAT-DYNAMICS-DESIGN-002} `make()` rejects what its card names with the status it names,
/// and accepts the bounds.
/// @req{CAT-DYNAMICS-DESIGN-003} `from_table` accepts a design's own numbers bit for bit and
/// refuses one out of its range: in a constant it does not compile
/// (`compile_fail/dynamics_table_out_of_range.cpp`), at run time `check::table_rejected`.
/// @req{CAT-DYNAMICS-DESIGN-004} In q31 the designs' contracts are instantiated; `make()` does
/// not compile until the profile arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_DYNAMICS_DESIGN_H
#define CATENAE_DYNAMICS_DESIGN_H

#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// What a compressor is designed from.
        struct compressor_spec {
            double threshold_db;   ///< T, dB re full scale
            double ratio;          ///< R, 1 to infinity (infinity: a limiter's slope)
            double knee_db;        ///< W, 0 for a hard knee
            double attack_frames;  ///< the attack's time constant, frames
            double release_frames; ///< the release's time constant, frames
            double makeup_db;      ///< M, applied after the reduction
        };

        /// What a look-ahead limiter is designed from.
        struct limiter_spec {
            double threshold_db;   ///< the ceiling, dB re full scale
            double release_frames; ///< the release's time constant, frames
        };

        /// What an expander is designed from.
        struct expander_spec {
            double        threshold_db;   ///< T, dB re full scale: below it the level is expanded
            double        ratio;          ///< R, 1 to infinity (infinity: a gate)
            double        knee_db;        ///< W, 0 for a hard knee
            double        range_db;       ///< the most it cuts, dB, above 0
            double        attack_frames;  ///< the opening's time constant, frames
            std::uint32_t hold_frames;    ///< frames it stays open after the level falls
            double        release_frames; ///< the closing's time constant, frames
        };

        /// A compressor's numbers, in nepers and one-pole coefficients.
        template <typename T>
        struct compressor_numbers {
            T threshold;          ///< T
            T half_knee;          ///< W / 2
            T above;              ///< 1 - 1 / R: the reduction per neper above the knee
            T knee_scale;         ///< (1 - 1 / R) / (2 W), 0 for a hard knee
            T makeup;             ///< M
            T attack;             ///< exp(-1 / tau), the attack's
            T attack_complement;  ///< 1 - it, made as -expm1(-1 / tau)
            T release;            ///< the release's coefficient
            T release_complement; ///< 1 - it
        };

        /// A limiter's numbers.
        template <typename T>
        struct limiter_numbers {
            T threshold;          ///< T, nepers
            T ceiling;            ///< exp(T), the largest magnitude it writes
            T release;            ///< the release's coefficient
            T release_complement; ///< 1 - it
        };

        /// An expander's numbers.
        template <typename T>
        struct expander_numbers {
            T             threshold;          ///< T
            T             half_knee;          ///< W / 2
            T             below;              ///< R - 1: the reduction per neper below the knee
            T             knee_scale;         ///< (R - 1) / (2 W), 0 for a hard knee
            T             range;              ///< the most it cuts, nepers
            T             attack;             ///< the opening's coefficient
            T             attack_complement;  ///< 1 - it
            T             release;            ///< the closing's coefficient
            T             release_complement; ///< 1 - it
            std::uint32_t hold;               ///< frames
        };

        namespace detail {
            /// What a dynamics design holds its numbers in: the sample's own type when it
            /// floats, float for a fixed-point format until its profile arrives (C6).
            template <typename S>
            using dynamics_value_t = std::conditional_t<format_traits<S>::is_fixed_point, float, storage_t<S>>;

            // The compiled designers (src/dynamics_design.cpp): design-time arithmetic in double,
            // the spec's checks of the card. `make()` rounds to S and checks what rounding can break.

            /// A compressor's numbers in double, or why there are none.
            expected<compressor_numbers<double>, status> compressor_numbers_of(const compressor_spec& spec) noexcept;
            /// A limiter's numbers in double, or why there are none.
            expected<limiter_numbers<double>, status> limiter_numbers_of(const limiter_spec& spec) noexcept;
            /// An expander's numbers in double, or why there are none.
            expected<expander_numbers<double>, status> expander_numbers_of(const expander_spec& spec) noexcept;

            /// Whether x is finite (false for a NaN).
            template <typename T>
            constexpr bool dynamics_finite(T x) noexcept {
                return x <= std::numeric_limits<T>::max() && x >= -std::numeric_limits<T>::max();
            }
            /// Whether x is at least 0, infinity allowed (false for a NaN).
            template <typename T>
            constexpr bool dynamics_not_negative(T x) noexcept {
                return x >= T{0};
            }
            /// Whether a coefficient and its complement are a one-pole's: alpha in [0, 1), its
            /// complement in (0, 1].
            template <typename T>
            constexpr bool one_pole(T alpha, T complement) noexcept {
                return alpha >= T{0} && alpha < T{1} && complement > T{0} && complement <= T{1};
            }

            template <typename T>
            constexpr bool valid_numbers(const compressor_numbers<T>& n) noexcept {
                return dynamics_finite(n.threshold) && dynamics_finite(n.half_knee) && n.half_knee >= T{0}
                       && n.above >= T{0} && n.above <= T{1} && dynamics_finite(n.knee_scale) && n.knee_scale >= T{0}
                       && dynamics_finite(n.makeup) && one_pole(n.attack, n.attack_complement)
                       && one_pole(n.release, n.release_complement);
            }
            template <typename T>
            constexpr bool valid_numbers(const limiter_numbers<T>& n) noexcept {
                return dynamics_finite(n.threshold) && dynamics_finite(n.ceiling) && n.ceiling > T{0}
                       && one_pole(n.release, n.release_complement);
            }
            template <typename T>
            constexpr bool valid_numbers(const expander_numbers<T>& n) noexcept {
                return dynamics_finite(n.threshold) && dynamics_finite(n.half_knee) && n.half_knee >= T{0}
                       && dynamics_not_negative(n.below) && dynamics_not_negative(n.knee_scale)
                       && dynamics_finite(n.range) && n.range > T{0} && one_pole(n.attack, n.attack_complement)
                       && one_pole(n.release, n.release_complement);
            }

            /// Every number of a design in double, rounded to T.
            template <typename T>
            constexpr compressor_numbers<T> rounded(const compressor_numbers<double>& n) noexcept {
                return {static_cast<T>(n.threshold),
                        static_cast<T>(n.half_knee),
                        static_cast<T>(n.above),
                        static_cast<T>(n.knee_scale),
                        static_cast<T>(n.makeup),
                        static_cast<T>(n.attack),
                        static_cast<T>(n.attack_complement),
                        static_cast<T>(n.release),
                        static_cast<T>(n.release_complement)};
            }
            template <typename T>
            constexpr limiter_numbers<T> rounded(const limiter_numbers<double>& n) noexcept {
                return {static_cast<T>(n.threshold), static_cast<T>(n.ceiling), static_cast<T>(n.release),
                        static_cast<T>(n.release_complement)};
            }
            template <typename T>
            constexpr expander_numbers<T> rounded(const expander_numbers<double>& n) noexcept {
                return {static_cast<T>(n.threshold),
                        static_cast<T>(n.half_knee),
                        static_cast<T>(n.below),
                        static_cast<T>(n.knee_scale),
                        static_cast<T>(n.range),
                        static_cast<T>(n.attack),
                        static_cast<T>(n.attack_complement),
                        static_cast<T>(n.release),
                        static_cast<T>(n.release_complement),
                        n.hold};
            }

            /// A dynamics design: its numbers, made by `make()` from a spec or checked from a
            /// table. `Numbers` is the numbers' template, `Spec` the spec, `Of` the designer.
            template <typename S, template <typename> class Numbers, typename Spec,
                      expected<Numbers<double>, status> (*Of)(const Spec&) noexcept>
            class dynamics_design {
                static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                                  || std::is_same<S, q31>::value,
                              "catenae: a dynamics design is float, double or q31");

              public:
                using value_type   = dynamics_value_t<S>; ///< what its numbers are held in
                using numbers_type = Numbers<value_type>; ///< the numbers
                using spec_type    = Spec;                ///< what make() takes

                /// The design for `spec` (the card's statuses). Outside the audio path.
                static expected<dynamics_design, status> make(const spec_type& spec) noexcept {
                    static_assert(!format_traits<S>::is_fixed_point || k_never<S>,
                                  "catenae: a fixed-point dynamics design is new design and arrives with its "
                                  "profile at C6 (D47)");
                    const expected<Numbers<double>, status> exact = Of(spec);
                    if (!exact) {
                        return unexpected(exact.error());
                    }
                    const numbers_type n = rounded<value_type>(*exact);
                    if (!valid_numbers(n)) {
                        return unexpected(status::out_of_range);
                    }
                    return dynamics_design(n);
                }

                /// A design from its numbers, checked: the card's ranges, in S. A table that fails
                /// does not initialise a constant; at run time it reaches the handler.
                static constexpr dynamics_design from_table(const numbers_type& numbers) noexcept {
                    check_always(valid_numbers(numbers), check::table_rejected);
                    return dynamics_design(numbers);
                }

                /// The numbers its stage runs.
                constexpr const numbers_type& numbers() const noexcept { return m_numbers; }

              private:
                constexpr explicit dynamics_design(const numbers_type& numbers) noexcept
                    : m_numbers(numbers) {}

                numbers_type m_numbers;
            };
        } // namespace detail

        /// @brief A compressor's design in S.
        template <typename S>
        using compressor_design =
            detail::dynamics_design<S, compressor_numbers, compressor_spec, &detail::compressor_numbers_of>;
        /// @brief A look-ahead limiter's design in S.
        template <typename S>
        using limiter_design = detail::dynamics_design<S, limiter_numbers, limiter_spec, &detail::limiter_numbers_of>;
        /// @brief An expander's design in S.
        template <typename S>
        using expander_design =
            detail::dynamics_design<S, expander_numbers, expander_spec, &detail::expander_numbers_of>;

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DYNAMICS_DESIGN_H
