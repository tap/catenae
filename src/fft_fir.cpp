/// @file fft_fir.cpp
/// @brief The FFT FIR's product of a frame's bins by its filter's (detail/fft_fir_kernel.h).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Compiled at contraction off, as the whole core is: each of the four products is rounded, then
// each sum, so the bits are the same on every target.
#include <cstddef>

#include "catenae/detail/fft_fir_kernel.h"

// Under GCC the loop is kept scalar. At -O2 and above GCC 13 vectorizes it for the Cortex-M55
// into MVE's VCMUL and VCMLA, whose accumulation is fused, though the core is built with
// contraction off: the product then rounds once where every other target rounds twice, and the
// FFT FIR's float output differed on that key alone (Bench run 46021806011). Clang does not
// vectorize it so on the targets CI builds; it is left to its own choices.
#if defined(__GNUC__) && !defined(__clang__)
#define CATENAE_FFT_FIR_SCALAR __attribute__((optimize("no-tree-vectorize")))
#else
#define CATENAE_FFT_FIR_SCALAR
#endif

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {
            namespace {

                /// (a + ib)(c + id), bin by bin, in place.
                template <typename T>
                CATENAE_FFT_FIR_SCALAR void fft_fir_times(complex<T>* frame, const complex<T>* h,
                                                          std::size_t bins) noexcept {
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
