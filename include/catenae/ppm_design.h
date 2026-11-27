/// @file ppm_design.h
/// @brief A peak programme meter's design: its attack and release coefficients, made from an
/// integration time and a return rate as IEC 60268-10 defines them, or checked from a table.
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | A design    | `ppm_design<S>`: the attack and release coefficients `ppm_meter` runs, in S; |
/// |             | made only by `make()` or `from_table()`                                     |
/// | Detector    | Zolzer's peak meter, "Digital Audio Signal Processing", 2nd ed., Wiley 2008, |
/// |             | ch. 7: y += attack (\|x\| - y) where \|x\| > y, else y x release            |
/// | Integration | as IEC 60268-10 defines it (quoted in EBU Tech 3205-E, 1979, p. 5): the      |
/// |             | length of a 5 kHz sine burst at a steady level that reads 2 dB below that   |
/// |             | level's steady reading. `make()` finds the attack that does so by simulating |
/// |             | the burst at the spec's rate, from phase 0, its reading the largest y during |
/// |             | the burst, the steady reading the largest over 25 times the burst            |
/// | Return      | `return_db` in `return_ms`, at a constant rate in dB: release = 10^(-return_db |
/// |             | / (20 return_ms x rate / 1000))                                             |
/// | EBU         | `ebu_ppm(rate)`: Type IIb, EBU Tech 3205-E 3.3 and 3.10: integration 10 ms, |
/// |             | return 24 dB in 2.8 s                                                       |
/// | Rejects     | `status::invalid_argument`: a NaN or infinite number, a rate, time or return |
/// |             | not above 0; `status::out_of_range`: a rate not above 10 kHz (the burst's   |
/// |             | tone would not be below Nyquist), an integration above 1 s, a burst no      |
/// |             | attack reads 2 dB low (one too short, or a tone too near Nyquist, to hold a |
/// |             | sample within 2 dB of its peak), or a coefficient that rounds to 0 or 1 in S |
/// | Table       | `from_table(numbers)` checks attack in (0, 1] and release in (0, 1): a table |
/// |             | that fails does not initialise a constant, and at run time reaches          |
/// |             | `check::table_rejected`                                                     |
/// | Cost        | `make()` runs outside the audio path, in double: about ten simulations of   |
/// |             | 25 bursts; a target that cannot afford it takes numbers made on a host       |
/// | Formats     | float and double; q31 declares its contract, its numbers held in float until |
/// |             | its profile arrives at C6 (D47)                                              |
/// | Rationale   | D12, D24, D37, D47                                                          |
///
/// @req{CAT-PPM-DESIGN-001} `make()`'s attack makes the integration's 5 kHz burst read 2 dB below
/// the steady reading, within 0.01 dB at 44.1, 48 and 96 kHz, and its release falls `return_db`
/// in `return_ms`, within rounding to S; `ebu_ppm` is 10 ms and 24 dB in 2.8 s.
/// @req{CAT-PPM-DESIGN-002} `make()` rejects what its card names with the status it names.
/// @req{CAT-PPM-DESIGN-003} `from_table` accepts a design's own numbers bit for bit and refuses
/// one out of range: in a constant it does not compile, at run time `check::table_rejected`.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PPM_DESIGN_H
#define CATENAE_PPM_DESIGN_H

#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// What a peak programme meter is designed from.
        struct ppm_spec {
            double sample_rate_hz; ///< the rate the meter runs at
            double integration_ms; ///< the 5 kHz burst that reads 2 dB below its steady reading
            double return_db;      ///< the fall, dB ...
            double return_ms;      ///< ... in this time, at a constant rate in dB
        };

        /// The EBU's Type IIb meter (EBU Tech 3205-E, 3.3 and 3.10) at `sample_rate_hz`.
        constexpr ppm_spec ebu_ppm(double sample_rate_hz) noexcept {
            return {sample_rate_hz, 10.0, 24.0, 2800.0};
        }

        /// A peak programme meter's numbers.
        template <typename T>
        struct ppm_numbers {
            T attack;  ///< the share of (|x| - y) added when |x| is above y
            T release; ///< y's factor per frame otherwise
        };

        namespace detail {
            /// What a meter's design holds its numbers in: the sample's own type when it floats,
            /// float for a fixed-point format until its profile arrives (C6).
            template <typename S>
            using ppm_value_t = std::conditional_t<format_traits<S>::is_fixed_point, float, storage_t<S>>;

            /// The numbers in double, or why there are none (src/ppm_design.cpp).
            expected<ppm_numbers<double>, status> ppm_numbers_of(const ppm_spec& spec) noexcept;

            /// Whether the numbers are a peak meter's: attack in (0, 1], release in (0, 1).
            template <typename T>
            constexpr bool valid_ppm(const ppm_numbers<T>& n) noexcept {
                return n.attack > T{0} && n.attack <= T{1} && n.release > T{0} && n.release < T{1};
            }
        } // namespace detail

        /// @brief A peak programme meter's design in S.
        template <typename S>
        class ppm_design {
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                              || std::is_same<S, q31>::value,
                          "catenae: a peak programme meter's design is float, double or q31");

          public:
            using value_type   = detail::ppm_value_t<S>;  ///< what its numbers are held in
            using numbers_type = ppm_numbers<value_type>; ///< the numbers

            /// The design for `spec` (the card's statuses). Outside the audio path.
            static expected<ppm_design, status> make(const ppm_spec& spec) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point peak programme meter is new design and arrives with its "
                              "profile at C6 (D47)");
                const expected<ppm_numbers<double>, status> exact = detail::ppm_numbers_of(spec);
                if (!exact) {
                    return unexpected(exact.error());
                }
                const numbers_type n{static_cast<value_type>(exact->attack), static_cast<value_type>(exact->release)};
                if (!detail::valid_ppm(n)) {
                    return unexpected(status::out_of_range);
                }
                return ppm_design(n);
            }

            /// A design from its numbers, checked. A table that fails does not initialise a
            /// constant; at run time it reaches the handler.
            static constexpr ppm_design from_table(const numbers_type& numbers) noexcept {
                detail::check_always(detail::valid_ppm(numbers), check::table_rejected);
                return ppm_design(numbers);
            }

            /// The numbers its meter runs.
            constexpr const numbers_type& numbers() const noexcept { return m_numbers; }

          private:
            constexpr explicit ppm_design(const numbers_type& numbers) noexcept
                : m_numbers(numbers) {}

            numbers_type m_numbers;
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PPM_DESIGN_H
