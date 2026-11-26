/// @file biquad_design.cpp
/// @brief The recursive designers: the cookbook's sections, the Butterworth's, and their group
/// delay, in double (PLAN.md 7.1: design-time arithmetic, compiled once).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The cookbook's formulas are Bristow-Johnson's ("Cookbook formulae for audio EQ biquad filter
// coefficients"), with 1 - cos w0 written as 2 sin^2(w0 / 2) and 1 + cos w0 as 2 cos^2(w0 / 2):
// the same numbers in exact arithmetic, without the cancellation that costs a low-frequency
// low-pass its precision. The Butterworth's sections and both group-delay functions are carried
// from DspTap at b26a7db (butterworth.h's design_butterworth_lowpass and
// butterworth_latency_deviation, iir_design.h's sections_group_delay; NOTICE.md).
#include "catenae/biquad_design.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>

#include "catenae/expected.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                constexpr double k_pi = 3.141592653589793238462643383279502884;

                /// The grid both latency scans use: 1,024 intervals.
                constexpr int k_grid = 1024;

                bool finite(double x) noexcept {
                    return x <= std::numeric_limits<double>::max() && x >= -std::numeric_limits<double>::max();
                }

                /// Whether the response reads its gain.
                bool has_gain(biquad_response r) noexcept {
                    return r == biquad_response::peaking || r == biquad_response::low_shelf
                           || r == biquad_response::high_shelf;
                }

                /// The row b0, b1, b2, a1, a2 from the unnormalised six.
                biquad_row<double> normalised(double b0, double b1, double b2, double a0, double a1,
                                              double a2) noexcept {
                    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
                }

                /// The frequency a response's nominal latency is read at.
                double reference_frequency(const biquad_spec& spec) noexcept {
                    if (spec.response == biquad_response::highpass) {
                        return 0.5;
                    }
                    if (spec.response == biquad_response::bandpass_skirt
                        || spec.response == biquad_response::bandpass_peak) {
                        return spec.frequency;
                    }
                    return 0.0;
                }

                /// Whether a response's deviation is read over every frequency.
                bool passes_everywhere(biquad_response r) noexcept {
                    return r == biquad_response::allpass || has_gain(r);
                }

            } // namespace

            expected<biquad_row<double>, status> cookbook_section(const biquad_spec& spec, double low) noexcept {
                if (!finite(spec.frequency) || !finite(spec.q) || !(spec.q > 0.0)
                    || (has_gain(spec.response) && !finite(spec.gain_db))) {
                    return unexpected(status::invalid_argument);
                }
                if (spec.frequency < low || spec.frequency > 0.5 - low) {
                    return unexpected(status::out_of_range);
                }
                const double w0    = 2.0 * k_pi * spec.frequency;
                const double cs    = std::cos(w0);
                const double alpha = std::sin(w0) / (2.0 * spec.q);
                const double sh    = std::sin(k_pi * spec.frequency);
                const double ch    = std::cos(k_pi * spec.frequency);
                const double omc   = 2.0 * sh * sh; // 1 - cos w0
                const double opc   = 2.0 * ch * ch; // 1 + cos w0
                switch (spec.response) {
                case biquad_response::lowpass:
                    return normalised(omc / 2.0, omc, omc / 2.0, 1.0 + alpha, -2.0 * cs, 1.0 - alpha);
                case biquad_response::highpass:
                    return normalised(opc / 2.0, -opc, opc / 2.0, 1.0 + alpha, -2.0 * cs, 1.0 - alpha);
                case biquad_response::bandpass_skirt:
                    return normalised(spec.q * alpha, 0.0, -spec.q * alpha, 1.0 + alpha, -2.0 * cs, 1.0 - alpha);
                case biquad_response::bandpass_peak:
                    return normalised(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cs, 1.0 - alpha);
                case biquad_response::notch:
                    return normalised(1.0, -2.0 * cs, 1.0, 1.0 + alpha, -2.0 * cs, 1.0 - alpha);
                case biquad_response::allpass:
                    return normalised(1.0 - alpha, -2.0 * cs, 1.0 + alpha, 1.0 + alpha, -2.0 * cs, 1.0 - alpha);
                case biquad_response::peaking: {
                    const double a = std::pow(10.0, spec.gain_db / 40.0);
                    return normalised(1.0 + alpha * a, -2.0 * cs, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cs,
                                      1.0 - alpha / a);
                }
                case biquad_response::low_shelf: {
                    const double a  = std::pow(10.0, spec.gain_db / 40.0);
                    const double sa = 2.0 * std::sqrt(a) * alpha;
                    return normalised(a * ((a + 1.0) - (a - 1.0) * cs + sa), 2.0 * a * ((a - 1.0) - (a + 1.0) * cs),
                                      a * ((a + 1.0) - (a - 1.0) * cs - sa), (a + 1.0) + (a - 1.0) * cs + sa,
                                      -2.0 * ((a - 1.0) + (a + 1.0) * cs), (a + 1.0) + (a - 1.0) * cs - sa);
                }
                case biquad_response::high_shelf: {
                    const double a  = std::pow(10.0, spec.gain_db / 40.0);
                    const double sa = 2.0 * std::sqrt(a) * alpha;
                    return normalised(a * ((a + 1.0) + (a - 1.0) * cs + sa), -2.0 * a * ((a - 1.0) + (a + 1.0) * cs),
                                      a * ((a + 1.0) + (a - 1.0) * cs - sa), (a + 1.0) - (a - 1.0) * cs + sa,
                                      2.0 * ((a - 1.0) - (a + 1.0) * cs), (a + 1.0) - (a - 1.0) * cs - sa);
                }
                }
                return unexpected(status::invalid_argument); // a value that names no response
            }

            void butterworth_sections(std::size_t order, double cutoff, span<biquad_row<double>> out) noexcept {
                const double      k     = std::tan(k_pi * cutoff);
                const double      k2    = k * k;
                const std::size_t count = order / 2;
                // The section with the largest alpha (lowest Q, smallest radius) runs first.
                for (std::size_t i = 0; i < count; ++i) {
                    const std::size_t idx = count - 1 - i;
                    const double      alpha =
                        2.0 * std::sin(static_cast<double>(2 * idx + 1) * k_pi / (2.0 * static_cast<double>(order)));
                    const double a0 = 1.0 + alpha * k + k2;
                    out[i] = {k2 / a0, 2.0 * k2 / a0, k2 / a0, 2.0 * (k2 - 1.0) / a0, (1.0 - alpha * k + k2) / a0};
                }
            }

            double cascade_group_delay(span<const biquad_row<double>> sections, double f) noexcept {
                const std::complex<double> zi  = std::polar(1.0, -2.0 * k_pi * f);
                const std::complex<double> zi2 = zi * zi;
                double                     tau = 0.0;
                for (const biquad_row<double>& c : sections) {
                    const std::complex<double> b = c[0] + c[1] * zi + c[2] * zi2;
                    const std::complex<double> a = 1.0 + c[3] * zi + c[4] * zi2;
                    tau +=
                        std::real((c[1] * zi + 2.0 * c[2] * zi2) / b) - std::real((c[3] * zi + 2.0 * c[4] * zi2) / a);
                }
                return tau;
            }

            double cascade_magnitude(span<const biquad_row<double>> sections, double f) noexcept {
                const std::complex<double> zi  = std::polar(1.0, -2.0 * k_pi * f);
                const std::complex<double> zi2 = zi * zi;
                std::complex<double>       r   = 1.0;
                for (const biquad_row<double>& c : sections) {
                    r *= (c[0] + c[1] * zi + c[2] * zi2) / (1.0 + c[3] * zi + c[4] * zi2);
                }
                return std::abs(r);
            }

            stage_latency cookbook_latency(const biquad_row<double>& section, const biquad_spec& spec) noexcept {
                const span<const biquad_row<double>> s(&section, 1);
                const double                         nominal = cascade_group_delay(s, reference_frequency(spec));
                const bool                           all     = passes_everywhere(spec.response);
                double                               peak    = 0.0;
                if (!all) {
                    for (int i = 0; i <= k_grid; ++i) {
                        const double m = cascade_magnitude(s, 0.5 * static_cast<double>(i) / k_grid);
                        peak           = m > peak ? m : peak;
                    }
                }
                const double floor = peak / std::sqrt(2.0);
                double       dev   = 0.0;
                for (int i = 0; i <= k_grid; ++i) {
                    const double f = 0.5 * static_cast<double>(i) / k_grid;
                    if (all || cascade_magnitude(s, f) >= floor) {
                        const double d = std::fabs(cascade_group_delay(s, f) - nominal);
                        dev            = d > dev ? d : dev;
                    }
                }
                return {static_cast<float>(nominal), static_cast<float>(dev)};
            }

            stage_latency butterworth_latency(span<const biquad_row<double>> sections, double cutoff) noexcept {
                const double nominal = cascade_group_delay(sections, 0.0);
                double       dev     = 0.0;
                for (int i = 0; i <= k_grid; ++i) {
                    const double f = cutoff * static_cast<double>(i) / k_grid;
                    const double d = std::fabs(cascade_group_delay(sections, f) - nominal);
                    dev            = d > dev ? d : dev;
                }
                return {static_cast<float>(nominal), static_cast<float>(dev)};
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
