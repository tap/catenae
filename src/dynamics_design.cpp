/// @file dynamics_design.cpp
/// @brief The dynamics designers: a spec in dB and frames to the numbers a stage runs, in
/// double (PLAN.md 7.1: design-time arithmetic, compiled once).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gain computers' parameters are Giannoulis, Massberg and Reiss's ("Digital Dynamic Range
// Compressor Design: A Tutorial and Analysis", JAES 60(6), 2012): a threshold, a ratio and a
// knee of width W, in the log domain; the expander's are its downward mirror, with a range
// (Reiss and McPherson, "Audio Effects", 2014, ch. 6). A time constant tau gives the one-pole
// coefficient exp(-1 / tau), the paper's alpha with tau in frames.
#include "catenae/dynamics_design.h"

#include <cmath>
#include <limits>

#include "catenae/expected.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                /// dB to nepers: ln 10 / 20.
                constexpr double k_dynamics_db_to_neper = 0.11512925464970228420089957273422;

                /// The largest threshold, gain, knee or range a design is made for, in dB.
                constexpr double k_dynamics_max_db = 200.0;

                bool dynamics_finite_double(double x) noexcept {
                    return x <= std::numeric_limits<double>::max() && x >= -std::numeric_limits<double>::max();
                }

                /// A level in dB that is finite and, within the card's bound, in range: the status
                /// if not.
                bool dynamics_level_ok(double db, status& why) noexcept {
                    if (!dynamics_finite_double(db)) {
                        why = status::invalid_argument;
                        return false;
                    }
                    if (db > k_dynamics_max_db || db < -k_dynamics_max_db) {
                        why = status::out_of_range;
                        return false;
                    }
                    return true;
                }

                /// A time constant that is finite and not negative.
                bool dynamics_time_ok(double frames) noexcept {
                    return dynamics_finite_double(frames) && frames >= 0.0;
                }

                /// A ratio from 1 to infinity (false for a NaN).
                bool dynamics_ratio_ok(double r) noexcept {
                    return r >= 1.0;
                }

                /// A knee's width: finite, not negative, within the bound; the status if not.
                bool dynamics_knee_ok(double db, status& why) noexcept {
                    if (!dynamics_finite_double(db) || db < 0.0) {
                        why = status::invalid_argument;
                        return false;
                    }
                    if (db > k_dynamics_max_db) {
                        why = status::out_of_range;
                        return false;
                    }
                    return true;
                }

                /// A one-pole of time constant `frames`: exp(-1 / tau) and its complement
                /// -expm1(-1 / tau), each to double's precision; tau = 0 is instant.
                void dynamics_one_pole(double frames, double& alpha, double& complement) noexcept {
                    if (frames > 0.0) {
                        alpha      = std::exp(-1.0 / frames);
                        complement = -std::expm1(-1.0 / frames);
                    }
                    else {
                        alpha      = 0.0;
                        complement = 1.0;
                    }
                }

            } // namespace

            expected<compressor_numbers<double>, status> compressor_numbers_of(const compressor_spec& spec) noexcept {
                status why = status::invalid_argument;
                if (!dynamics_ratio_ok(spec.ratio) || !dynamics_time_ok(spec.attack_frames)
                    || !dynamics_time_ok(spec.release_frames)) {
                    return unexpected(status::invalid_argument);
                }
                if (!dynamics_level_ok(spec.threshold_db, why) || !dynamics_level_ok(spec.makeup_db, why)
                    || !dynamics_knee_ok(spec.knee_db, why)) {
                    return unexpected(why);
                }
                compressor_numbers<double> n{};
                const double               knee = spec.knee_db * k_dynamics_db_to_neper;
                n.threshold                     = spec.threshold_db * k_dynamics_db_to_neper;
                n.half_knee                     = 0.5 * knee;
                n.above                         = 1.0 - 1.0 / spec.ratio; // 1 at a ratio of infinity
                n.knee_scale                    = knee > 0.0 ? n.above / (2.0 * knee) : 0.0;
                n.makeup                        = spec.makeup_db * k_dynamics_db_to_neper;
                dynamics_one_pole(spec.attack_frames, n.attack, n.attack_complement);
                dynamics_one_pole(spec.release_frames, n.release, n.release_complement);
                return n;
            }

            expected<limiter_numbers<double>, status> limiter_numbers_of(const limiter_spec& spec) noexcept {
                status why = status::invalid_argument;
                if (!dynamics_time_ok(spec.release_frames)) {
                    return unexpected(status::invalid_argument);
                }
                if (!dynamics_level_ok(spec.threshold_db, why)) {
                    return unexpected(why);
                }
                limiter_numbers<double> n{};
                n.threshold = spec.threshold_db * k_dynamics_db_to_neper;
                n.ceiling   = std::exp(n.threshold);
                dynamics_one_pole(spec.release_frames, n.release, n.release_complement);
                return n;
            }

            expected<expander_numbers<double>, status> expander_numbers_of(const expander_spec& spec) noexcept {
                status why = status::invalid_argument;
                if (!dynamics_ratio_ok(spec.ratio) || !dynamics_time_ok(spec.attack_frames)
                    || !dynamics_time_ok(spec.release_frames)) {
                    return unexpected(status::invalid_argument);
                }
                if (!dynamics_level_ok(spec.threshold_db, why) || !dynamics_knee_ok(spec.knee_db, why)
                    || !dynamics_level_ok(spec.range_db, why)) {
                    return unexpected(why);
                }
                if (!(spec.range_db > 0.0)) {
                    return unexpected(status::invalid_argument);
                }
                expander_numbers<double> n{};
                const double             knee = spec.knee_db * k_dynamics_db_to_neper;
                n.threshold                   = spec.threshold_db * k_dynamics_db_to_neper;
                n.half_knee                   = 0.5 * knee;
                n.below                       = spec.ratio - 1.0; // infinity for a gate
                n.knee_scale                  = knee > 0.0 ? n.below / (2.0 * knee) : 0.0;
                n.range                       = spec.range_db * k_dynamics_db_to_neper;
                dynamics_one_pole(spec.attack_frames, n.attack, n.attack_complement);
                dynamics_one_pole(spec.release_frames, n.release, n.release_complement);
                n.hold = spec.hold_frames;
                return n;
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
