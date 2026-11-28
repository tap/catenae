/// @file loudness_design.cpp
/// @brief The K-weighting's designer: BS.1770-4's 48 kHz sections, the bilinear transform
/// inverted, and the analog sections made again at the caller's rate, in double
/// (include/catenae/loudness_design.h).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The tables are ITU-R BS.1770-4 (10/2015), Annex 1, Tables 1 and 2. The inversion is the
// bilinear transform's, s = (1 / K) (1 - z^-1) / (1 + z^-1), solved for K, Q and the three
// gains from a normalised section (Oppenheim and Schafer, "Discrete-Time Signal Processing",
// 3rd ed., 7.2): no coefficient of any other implementation is read.
#include "catenae/loudness_design.h"

#include <array>
#include <cmath>
#include <limits>

#include "catenae/biquad_design.h"
#include "catenae/expected.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                constexpr double k_pi = 3.141592653589793238462643383279502884;

                /// The rate BS.1770-4's tables are at.
                constexpr double k_table_rate = 48000.0;

                /// BS.1770-4's reference frequency (Annex 1, its Notes 1 and 2).
                constexpr double k_reference_hz = 997.0;

                /// The grid the latency scan uses: 1,024 intervals, the cookbook's.
                constexpr int k_grid = 1024;

                /// An analog second-order section, (Vh s^2 + Vb s / Q + Vl) / (s^2 + s / Q + 1)
                /// with s normalised to its f0.
                struct analog_section {
                    double f0; ///< Hz
                    double q;  ///< Q
                    double vh; ///< the gain at infinity
                    double vb; ///< the band term's gain
                    double vl; ///< the gain at DC
                };

                /// The analog section whose transform at `rate`, pre-warped at its f0, is `r`.
                analog_section inverted(const biquad_row<double>& r, double rate) noexcept {
                    const double b0 = r[0];
                    const double b1 = r[1];
                    const double b2 = r[2];
                    const double a1 = r[3];
                    const double a2 = r[4];
                    const double k  = std::sqrt((1.0 + a1 + a2) / (1.0 - a1 + a2));
                    const double kq = 2.0 * (1.0 - a2) / (1.0 - a1 + a2); // K / Q
                    return {rate * std::atan(k) / k_pi, k / kq, (b0 - b1 + b2) / (1.0 - a1 + a2),
                            (b0 - b2) / (1.0 - a2), (b0 + b1 + b2) / (1.0 + a1 + a2)};
                }

                /// The section's transform at `rate`, pre-warped at its f0, normalised.
                biquad_row<double> transformed(const analog_section& a, double rate) noexcept {
                    const double k  = std::tan(k_pi * a.f0 / rate);
                    const double kq = k / a.q;
                    const double k2 = k * k;
                    const double a0 = 1.0 + kq + k2;
                    return {(a.vh + a.vb * kq + a.vl * k2) / a0, 2.0 * (a.vl * k2 - a.vh) / a0,
                            (a.vh - a.vb * kq + a.vl * k2) / a0, 2.0 * (k2 - 1.0) / a0, (1.0 - kq + k2) / a0};
                }

            } // namespace

            std::array<biquad_row<double>, 2> k_weighting_tables() noexcept {
                return {{
                    {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585},
                    {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621},
                }};
            }

            expected<std::array<biquad_row<double>, 2>, status> k_weighting_sections(double sample_rate_hz,
                                                                                     double low) noexcept {
                if (!(sample_rate_hz > 0.0 && sample_rate_hz <= std::numeric_limits<double>::max())) {
                    return unexpected(status::invalid_argument);
                }
                const std::array<biquad_row<double>, 2> tables   = k_weighting_tables();
                const analog_section                    shelf    = inverted(tables[0], k_table_rate);
                const analog_section                    highpass = inverted(tables[1], k_table_rate);
                if (highpass.f0 / sample_rate_hz < low || shelf.f0 / sample_rate_hz > 0.5 - low) {
                    return unexpected(status::out_of_range);
                }
                if (!(sample_rate_hz < k_table_rate) && !(sample_rate_hz > k_table_rate)) {
                    return tables; // the standard's own numbers, not their round trip
                }
                return std::array<biquad_row<double>, 2>{
                    {transformed(shelf, sample_rate_hz), transformed(highpass, sample_rate_hz)}};
            }

            stage_latency k_weighting_latency(span<const biquad_row<double>> sections, double sample_rate_hz) noexcept {
                const double nominal = cascade_group_delay(sections, k_reference_hz / sample_rate_hz);
                double       peak    = 0.0;
                for (int i = 0; i <= k_grid; ++i) {
                    const double m = cascade_magnitude(sections, 0.5 * static_cast<double>(i) / k_grid);
                    peak           = m > peak ? m : peak;
                }
                const double floor = peak / std::sqrt(2.0);
                double       dev   = 0.0;
                for (int i = 0; i <= k_grid; ++i) {
                    const double f = 0.5 * static_cast<double>(i) / k_grid;
                    if (cascade_magnitude(sections, f) >= floor) {
                        const double d = std::fabs(cascade_group_delay(sections, f) - nominal);
                        dev            = d > dev ? d : dev;
                    }
                }
                return {static_cast<float>(nominal), static_cast<float>(dev)};
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
