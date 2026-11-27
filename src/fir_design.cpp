/// @file fir_design.cpp
/// @brief The FIR designer: a Kaiser-windowed ideal response, in double (PLAN.md 7.1:
/// design-time arithmetic, compiled once).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The window and its parameter are Kaiser's ("Nonrecursive digital filter design using the
// I0-sinh window function", 1974), the ideal responses the window method's (Oppenheim and
// Schafer, "Discrete-Time Signal Processing", 3rd ed., 7.5-7.6). bessel_i0, kaiser_beta and
// sinc are carried from DspTap's kaiser.h at b26a7db (NOTICE.md), with design_prototype's
// window argument; the four responses and their normalisation are new here.
#include "catenae/fir_design.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include "catenae/expected.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                constexpr double k_fir_pi = 3.141592653589793238462643383279502884;

                /// The highest attenuation a design is made for, in dB.
                constexpr double k_fir_max_attenuation = 200.0;

                bool fir_finite_double(double x) noexcept {
                    return x <= std::numeric_limits<double>::max() && x >= -std::numeric_limits<double>::max();
                }

                /// The modified Bessel function of the first kind, order zero, by its power series,
                /// summed until a term no longer counts in double (DspTap's bessel_i0).
                double fir_bessel_i0(double x) noexcept {
                    const double half_x = 0.5 * x;
                    double       term   = 1.0;
                    double       sum    = 1.0;
                    for (int k = 1; k < 1000; ++k) {
                        const double r = half_x / static_cast<double>(k);
                        term *= r * r;
                        sum += term;
                        if (term < 1e-21 * sum) {
                            break;
                        }
                    }
                    return sum;
                }

                /// The window's beta for a stopband of `atten_db` (Kaiser's empirical fit; DspTap's
                /// kaiser_beta).
                double fir_kaiser_beta(double atten_db) noexcept {
                    if (atten_db > 50.0) {
                        return 0.1102 * (atten_db - 8.7);
                    }
                    if (atten_db > 21.0) {
                        return 0.5842 * std::pow(atten_db - 21.0, 0.4) + 0.07886 * (atten_db - 21.0);
                    }
                    return 0.0;
                }

                /// The ideal low-pass at `f` cycles per sample, at `t` samples from its centre:
                /// 2 f sinc(2 f t), sinc(x) = sin(pi x) / (pi x) and 1 at 0 (DspTap's sinc).
                double fir_ideal_lowpass(double f, double t) noexcept {
                    const double x = 2.0 * f * t;
                    if (std::fabs(x) < 1e-12) {
                        return 2.0 * f;
                    }
                    const double px = k_fir_pi * x;
                    return 2.0 * f * (std::sin(px) / px);
                }

                /// Whether the response needs its high edge's low-pass as well as its low's.
                bool fir_two_edges(fir_response r) noexcept {
                    return r == fir_response::bandpass || r == fir_response::bandstop;
                }

                /// Whether the response is delta less its low-passes' combination.
                bool fir_complement(fir_response r) noexcept {
                    return r == fir_response::highpass || r == fir_response::bandstop;
                }

            } // namespace

            expected<fir_request, status> fir_checked(const fir_request& request) noexcept {
                if (!fir_finite_double(request.low) || !fir_finite_double(request.high)
                    || !fir_finite_double(request.attenuation_db)) {
                    return unexpected(status::invalid_argument);
                }
                if (fir_two_edges(request.response) && !(request.low < request.high)) {
                    return unexpected(status::invalid_argument);
                }
                if (!(request.low > 0.0) || !(request.high < 0.5)) {
                    return unexpected(status::out_of_range);
                }
                if (request.attenuation_db < 0.0 || request.attenuation_db > k_fir_max_attenuation) {
                    return unexpected(status::out_of_range);
                }
                return request;
            }

            void fir_half_design(const fir_request& request, std::size_t taps, span<double> half) noexcept {
                const std::size_t count   = half.size();
                const bool        odd     = taps % 2 == 1;
                const double      centre  = 0.5 * static_cast<double>(taps - 1);
                const double      beta    = fir_kaiser_beta(request.attenuation_db);
                const double      i0_beta = fir_bessel_i0(beta);
                const bool        two     = fir_two_edges(request.response);
                // The window into `half`, and each low-pass's gain at DC: a pair of taps counts
                // twice, an odd count's centre once.
                double dc_low  = 0.0;
                double dc_high = 0.0;
                for (std::size_t i = 0; i < count; ++i) {
                    const double t = static_cast<double>(i) - centre;
                    double       w = 1.0;
                    if (taps > 1) {
                        const double u   = t / centre; // the window's argument, -1 to 0
                        const double arg = 1.0 - u * u;
                        w                = fir_bessel_i0(beta * std::sqrt(arg > 0.0 ? arg : 0.0)) / i0_beta;
                    }
                    half[i]             = w;
                    const double weight = odd && i + 1 == count ? 1.0 : 2.0;
                    dc_low += weight * w * fir_ideal_lowpass(request.low, t);
                    if (two) {
                        dc_high += weight * w * fir_ideal_lowpass(request.high, t);
                    }
                }
                // Each low-pass at unity gain at DC, combined as the response asks.
                for (std::size_t i = 0; i < count; ++i) {
                    const double t     = static_cast<double>(i) - centre;
                    const double w     = half[i];
                    const double delta = odd && i + 1 == count ? 1.0 : 0.0;
                    double       h     = w * fir_ideal_lowpass(request.low, t) / dc_low;
                    if (two) {
                        h = w * fir_ideal_lowpass(request.high, t) / dc_high - h;
                    }
                    half[i] = fir_complement(request.response) ? delta - h : h;
                }
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
