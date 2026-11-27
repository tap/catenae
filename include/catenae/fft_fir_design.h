/// @file fft_fir_design.h
/// @brief The FFT FIR's design: the spectrum of an FIR design's taps, made in double into the
/// caller's table, or a checked table in ROM (D37, PLAN.md 3.3).
///
/// | Contract    |                                                                             |
/// |-------------|-----------------------------------------------------------------------------|
/// | A design    | `fft_fir_design<S, Taps, Block, Phase>`: Block + 1 bins of `complex<S>`, the |
/// |             | spectrum of a `fir_design<S, Taps, Phase>`'s taps zero-padded to N = 2 Block |
/// |             | samples, times 2/N; made only by `make()` or `from_table()`, and always a   |
/// |             | view of the caller's table (`table_view`): past 512 bytes from Block = 64 in |
/// |             | float, and a shorter FIR runs cheaper direct                                 |
/// | Block       | a power of two from 2 to 32,768 and at least Taps - 1, since overlap-save     |
/// |             | keeps the last Block samples of each window of 2 Block; `fft_fir_block_for`  |
/// |             | (Taps) is the smallest at least max(Taps - 1, 16), DspTap's default          |
/// | Make        | `make(taps, table, workspace)`: the taps widened to double, the double      |
/// |             | engine's forward over N, times 2/N (exact: N is a power of two), each part  |
/// |             | rounded once to S into `table`, which the design then views, through        |
/// |             | `workspace_size` complex doubles of the caller's; `status::out_of_range`    |
/// |             | when a bin does not fit S. It needs the double FFT (`CATENAE_FFT_F64`)       |
/// | Table       | `from_table` checks in S: every part finite, and the DC and Nyquist bins    |
/// |             | real, as a real filter's are. That a spectrum is of linear-phase taps is its |
/// |             | generator's promise (`tools/tablegen`, checked by CTest): no check without   |
/// |             | `libm` can see it. A failing table does not initialise a constant, and one  |
/// |             | met at run time reaches `check::table_rejected`                              |
/// | Latency     | the taps design's: (Taps - 1) / 2 frames at linear phase, else as it states |
/// | Cost        | `make()` runs in the caller's frame, outside the audio path: one double      |
/// |             | transform of N, software double on a single-precision FPU. Cortex-M33, worst |
/// |             | call (Bench run 46021806011): 255 float taps in blocks of 256, 0.59 M        |
/// |             | instructions and 872 B of stack                                              |
/// | Formats     | float and double; q31 declares its contract, and its spectrum's format and   |
/// |             | its arithmetic are new design at C6 (D47)                                    |
/// | Rationale   | D24, D25, D37, D47, D54, PLAN.md 3.3                                         |
///
/// @req{CAT-FFT-FIR-DESIGN-001} `make()` writes, for each bin k up to Block, the forward of the
/// taps zero-padded to 2 Block in double, times 1 / Block, each part rounded once to S, and the
/// design views that table; its latency is the taps design's.
/// @req{CAT-FFT-FIR-DESIGN-002} `from_table` accepts a table whose parts are finite and whose DC
/// and Nyquist bins have zero imaginary parts, bit for bit, and refuses any other: in a constant
/// it does not compile (`compile_fail/fft_fir_table_not_real.cpp`), at run time
/// `check::table_rejected`.
/// @req{CAT-FFT-FIR-DESIGN-003} A block that is not a power of two from 2 to 32,768 at least
/// Taps - 1 does not compile, and `fft_fir_block_for(taps)` is the smallest power of two at least
/// max(taps - 1, 16).
/// @req{CAT-FFT-FIR-DESIGN-004} In q31 the design's contract is instantiated, and its `make()`
/// and `from_table()` do not compile until its profile arrives at C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FFT_FIR_DESIGN_H
#define CATENAE_FFT_FIR_DESIGN_H

#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/expected.h"
#include "catenae/fft.h"
#include "catenae/fir_design.h"
#include "catenae/sample_format.h"
#include "catenae/sample_traits.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// The longest block an FFT FIR runs: its transform of twice that is the longest with
        /// constant tables (`fft_max_table_size`).
        inline constexpr std::size_t max_fft_fir_block = fft_max_table_size / 2;

        /// The default block for `taps` taps: the smallest power of two at least
        /// max(taps - 1, 16) (DspTap's).
        constexpr std::size_t fft_fir_block_for(std::size_t taps) noexcept {
            const std::size_t least = taps > 17 ? taps - 1 : 16;
            std::size_t       block = 16;
            while (block < least) {
                block *= 2;
            }
            return block;
        }

        namespace detail {

            /// Whether a block is one an FFT FIR of `taps` taps runs.
            constexpr bool fft_fir_block_ok(std::size_t taps, std::size_t block) noexcept {
                return block >= 2 && block <= max_fft_fir_block && (block & (block - 1u)) == 0u && block + 1 >= taps;
            }

            /// Whether every part of `h` is finite and its DC and Nyquist bins are real, in their
            /// own type.
            template <typename C, std::size_t Bins>
            constexpr bool fft_fir_table_ok(const complex<C>* h) noexcept {
                bool finite = true;
                for (std::size_t k = 0; k < Bins; ++k) {
                    finite = finite && fir_finite(h[k].re) && fir_finite(h[k].im);
                }
                return finite && fir_same(h[0].im, C{}) && fir_same(h[Bins - 1].im, C{});
            }

        } // namespace detail

        /// @brief The spectrum an FFT FIR multiplies by: of a `fir_design<S, Taps, Phase>`'s taps
        /// over a transform of 2 Block, times 2 / (2 Block), viewed. A class, not an aggregate:
        /// only `make()` and `from_table()` produce one.
        template <typename S, std::size_t Taps, std::size_t Block, fir_phase Phase = fir_phase::linear>
        class fft_fir_design : private detail::fir_stored_latency<Phase> {
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value
                              || std::is_same<S, q31>::value,
                          "catenae: an FFT FIR design is float or double, and q31's contract");
            static_assert(Taps >= 1, "catenae: an FFT FIR has at least one tap");
            static_assert(detail::fft_fir_block_ok(Taps, Block),
                          "catenae: an FFT FIR's block is a power of two from 2 to 32,768, at least Taps - 1");
            using stored_latency = detail::fir_stored_latency<Phase>;

          public:
            using value_type                       = storage_t<S>;               ///< one part of a bin
            using bin_type                         = complex<value_type>;        ///< one bin
            using taps_type                        = fir_design<S, Taps, Phase>; ///< what make() takes
            static constexpr std::size_t tap_count = Taps;                       ///< the FIR's taps
            static constexpr std::size_t block     = Block;                      ///< frames a block
            static constexpr std::size_t length    = 2 * Block;                  ///< the transform's N
            static constexpr std::size_t bins      = Block + 1;                  ///< N/2 + 1, DC to Nyquist
            static constexpr fir_phase   phase     = Phase;                      ///< linear or any
            using table_type                       = std::array<bin_type, bins>; ///< the spectrum
            /// The complex doubles of the caller's make() works in.
            static constexpr std::size_t workspace_size = bins;

            /// The spectrum of `taps`, written into `table`, which the design views, through
            /// `workspace`: `status::out_of_range` if a bin does not fit S. Outside the audio path.
            static expected<fft_fir_design, status> make(const taps_type& taps, span<bin_type, bins> table,
                                                         span<complex<double>, workspace_size> workspace) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point FFT FIR's spectrum is new design and arrives with its profile at "
                              "C6 (D47)");
                using traits = sample_traits<value_type>;
                for (complex<double>& v : workspace) {
                    v = complex<double>{0.0, 0.0};
                }
                const typename taps_type::coefficient_type* const h = taps.data();
                for (std::size_t k = 0; k < Taps; ++k) { // the forward reads {x[2n], x[2n + 1]}
                    const auto v = static_cast<double>(h[k]);
                    if (k % 2 == 0) {
                        workspace[k / 2].re = v;
                    }
                    else {
                        workspace[k / 2].im = v;
                    }
                }
                fft_plan<double>::of<length>().forward(workspace);
                const double scale = 1.0 / static_cast<double>(Block); // 2/N, a power of two: exact
                bool         fits  = true;
                for (std::size_t k = 0; k < bins; ++k) {
                    const value_type re = traits::make_coeff(workspace[k].re * scale);
                    const value_type im = traits::make_coeff(workspace[k].im * scale);
                    fits                = fits && detail::fir_finite(re) && detail::fir_finite(im);
                    table[k]            = bin_type{re, im};
                }
                if (!fits) {
                    return unexpected(status::out_of_range);
                }
                return fft_fir_design(table_view<bin_type, bins>(table), taps.latency());
            }

            /// A linear-phase FFT FIR from its spectrum, checked: finite, DC and Nyquist real. A
            /// table that fails does not initialise a constant; at run time it reaches the handler.
            static constexpr fft_fir_design from_table(const table_view<bin_type, bins>& spectrum) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point FFT FIR's spectrum is new design and arrives with its profile at "
                              "C6 (D47)");
                static_assert(Phase == fir_phase::linear,
                              "catenae: an FFT FIR of any taps states its latency: from_table(spectrum, latency)");
                detail::check_always(detail::fft_fir_table_ok<value_type, bins>(spectrum.data()),
                                     check::table_rejected);
                return fft_fir_design(spectrum, made_latency());
            }

            /// Any FFT FIR from its spectrum and the latency its generator measured, checked as
            /// the linear one is, and the latency finite and not negative.
            static constexpr fft_fir_design from_table(const table_view<bin_type, bins>& spectrum,
                                                       stage_latency                     latency) noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point FFT FIR's spectrum is new design and arrives with its profile at "
                              "C6 (D47)");
                static_assert(Phase == fir_phase::any,
                              "catenae: a linear-phase FFT FIR's latency is its type's: from_table(spectrum)");
                detail::check_always(detail::fft_fir_table_ok<value_type, bins>(spectrum.data())
                                         && latency.nominal >= 0.0f && latency.deviation >= 0.0f
                                         && latency.nominal <= std::numeric_limits<float>::max()
                                         && latency.deviation <= std::numeric_limits<float>::max(),
                                     check::table_rejected);
                return fft_fir_design(spectrum, latency);
            }

            /// Bins 0 to Block.
            constexpr span<const bin_type, bins> table() const noexcept { return m_spectrum.values(); }
            /// The first bin, for a kernel.
            constexpr const bin_type* data() const noexcept { return m_spectrum.data(); }
            /// `{nominal, deviation}`, in frames: (Taps - 1) / 2 and 0 for linear phase.
            constexpr stage_latency latency() const noexcept {
                if constexpr (Phase == fir_phase::linear) {
                    return made_latency();
                }
                else {
                    return this->stored;
                }
            }

          private:
            constexpr fft_fir_design(const table_view<bin_type, bins>& spectrum, stage_latency latency) noexcept
                : stored_latency(latency)
                , m_spectrum(spectrum) {}

            /// A linear-phase design's latency.
            static constexpr stage_latency made_latency() noexcept {
                return {static_cast<float>(Taps - 1) * 0.5f, 0.0f};
            }

            table_view<bin_type, bins> m_spectrum;
        };

        /// Any FIR's spectrum: its latency the design's, not its type's.
        template <typename S, std::size_t Taps, std::size_t Block>
        using fft_fir_any_design = fft_fir_design<S, Taps, Block, fir_phase::any>;

        /// A design always views its spectrum.
        template <typename S, std::size_t Taps, std::size_t Block, fir_phase Phase>
        struct holds_view<fft_fir_design<S, Taps, Block, Phase>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FFT_FIR_DESIGN_H
