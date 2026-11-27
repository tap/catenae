/// @file fft_fir_kernel.h
/// @brief The FFT FIR's one piece of arithmetic between its transforms: each bin times its
/// filter's, compiled in the core (src/fft_fir.cpp) at contraction off.
// Copyright 2026 Timothy Place and the catenae contributors
//
// In the compiled core and not the header, so that no caller's flags can fuse its multiply-adds:
// with the transforms, which are compiled too, an FFT FIR's output bits are then a function of
// its input and its design alone, on every target (fft_fir.h's card).
#ifndef CATENAE_DETAIL_FFT_FIR_KERNEL_H
#define CATENAE_DETAIL_FFT_FIR_KERNEL_H

#include <cstddef>

#include "catenae/detail/abi.h"
#include "catenae/sample_format.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// frame[k] = frame[k] h[k] for k below `bins`: (a + ib)(c + id) is (ac - bd) + i(ad + bc),
            /// each product rounded, then each sum. Precondition: both hold `bins` values.
            void fft_fir_product(complex<float>* frame, const complex<float>* h, std::size_t bins) noexcept;
            /// The same in double.
            void fft_fir_product(complex<double>* frame, const complex<double>* h, std::size_t bins) noexcept;

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_FFT_FIR_KERNEL_H
