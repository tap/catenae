/// @file fft.cpp
/// @brief The floating FFT: the constant tables of every length and the plans' transforms.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Each table is computed by the engine's own constexpr statements (src/fft_srdif.h) while this
// file compiles, and is constant-initialized: in .rodata, one section per length and format,
// so an image keeps only the lengths it uses. The build raises the compiler's constant-evaluation
// limits for this file (CMakeLists.txt, the wscript): the longest table is 65536 / 8 angles.
#include "catenae/fft.h"

#include <array>
#include <cstddef>

#include "catenae/compiler.h"
#include "fft_srdif.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// The table of one length, built by the engine's own statements.
            template <typename T, std::size_t N>
            constexpr std::array<T, fft_float_table_size(N)> make_fft_table() noexcept {
                std::array<T, fft_float_table_size(N)> t{};
                srdif<T>::fill_tables(t.data(), N);
                return t;
            }

        } // namespace detail

        template <typename S, std::size_t N>
        CATENAE_CONSTINIT const std::array<storage_t<S>, fft_table_size<S>(N)> fft_tables<S, N>::values =
            detail::make_fft_table<storage_t<S>, N>();

        template <typename S>
        expected<fft_plan<S>, status> fft_plan<S>::make(std::size_t n, span<value_type> workspace) noexcept {
            if (!fft_supports(n)) {
                return unexpected(status::out_of_range);
            }
            if (workspace.size() < fft_workspace_size<S>(n)) {
                return unexpected(status::size_mismatch);
            }
            detail::srdif<value_type>::fill_tables(workspace.data(), n);
            return fft_plan(workspace.data(), n);
        }

        template <typename S>
        void fft_plan<S>::forward(span<complex_type> frame) const noexcept {
            detail::srdif<value_type>::forward(m_tables, m_size, frame.data());
        }

        template <typename S>
        int fft_plan<S>::inverse(span<complex_type> frame) const noexcept {
            detail::srdif<value_type>::inverse(m_tables, m_size, frame.data());
            return 1;
        }

#if CATENAE_FFT_F32 != CATENAE_FFT_NONE
#define CATENAE_FFT_DEFINE_F32(n) template struct fft_tables<float, n>;
        CATENAE_FFT_LENGTHS(CATENAE_FFT_DEFINE_F32)
#undef CATENAE_FFT_DEFINE_F32
        template class fft_plan<float>;
#endif
#if CATENAE_FFT_F64 != CATENAE_FFT_NONE
#define CATENAE_FFT_DEFINE_F64(n) template struct fft_tables<double, n>;
        CATENAE_FFT_LENGTHS(CATENAE_FFT_DEFINE_F64)
#undef CATENAE_FFT_DEFINE_F64
        template class fft_plan<double>;
#endif

    } // namespace CATENAE_ABI
} // namespace catenae
