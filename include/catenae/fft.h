/// @file fft.h
/// @brief The real FFT: `fft_plan<S>` over the constant `fft_tables<S, N>`, D25's frame in place.
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Frame     | N/2 + 1 values of `complex<storage>`, DC to Nyquist, every bin stored (D25)      |
/// | Forward   | X[k] = sum x[n] exp(-i 2 pi k n / N), unscaled in float and double               |
/// | Input     | the N real samples as N/2 complex values: entry n holds {x[2n], x[2n + 1]}       |
/// | Inverse   | in place; returns e: the frame x 2^e is sum X[k] exp(+i 2 pi k n / N), = N x     |
/// | Lengths   | a power of two from `fft_min_size` (4) to `fft_max_size` (65536)                 |
/// | Tables    | `fft_tables<S, N>`: constant, in ROM, one per length and type, from the core     |
/// | Run time  | `fft_plan<S>::make(n, workspace)`: the same tables built into a caller's storage |
/// | Back end  | `CATENAE_FFT_F32`, `_F64`, `_Q15`, `_Q31`; own by default; `none` leaves it out  |
/// | Formats   | float and double (the own engine); q15 and q31 declared, the engine at C6       |
/// | Limits    | no heap, no exceptions; a transform is noexcept, allocation-free, re-entrant     |
/// | Method    | split-radix DIF of length N/2 and the real post-pass (src/fft_srdif.h, cited)    |
/// | Rationale | D25, D28, D38, D48, D4, D22                                                      |
///
/// @req{CAT-FFT-001} The forward writes, for N real samples, the N/2 + 1 bins of
/// sum x[n] exp(-i 2 pi k n / N), DC to Nyquist, with the imaginary parts of DC and Nyquist zero,
/// within the floor its battery pins against a double-length reference.
/// @req{CAT-FFT-002} The inverse returns e, and the frame it writes times 2^e is
/// sum X[k] exp(+i 2 pi k n / N) over the full spectrum: a forward then an inverse gives
/// N x 2^-e = (N/2) x in float and double, where e is 1 at every length.
/// @req{CAT-FFT-003} The output bits of a forward and an inverse are a function of the input
/// and the length alone, pinned per length from 4 to 65536 in float and double.
/// @req{CAT-FFT-004} A length is accepted exactly when it is a power of two from 4 to 65536:
/// `fft_tables<S, N>` for any other N does not compile, and `make()` returns
/// `status::out_of_range`, and `status::size_mismatch` for a workspace below
/// `fft_workspace_size<S>(n)`.
/// @req{CAT-FFT-005} A plan made at run time holds the same tables, bit for bit, as
/// `fft_tables<S, N>`, and transforms to the same bits.
/// @req{CAT-FFT-006} A plan is a trivially copyable constant: a view of its tables and its
/// length. A transform writes only the frame it is given, so plans and tables are shared by
/// any number of threads at once.
/// @req{CAT-FFT-007} The fixed-point formats' forward grows the output by a constant of the
/// length: `fft_forward_exponent<q15>(n)` is log2 n, `<q31>` log2 n + 1, and 0 for float and
/// double. Their engine arrives at C6; until then a q15 or q31 plan does not compile.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FFT_H
#define CATENAE_FFT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/expected.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// The shortest length the own engines transform.
        inline constexpr std::size_t fft_min_size = 4;
        /// The longest length the own engines transform.
        inline constexpr std::size_t fft_max_size = 65536;

        /// Whether n is a length the own engines transform: a power of two from 4 to 65536.
        constexpr bool fft_supports(std::size_t n) noexcept {
            return n >= fft_min_size && n <= fft_max_size && (n & (n - 1u)) == 0u;
        }

        /// The bins of a length-n frame: n/2 + 1, DC to Nyquist (D25).
        constexpr std::size_t fft_bins(std::size_t n) noexcept {
            return n / 2u + 1u;
        }

        namespace detail {

            /// log2 n, for a power of two.
            constexpr int fft_log2(std::size_t n) noexcept {
                int l = 0;
                while ((std::size_t{1} << static_cast<unsigned>(l)) < n) {
                    ++l;
                }
                return l;
            }

            /// Whether this build carries an FFT for S, and its engine is here (C4: float, double).
            template <typename S>
            struct fft_format;
            template <>
            struct fft_format<float> {
                static constexpr bool k_built = CATENAE_FFT_F32 != CATENAE_FFT_NONE;
                static constexpr bool k_ready = true;
            };
            template <>
            struct fft_format<double> {
                static constexpr bool k_built = CATENAE_FFT_F64 != CATENAE_FFT_NONE;
                static constexpr bool k_ready = true;
            };
            template <>
            struct fft_format<q15> {
                static constexpr bool k_built = CATENAE_FFT_Q15 != CATENAE_FFT_NONE;
                static constexpr bool k_ready = false; // the fixed-point engine arrives at C6 (D48)
            };
            template <>
            struct fft_format<q31> {
                static constexpr bool k_built = CATENAE_FFT_Q31 != CATENAE_FFT_NONE;
                static constexpr bool k_ready = false;
            };

            /// The values of the floating engine's table at length n: the post-pass's n/2, the
            /// fused pass's 12 at n = 64 and 48 from n = 128, and 12 (n/32 - 1) from n = 256.
            constexpr std::size_t fft_float_table_size(std::size_t n) noexcept {
                const std::size_t m = n / 2u;
                return n / 2u + (m >= 64u ? 48u : (m == 32u ? 12u : 0u)) + (m >= 128u ? 12u * (m / 16u - 1u) : 0u);
            }

            /// The assertions every FFT name makes of its format.
            template <typename S>
            struct fft_checked {
                static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                                  || std::is_same<S, q15>::value || std::is_same<S, q31>::value,
                              "catenae: an FFT is float, double, q15 or q31");
                static_assert(!std::is_same<S, float>::value || fft_format<float>::k_built,
                              "catenae: this build has no float FFT (CATENAE_FFT_F32=none)");
                static_assert(!std::is_same<S, double>::value || fft_format<double>::k_built,
                              "catenae: this build has no double FFT (CATENAE_FFT_F64=none)");
                static_assert(!std::is_same<S, q15>::value || fft_format<q15>::k_built,
                              "catenae: this build has no q15 FFT (CATENAE_FFT_Q15=none)");
                static_assert(!std::is_same<S, q31>::value || fft_format<q31>::k_built,
                              "catenae: this build has no q31 FFT (CATENAE_FFT_Q31=none)");
                static_assert(fft_format<S>::k_ready, "catenae: the fixed-point FFT engine arrives at C6 (D48)");
                static constexpr bool k_ok = true;
            };

        } // namespace detail

        /// How many bits the forward grows a length-n spectrum by, a constant of the format and
        /// the length, so a spectrum's format is its type (D38): 0 for float and double, log2 n
        /// for q15 and log2 n + 1 for q31.
        template <typename S>
        constexpr int fft_forward_exponent(std::size_t n) noexcept {
            if constexpr (std::is_same<S, q15>::value) {
                return detail::fft_log2(n);
            }
            else if constexpr (std::is_same<S, q31>::value) {
                return detail::fft_log2(n) + 1;
            }
            else {
                static_cast<void>(n);
                return 0;
            }
        }

        /// The values of `storage_t<S>` a length-n plan's tables hold.
        template <typename S>
        constexpr std::size_t fft_table_size(std::size_t n) noexcept {
            static_assert(detail::fft_checked<S>::k_ok, "");
            return detail::fft_float_table_size(n);
        }

        /// The values of `storage_t<S>` `fft_plan<S>::make(n, workspace)` needs: its tables.
        template <typename S>
        constexpr std::size_t fft_workspace_size(std::size_t n) noexcept {
            return fft_table_size<S>(n);
        }

        /// The constant tables of one length and format, in ROM and shared by every plan of
        /// that length. Defined in the compiled core, so an image holds only the lengths it uses.
        template <typename S, std::size_t N>
        struct fft_tables {
            static_assert(detail::fft_checked<S>::k_ok, "");
            static_assert(fft_supports(N), "catenae: an FFT length is a power of two from 4 to 65536");
            /// The tables (src/fft_srdif.h says what is where).
            static const std::array<storage_t<S>, fft_table_size<S>(N)> values;
        };

        /// @brief A real FFT of one length: a view of its constant tables and the length.
        template <typename S>
        class fft_plan {
            static_assert(detail::fft_checked<S>::k_ok, "");

          public:
            using value_type   = storage_t<S>;        ///< a sample
            using complex_type = complex<value_type>; ///< a bin

            /// The plan over `fft_tables<S, N>`, a constant.
            template <std::size_t N>
            static constexpr fft_plan of() noexcept {
                return fft_plan(fft_tables<S, N>::values.data(), N);
            }

            /// A plan of length n whose tables are built into `workspace`, which must hold
            /// `fft_workspace_size<S>(n)` values and outlive the plan. Not for the audio thread:
            /// it computes N/8 angles in integer arithmetic.
            static expected<fft_plan, status> make(std::size_t n, span<value_type> workspace) noexcept;

            /// The length N.
            constexpr std::size_t size() const noexcept { return m_size; }

            /// The forward transform in place over `frame`, N/2 + 1 values: the samples in, as
            /// {x[2n], x[2n + 1]} in entries 0 to N/2 - 1, and the bins out.
            void forward(span<complex_type> frame) const noexcept;

            /// The inverse transform in place over `frame`: the bins in, and the samples out
            /// as the forward reads them, scaled so that the frame x 2^e is the unnormalized
            /// inverse. @return e.
            [[nodiscard]] int inverse(span<complex_type> frame) const noexcept;

          private:
            constexpr fft_plan(const value_type* tables, std::size_t n) noexcept
                : m_tables(tables)
                , m_size(n) {}

            const value_type* m_tables; ///< fft_table_size<S>(m_size) values
            std::size_t       m_size;   ///< N
        };

        /// The lengths the compiled core holds tables for, one explicit instantiation each.
#define CATENAE_FFT_LENGTHS(X)                                                                                         \
    X(4) X(8) X(16) X(32) X(64) X(128) X(256) X(512) X(1024) X(2048) X(4096) X(8192) X(16384) X(32768) X(65536)

#if CATENAE_FFT_F32 != CATENAE_FFT_NONE
#define CATENAE_FFT_DECLARE_F32(n) extern template struct fft_tables<float, n>;
        CATENAE_FFT_LENGTHS(CATENAE_FFT_DECLARE_F32)
#undef CATENAE_FFT_DECLARE_F32
        extern template class fft_plan<float>;
#endif
#if CATENAE_FFT_F64 != CATENAE_FFT_NONE
#define CATENAE_FFT_DECLARE_F64(n) extern template struct fft_tables<double, n>;
        CATENAE_FFT_LENGTHS(CATENAE_FFT_DECLARE_F64)
#undef CATENAE_FFT_DECLARE_F64
        extern template class fft_plan<double>;
#endif

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FFT_H
