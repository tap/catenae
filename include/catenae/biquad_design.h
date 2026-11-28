/// @file biquad_design.h
/// @brief Recursive designs: a biquad section from the Audio EQ Cookbook, an even-order
/// Butterworth low-pass as a cascade of them, and a checked table (D37, PLAN.md 3.3, 9.2).
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | A design    | `biquad_design<S, Sections>`: each section's b0, b1, b2, a1, a2 in the kernel's |
/// |             | type, and the numbers its stage reports; made only by `make()` or `from_table()` |
/// | Cookbook    | `biquad_design<S>::make(biquad_spec)`: low-pass, high-pass, band-pass of skirt |
/// |             | gain Q and of 0 dB peak, notch, all-pass, peaking, low and high shelf, at f0 and |
/// |             | Q, the last three with a gain in dB (Bristow-Johnson, "Cookbook formulae for   |
/// |             | audio EQ biquad filter coefficients"; the shelves in its Q form)               |
/// | Butterworth | `butterworth_design<S, Order>::make(cutoff)`: even orders 2 to 16, -3 dB at the |
/// |             | cutoff, bilinear with pre-warping, poles in increasing radius (Oppenheim and  |
/// |             | Schafer, "Discrete-Time Signal Processing", 3rd ed., 7.2; DspTap, NOTICE.md)  |
/// | Frequencies | cycles per sample, `min_frequency<S>` to 1/2 less it: 1e-4 in float (below it |
/// |             | float loses a Butterworth pole, DspTap's measurement), 1e-5 in double         |
/// | Rejects     | `status::invalid_argument`: a NaN or infinite argument, a Q not above 0;      |
/// |             | `status::out_of_range`: a frequency outside the bounds, or coefficients that in |
/// |             | S are not finite or put a pole on or outside the unit circle                 |
/// | Latency     | `{nominal, deviation}` in frames, of the double design: the group delay at DC |
/// |             | (Nyquist for the high-pass, f0 for the band-passes), and its largest departure |
/// |             | over the points of 1,025 from 0 to 1/2 within 3 dB of the largest gain (all of |
/// |             | them for peaking, shelving and all-pass); a Butterworth's over 1,025 points  |
/// |             | from 0 to its cutoff, DspTap's definition                                     |
/// | Tails       | of the coefficients in S, plus one frame per section: exact, where DspTap's    |
/// |             | binary search over A^(2^j) finds ln(S's largest) + ln ||A^n|| below ln(S's    |
/// |             | smallest normal); practical, where it finds ln ||A^n|| below ln 1e-6 (120 dB)  |
/// | Limits      | ||A^n|| rises and falls within the slowest pole's period: the search's n lies   |
/// |             | between the first frame the bound holds and the last it fails (18 frames short |
/// |             | at order 8, 0.01, float); from full scale a state decays far sooner than that  |
/// | Residue     | 256 times S's smallest normal after the exact tail (DspTap's bound), pinned at |
/// |             | the battery's designs under either flush-to-zero setting                      |
/// | Cost        | `make()` runs in the caller's frame, outside the audio path. Cortex-M33, worst |
/// |             | call (Bench run 45837885047): a cookbook section 28.2 M instructions, 1,380 B of |
/// |             | stack; order 2, 4, 8, 16: 9.8, 16.8, 33.0, 74.8 M, 1,300, 2,008, 6,120, 22,008 B |
/// | Table       | `from_table(rows, numbers)` checks, exactly in S, each row's poles inside the |
/// |             | unit circle and its coefficients finite: a failing table does not initialise a |
/// |             | constant, and one met at run time reaches `check::table_rejected`              |
/// | Formats     | float and double; q31 declares its contract (Q2.30 coefficients, the table    |
/// |             | check in them), its design arriving with its section scaling at C6 (D47)      |
/// | Rationale   | D24, D37, D47, D54, PLAN.md 3.3                                               |
///
/// @req{CAT-BIQUAD-DESIGN-001} Each of the cookbook's nine is its formula: at the battery's
/// f0, Q and gains its coefficients are the printed formula's within rounding, its gain at f0
/// the response's (Q, 0 dB, zero, the gain, half of it for a shelf; unity everywhere for the
/// all-pass), and its nominal latency its group delay at the reference frequency.
/// @req{CAT-BIQUAD-DESIGN-002} A Butterworth of order N is -3.0103 dB at its cutoff, unity at
/// DC and zero at Nyquist; its sections are the cookbook low-pass at the Butterworth Qs within
/// rounding, in increasing pole radius.
/// @req{CAT-BIQUAD-DESIGN-003} `make()` rejects what its card names with the status it names,
/// and accepts the bounds at every order.
/// @req{CAT-BIQUAD-DESIGN-004} Each tail lies between the first frame its bound holds and the
/// last frame it fails, counted by multiplying the kernel's own transition in frame by frame,
/// plus one per section; the plan's figures (14,736 and 149,085 frames) are reproduced.
/// @req{CAT-BIQUAD-DESIGN-005} `from_table` accepts a stable table bit for bit and refuses a
/// pole on or outside the unit circle or a coefficient that is not finite: in a constant it does
/// not compile (`compile_fail/biquad_table_unstable.cpp`), at run time `check::table_rejected`.
/// @req{CAT-BIQUAD-DESIGN-006} In q31 the coefficients are Q2.30 and the table check holds in
/// them; `make()` does not compile until the section scaling arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_BIQUAD_DESIGN_H
#define CATENAE_BIQUAD_DESIGN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/biquad_kernel.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/recursive_tail.h"
#include "catenae/detail/shift.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// The cookbook's responses.
        enum class biquad_response : std::uint8_t {
            lowpass,        ///< low-pass, -3 dB-ish at f0 by Q (Q = 0.7071 is Butterworth)
            highpass,       ///< high-pass
            bandpass_skirt, ///< band-pass, constant skirt gain: its peak gain is Q
            bandpass_peak,  ///< band-pass, constant 0 dB peak gain
            notch,          ///< notch: zero gain at f0
            allpass,        ///< all-pass: unity gain, its phase turning through f0
            peaking,        ///< peaking EQ: the gain at f0, unity far from it
            low_shelf,      ///< low shelf: the gain below f0, unity above
            high_shelf,     ///< high shelf: the gain above f0, unity below
        };

        /// What the cookbook designs one section from.
        struct biquad_spec {
            biquad_response response;  ///< which formula
            double          frequency; ///< f0, in cycles per sample
            double          q;         ///< Q, above 0 (the shelves take the cookbook's Q form too)
            double          gain_db;   ///< peaking and shelving: the gain in dB; unread otherwise
        };

        /// The numbers a recursive design stores beside its coefficients, and a stage reports.
        struct biquad_numbers {
            float       nominal_latency;       ///< frames
            float       latency_deviation;     ///< frames
            std::size_t tail_frames;           ///< the exact tail
            std::size_t practical_tail_frames; ///< 120 dB of decay
            float       flush_residue;         ///< the bound after the exact tail
        };

        /// The lowest frequency a design in S is made at, and the highest's distance from 1/2,
        /// in cycles per sample: float's rounding of a1 and a2 loses a Butterworth pole to the
        /// unit circle below 4.7e-5 (DspTap's measurement), and 1e-4 keeps 4.4 times that margin.
        template <typename S>
        inline constexpr double min_frequency = std::is_same<S, float>::value ? 1e-4 : 1e-5;

        namespace detail {
            /// A section's coefficient: the sample's own type when it floats, Q2.30 in an int32
            /// for a fixed-point format (the recursive kernel's coefficients; arithmetic at C6).
            template <typename S>
            using biquad_coefficient_t =
                std::conditional_t<format_traits<S>::is_fixed_point, std::int32_t, storage_t<S>>;

            /// Whether c < 1 + a, exactly: the sum split into its rounded value and its error
            /// (Knuth's two-sum), so a pole a rounding away from the unit circle is judged by
            /// the coefficients as they are.
            template <typename C>
            constexpr bool below_one_plus(C c, C a) noexcept {
                const C s   = C{1} + a;
                const C bb  = s - C{1};
                const C err = (C{1} - (s - bb)) + (a - bb);
                return c < s || (!(c > s) && err > C{0});
            }

            /// Whether every coefficient is finite (false for a NaN).
            template <typename C>
            constexpr bool finite_row(const biquad_row<C>& r) noexcept {
                bool ok = true;
                for (const C c : r) {
                    ok = ok && c <= std::numeric_limits<C>::max() && c >= -std::numeric_limits<C>::max();
                }
                return ok;
            }

            /// Whether the row's poles, of 1 + a1 z^-1 + a2 z^-2, are inside the unit circle:
            /// |a2| < 1 and |a1| < 1 + a2 (the stability triangle), judged exactly in the row's
            /// own type; in Q2.30, against 2^30.
            template <typename C>
            constexpr bool stable_row(const biquad_row<C>& r) noexcept {
                if constexpr (std::is_floating_point<C>::value) {
                    return finite_row(r) && r[4] < C{1} && r[4] > C{-1} && below_one_plus(r[3], r[4])
                           && below_one_plus(-r[3], r[4]);
                }
                else {
                    constexpr std::int64_t one = detail::shift_left(std::int64_t{1}, 30);
                    const std::int64_t     a1  = r[3];
                    const std::int64_t     a2  = r[4];
                    return a2 < one && a2 > -one && a1 < one + a2 && -a1 < one + a2;
                }
            }

            /// Whether every row of a table is.
            template <typename C, std::size_t K>
            constexpr bool stable_table(const std::array<biquad_row<C>, K>& t) noexcept {
                bool ok = true;
                for (const biquad_row<C>& r : t) {
                    ok = ok && stable_row(r);
                }
                return ok;
            }

            /// Whether a stored number is finite and, for a residue, not negative.
            constexpr bool finite_number(float x) noexcept {
                return x <= std::numeric_limits<float>::max() && x >= -std::numeric_limits<float>::max();
            }

            // The compiled designers (src/biquad_design.cpp): design-time arithmetic in double.

            /// The cookbook's section for `spec`, normalised by a0, in double; the statuses of
            /// the card. Checks the arguments, not the format: `make()` does that.
            expected<biquad_row<double>, status> cookbook_section(const biquad_spec& spec, double low) noexcept;
            /// The sections of an order-N Butterworth low-pass at `cutoff`, in increasing pole
            /// radius, into `out` (N / 2 of them). Precondition: an even N from 2 to 16 and a
            /// cutoff within the double bounds.
            void butterworth_sections(std::size_t order, double cutoff, span<biquad_row<double>> out) noexcept;
            /// The cascade's group delay at f cycles per sample, in frames.
            double cascade_group_delay(span<const biquad_row<double>> sections, double f) noexcept;
            /// The cascade's magnitude at f.
            double cascade_magnitude(span<const biquad_row<double>> sections, double f) noexcept;
            /// A cookbook section's latency, by the card's rule.
            stage_latency cookbook_latency(const biquad_row<double>& section, const biquad_spec& spec) noexcept;
            /// A Butterworth's latency: the group delay at DC and the largest departure from it
            /// over 1,025 points from 0 to the cutoff.
            stage_latency butterworth_latency(span<const biquad_row<double>> sections, double cutoff) noexcept;

            /// The residue bound after the exact tail, as a float: 256 times S's smallest
            /// normal value, and float's smallest positive value where that rounds to zero.
            template <typename S>
            constexpr float residue_bound() noexcept {
                const double bound = 256.0 * static_cast<double>(std::numeric_limits<S>::min());
                const auto   f     = static_cast<float>(bound);
                return f > 0.0f ? f : std::numeric_limits<float>::denorm_min();
            }
        } // namespace detail

        template <typename S, std::size_t Order>
        class butterworth_design;
        template <typename S>
        class k_weighting_design; // catenae/loudness_design.h

        /// @brief `Sections` biquad sections in S's kernel type, with the numbers its stage
        /// reports. A class, not an aggregate: only `make()` and `from_table()` produce one.
        template <typename S, std::size_t Sections = 1>
        class biquad_design {
            static_assert(Sections >= 1 && Sections <= 8, "catenae: a biquad design has 1 to 8 sections");
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                              || std::is_same<S, q31>::value,
                          "catenae: a biquad design is float, double or q31");

          public:
            using coefficient_type                = detail::biquad_coefficient_t<S>;      ///< one coefficient
            using row_type                        = detail::biquad_row<coefficient_type>; ///< b0, b1, b2, a1, a2
            using table_type                      = std::array<row_type, Sections>;       ///< every section, in order
            static constexpr std::size_t sections = Sections;                             ///< how many

            /// One section from the cookbook (the card's statuses). Outside the audio path.
            static expected<biquad_design, status> make(const biquad_spec& spec) noexcept {
                static_assert(Sections == 1, "catenae: make(biquad_spec) designs one section; a cascade is a chain "
                                             "of them, a Butterworth or a table");
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point biquad design needs its section scaling, which arrives at C6 "
                              "(D47)");
                const expected<detail::biquad_row<double>, status> designed =
                    detail::cookbook_section(spec, min_frequency<S>);
                if (!designed) {
                    return unexpected(designed.error());
                }
                const std::array<detail::biquad_row<double>, 1> exact = {{*designed}};
                return from_double(exact, detail::cookbook_latency(*designed, spec));
            }

            /// A design from its rows and numbers, checked: every row's poles inside the unit
            /// circle and its coefficients finite, in S; the latency finite. A table that fails
            /// does not initialise a constant; at run time it reaches the handler.
            static constexpr biquad_design from_table(const table_type& rows, const biquad_numbers& numbers) noexcept {
                detail::check_always(detail::stable_table(rows) && detail::finite_number(numbers.nominal_latency)
                                         && detail::finite_number(numbers.latency_deviation)
                                         && detail::finite_number(numbers.flush_residue),
                                     check::table_rejected);
                return biquad_design(rows, numbers);
            }

            /// The sections, in the order they run.
            constexpr const table_type& table() const noexcept { return m_rows; }
            /// The stored numbers.
            constexpr const biquad_numbers& numbers() const noexcept { return m_numbers; }
            /// `{nominal, deviation}`, in frames.
            constexpr stage_latency latency() const noexcept {
                return {m_numbers.nominal_latency, m_numbers.latency_deviation};
            }
            /// The exact tail, in frames.
            constexpr std::size_t tail_frames() const noexcept { return m_numbers.tail_frames; }
            /// The practical tail, in frames.
            constexpr std::size_t practical_tail_frames() const noexcept { return m_numbers.practical_tail_frames; }
            /// The residue bound after the exact tail.
            constexpr float flush_residue() const noexcept { return m_numbers.flush_residue; }

          private:
            template <typename, std::size_t>
            friend class butterworth_design;
            template <typename>
            friend class k_weighting_design;

            constexpr biquad_design(const table_type& rows, const biquad_numbers& numbers) noexcept
                : m_rows(rows)
                , m_numbers(numbers) {}

            /// The design of the double sections in S: rounded, checked, its tails from the
            /// rounded coefficients and its latency the double design's.
            static expected<biquad_design, status>
            from_double(const std::array<detail::biquad_row<double>, Sections>& exact, stage_latency latency) noexcept {
                table_type                                       rows{};
                std::array<detail::biquad_row<double>, Sections> rounded{};
                for (std::size_t s = 0; s < Sections; ++s) {
                    for (std::size_t j = 0; j < 5; ++j) {
                        rows[s][j]    = static_cast<coefficient_type>(exact[s][j]);
                        rounded[s][j] = static_cast<double>(rows[s][j]);
                    }
                }
                if (!detail::stable_table(rows)) {
                    return unexpected(status::out_of_range);
                }
                const biquad_numbers numbers = {
                    latency.nominal, latency.deviation, detail::exact_tail_frames<S, Sections>(rounded),
                    detail::practical_tail_frames<Sections>(rounded), detail::residue_bound<S>()};
                return biquad_design(rows, numbers);
            }

            table_type     m_rows;
            biquad_numbers m_numbers;
        };

        /// @brief The even-order Butterworth low-pass in S, as `Order / 2` biquad sections.
        template <typename S, std::size_t Order>
        class butterworth_design {
            static_assert(Order >= 2 && Order <= 16 && Order % 2 == 0,
                          "catenae: a Butterworth is of even order 2 to 16");

          public:
            using design_type                  = biquad_design<S, Order / 2>; ///< what make() returns
            static constexpr std::size_t order = Order;                       ///< its order

            /// The design at `cutoff` cycles per sample, its -3 dB point (the card's statuses).
            /// Outside the audio path: its stack and instruction count are on the card.
            static expected<design_type, status> make(double cutoff) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point Butterworth needs its section scaling, which arrives at C6 "
                              "(D47)");
                if (!(cutoff <= std::numeric_limits<double>::max() && cutoff >= -std::numeric_limits<double>::max())) {
                    return unexpected(status::invalid_argument);
                }
                if (cutoff < min_frequency<S> || cutoff > 0.5 - min_frequency<S>) {
                    return unexpected(status::out_of_range);
                }
                std::array<detail::biquad_row<double>, Order / 2> exact{};
                detail::butterworth_sections(Order, cutoff, span<detail::biquad_row<double>>(exact));
                return design_type::from_double(
                    exact, detail::butterworth_latency(span<const detail::biquad_row<double>>(exact), cutoff));
            }
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_BIQUAD_DESIGN_H
