/// @file ppm_design.cpp
/// @brief The peak programme meter's designer: an integration time and a return rate to the
/// attack and release its meter runs, in double (PLAN.md 7.1: design-time arithmetic).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The detector is Zolzer's peak meter ("Digital Audio Signal Processing", 2nd ed., 2008, ch. 7):
// y += attack (|x| - y) where |x| > y, else y x release. The release follows from a fall of
// constant rate in dB. The attack is found by simulation: IEC 60268-10 defines the integration
// time as the length of a 5 kHz sine burst at a steady level that reads 2 dB below that level's
// steady reading (EBU Tech 3205-E, 1979, p. 5), and no closed form gives a peak detector's reading
// of a rectified sine. The reading of a burst is the largest y during it; the steady reading is
// the largest over 25 times the burst, which settles to within 1e-4 dB for the EBU's 10 ms.
#include "catenae/ppm_design.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include "catenae/expected.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                /// The burst's tone, Hz.
                constexpr double k_ppm_tone_hz = 5000.0;
                /// The reading of the burst below the steady reading: 2 dB, as a ratio.
                constexpr double k_ppm_burst_ratio = 0.79432823472428150206591828283639; // 10^(-2/20)
                /// The steady reading's length, in bursts.
                constexpr std::size_t k_ppm_steady_bursts = 25;
                /// The longest integration a design is made for, ms.
                constexpr double k_ppm_max_integration_ms = 1000.0;
                /// The attack's search: at most this many steps, each a simulation.
                constexpr int k_ppm_max_steps = 60;
                /// ln 10 / 20.
                constexpr double k_ppm_db_to_neper = 0.11512925464970228420089957273422;
                /// 2 pi.
                constexpr double k_ppm_two_pi = 6.283185307179586476925286766559;

                bool ppm_finite(double x) noexcept {
                    return x <= std::numeric_limits<double>::max() && x >= -std::numeric_limits<double>::max();
                }

                /// The burst's reading over the steady reading, in dB, for one attack. The tone is
                /// made by the recurrence sin((i + 1) w) = 2 cos w sin(i w) - sin((i - 1) w), from
                /// phase 0.
                double ppm_burst_db(double attack, double release, double w, std::size_t burst) noexcept {
                    const double      twice_cos = 2.0 * std::cos(w);
                    double            previous  = -std::sin(w);
                    double            current   = 0.0;
                    double            y         = 0.0;
                    double            burst_max = 0.0;
                    double            steady    = 0.0;
                    const std::size_t total     = burst * k_ppm_steady_bursts;
                    for (std::size_t i = 0; i < total; ++i) {
                        const double x = std::fabs(current);
                        if (x > y) {
                            y += attack * (x - y);
                        }
                        else {
                            y *= release;
                        }
                        if (i < burst && y > burst_max) {
                            burst_max = y;
                        }
                        if (y > steady) {
                            steady = y;
                        }
                        const double next = twice_cos * current - previous;
                        previous          = current;
                        current           = next;
                    }
                    return 20.0 * std::log10(burst_max / steady);
                }

            } // namespace

            expected<ppm_numbers<double>, status> ppm_numbers_of(const ppm_spec& spec) noexcept {
                if (!ppm_finite(spec.sample_rate_hz) || !ppm_finite(spec.integration_ms) || !ppm_finite(spec.return_db)
                    || !ppm_finite(spec.return_ms) || !(spec.sample_rate_hz > 0.0) || !(spec.integration_ms > 0.0)
                    || !(spec.return_db > 0.0) || !(spec.return_ms > 0.0)) {
                    return unexpected(status::invalid_argument);
                }
                if (spec.sample_rate_hz <= 2.0 * k_ppm_tone_hz || spec.integration_ms > k_ppm_max_integration_ms) {
                    return unexpected(status::out_of_range);
                }
                const double burst_frames = std::round(spec.integration_ms * spec.sample_rate_hz / 1000.0);
                if (burst_frames < 1.0) {
                    return unexpected(status::out_of_range);
                }
                const auto   burst = static_cast<std::size_t>(burst_frames);
                const double release =
                    std::exp(-spec.return_db * k_ppm_db_to_neper / (spec.return_ms * spec.sample_rate_hz / 1000.0));
                const double w      = k_ppm_two_pi * k_ppm_tone_hz / spec.sample_rate_hz;
                const double target = 20.0 * std::log10(k_ppm_burst_ratio);

                // The attack whose burst reads 2 dB low. The reading rises with the attack, so a
                // bracket from a constant level's attack (a peak detector charges no faster on a
                // rectified sine) upward, then the Illinois form of false position on the reading.
                double low     = -std::expm1(std::log1p(-k_ppm_burst_ratio) / burst_frames);
                double low_db  = ppm_burst_db(low, release, w, burst) - target;
                double high    = low;
                double high_db = low_db;
                int    steps   = 0;
                while (high_db < 0.0 && steps < k_ppm_max_steps) {
                    low     = high;
                    low_db  = high_db;
                    high    = high * 2.0 < 1.0 ? high * 2.0 : 1.0;
                    high_db = ppm_burst_db(high, release, w, burst) - target;
                    ++steps;
                    if (high >= 1.0) {
                        break;
                    }
                }
                if (high_db < 0.0) {
                    return unexpected(status::out_of_range); // no attack reads 2 dB low: too short
                }
                int side = 0;
                while (steps < k_ppm_max_steps && high - low > 1e-12 * high) {
                    const double mid    = (low * high_db - high * low_db) / (high_db - low_db);
                    const double mid_db = ppm_burst_db(mid, release, w, burst) - target;
                    ++steps;
                    if (mid_db < 0.0) {
                        low    = mid;
                        low_db = mid_db;
                        if (side == -1) {
                            high_db /= 2.0;
                        }
                        side = -1;
                    }
                    else {
                        high    = mid;
                        high_db = mid_db;
                        if (side == 1) {
                            low_db /= 2.0;
                        }
                        side = 1;
                    }
                }
                const double attack = -low_db < high_db ? low : high;
                return ppm_numbers<double>{attack, release};
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
