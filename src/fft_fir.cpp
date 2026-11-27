/// @file fft_fir.cpp
/// @brief The FFT FIR's product of a frame's bins by its filter's (detail/fft_fir_kernel.h).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Compiled at contraction off, as the whole core is: each of the four products is rounded, then
// each sum, so the bits are the same on every target.
#include <cstddef>

#include "catenae/detail/fft_fir_kernel.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {
            namespace {

                /// (a + ib)(c + id), bin by bin, in place.
                template <typename T>
                void fft_fir_times(complex<T>* frame, const complex<T>* h, std::size_t bins) noexcept {
                    for (std::size_t k = 0; k < bins; ++k) {
                        const T a = frame[k].re;
                        const T b = frame[k].im;
                        const T c = h[k].re;
                        const T d = h[k].im;
                        frame[k]  = complex<T>{a * c - b * d, a * d + b * c};
                    }
                }

            } // namespace

            void fft_fir_product(complex<float>* frame, const complex<float>* h, std::size_t bins) noexcept {
                fft_fir_times(frame, h, bins);
            }

            void fft_fir_product(complex<double>* frame, const complex<double>* h, std::size_t bins) noexcept {
                fft_fir_times(frame, h, bins);
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
