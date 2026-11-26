/// @file check.h
/// @brief The checks: their codes, the one handler, and the two levels (D27, PLAN.md 3.4).
///
/// | Contract  |                                                                              |
/// |-----------|------------------------------------------------------------------------------|
/// | Levels    | `boundary` (every build), `audit` (a build option, CATENAE_CHECKS)          |
/// | Handler   | `check_failed(check)`, `[[noreturn]]`, defined by the integrator, not here  |
/// | Codes     | `enum class check`: no strings in ROM                                         |
/// | Default   | a trapping handler ships apart, as `catenae::trap_handler` (src/handler/)   |
/// | Rationale | D27, D21, D23                                                                |
///
/// @req{CAT-CHECK-001} A failed check calls `check_failed` with its code and nothing returns
/// from it; the library defines no handler, so an image without one does not link.
/// @req{CAT-CHECK-002} A boundary check runs in every build; an audit check runs only when
/// the build's level is audit, and costs nothing otherwise.
/// @req{CAT-CHECK-003} A check that fails while a constant is evaluated does not compile.
/// @req{CAT-CHECK-004} The handler's name carries the level (catenae/detail/abi.h): a file
/// built at one level that holds a run-time check does not link against a handler built at
/// the other.
/// @req{CAT-CHECK-005} A code is one byte and keeps its number: boundary codes from 1, the
/// vocabulary's from 32, audit codes from 64. A code is added with a new number.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_CHECK_H
#define CATENAE_CHECK_H

#include <cstdint>

#include "catenae/compiler.h"
#include "catenae/detail/abi.h"
#include "catenae/fwd.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// What failed. Boundary codes are below 64, audit codes from 64.
        enum class check : std::uint8_t {
            // Boundary: where a caller's buffers enter, in every build (D27).
            input_not_whole_frames = 1, ///< an input that is not a whole number of frames
            output_too_small       = 2, ///< an output that cannot hold outputs_for(n) frames
            flush_output_too_small = 3, ///< a flush output that cannot hold one burst
            push_too_large         = 4, ///< a push above the junction's largest push
            pull_too_large         = 5, ///< a pull above the junction's target
            request_too_large      = 6, ///< a fed_by request above its MaxRequest
            // Preconditions of the vocabulary, in every build (PLAN.md section 3.3).
            expected_empty     = 32, ///< value(), operator* or operator-> of an expected with none
            expected_not_error = 33, ///< error() of an expected that holds a value
            table_rejected     = 34, ///< a table met at run time that its design's check refuses
            // Audit: the library's own promises, and the host's (D27).
            index_out_of_range = 64, ///< an index or a subrange outside its span
            invariant          = 65, ///< an invariant of the library's own state
            outputs_exceeded   = 66, ///< a stage wrote more than its max_outputs_for
            state_not_reset    = 67, ///< a chain's state used before its reset
            fifo_short         = 68, ///< steady<>'s FIFO ran short
            fifo_over          = 69, ///< steady<>'s FIFO ran over
            pull_above_staging = 70, ///< a pull larger than fed_by's staging
            flush_to_zero      = 71, ///< the host's flush-to-zero setting (the check arrives at C3)
            buffers_overlap    = 72, ///< a stage's in and out that overlap without being one buffer
        };

        /// Where every failed check goes. Defined by the integrator: it is where their system
        /// enters its safe state, and it must not return.
        [[noreturn]] void check_failed(check code) noexcept;

        namespace detail {

            /// Whether this build runs the audit checks.
            inline constexpr bool k_audit_level = CATENAE_CHECKS == CATENAE_CHECKS_AUDIT;

            /// A check in every build.
            CATENAE_ALWAYS_INLINE constexpr void check_always(bool ok, check code) noexcept {
                if (!ok) {
                    check_failed(code);
                }
            }

            /// A check at the audit level only.
            CATENAE_ALWAYS_INLINE constexpr void check_audit(bool ok, check code) noexcept {
                if constexpr (k_audit_level) {
                    if (!ok) {
                        check_failed(code);
                    }
                }
                else {
                    static_cast<void>(ok);
                    static_cast<void>(code);
                }
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_CHECK_H
