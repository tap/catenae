/// @file bessel_i0.h
/// @brief The modified Bessel function of the first kind, order zero: the Kaiser window's, for
/// the FIR designer and the STFT's windows alike (a private header of the compiled core).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Carried from DspTap's kaiser.h (bessel_i0) at b26a7db, NOTICE.md, as src/fir_design.cpp first
// held it: the power series sum ((x / 2)^k / k!)^2, summed in double until a term no longer
// counts. IEEE arithmetic only, so its bits are the same on every target.
#ifndef CATENAE_SRC_BESSEL_I0_H
#define CATENAE_SRC_BESSEL_I0_H

#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// I0(x) by its power series.
            inline double bessel_i0(double x) noexcept {
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

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SRC_BESSEL_I0_H
