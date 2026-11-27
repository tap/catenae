/// @file fft_fir.h
/// @brief `fft_fir`: an FIR by fast convolution (overlap-save), a stage that works in blocks;
/// `fft_fir_any`, any FIR, its latency its design's (PLAN.md 9.5, 10: C4).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                     |
/// | Ratio      | 1:1, input group `Block`, up to `Block - 1` frames behind, never ahead,     |
/// |            | quantum `Block`: a block is written whole when its last frame arrives;     |
/// |            | `steady<>` makes it frame-synchronous, `Block - 1` frames later             |
/// | Configured | an `fft_fir_design<S, Taps, Block, Phase>`, which views its spectrum, so    |
/// |            | the stage `holds_view` (D54); `set_design()` between calls                  |
/// | State      | per channel the block before; the block so far, interleaved; its count;    |
/// |            | rests at zero                                                                |
/// | Workspace  | one frame of `Block + 1` bins, one channel at a time                         |
/// | Method     | overlap-save (Oppenheim and Schafer, 3rd ed., 8.7.3; Stockham 1966): per    |
/// |            | channel the window of the block before and this one, N = 2 Block samples,  |
/// |            | through the forward, times the design's spectrum, through the inverse; its |
/// |            | last `Block` samples are the linear convolution, since Block >= Taps - 1    |
/// | Arithmetic | in S, all of it in the compiled core: the transforms and the product by the |
/// |            | spectrum (`detail/fft_fir_kernel.h`), at contraction off; the header only   |
/// |            | copies. So the output's bits are a function of the input and the design    |
/// |            | alone, on every target, float included                                       |
/// | Latency    | `fft_fir`: exactly (Taps - 1) / 2 frames, a constant of the type, the        |
/// |            | waiting for a block apart (it is the shortfall); `fft_fir_any`: not exact,  |
/// |            | its design's `{nominal, deviation}`                                          |
/// | Tail       | 2 Block - 1 frames, exact and practical, no residue: then every window that |
/// |            | is still to be written holds only zeros, and transforms to zero exactly    |
/// | Cost       | all of a block's work is on the call that completes it: 2 Ch transforms of |
/// |            | N and Ch products of Block + 1 bins; the other calls only copy              |
/// | Formats    | float and double; q31 declares its contract, and its arithmetic is new      |
/// |            | design at C6 (D47)                                                           |
/// | Limits     | a non-finite input makes every output of its channel in two blocks         |
/// |            | non-finite, the windows that hold it; then they hold it no longer          |
/// | Rationale  | D24, D35, D37, D39, D47, D49, D50, D54, PLAN.md 3.2, 9.5                     |
///
/// @req{CAT-FFT-FIR-001} From reset, each channel's output is the circular convolution of each
/// window of 2 Block samples (the block before, then this one) with the design, by its spectrum,
/// keeping the last Block samples, written when the block's last frame arrives: in a block
/// whole, every channel alike, the same bits for any chunking.
/// @req{CAT-FFT-FIR-002} It is the direct convolution by the design's taps within the floor its
/// battery states, in float and in double, and `fft_fir` delays an impulse by exactly
/// (Taps - 1) / 2 frames, which is its exact latency; `fft_fir_any` reports its design's.
/// @req{CAT-FFT-FIR-003} Input group `Block`, shortfall `Block - 1`, no lead and quantum `Block`;
/// in `steady<>` it is frame-synchronous and its latency `Block - 1` frames more.
/// @req{CAT-FFT-FIR-004} After 2 Block - 1 zero frames the output is silence, exactly: both
/// tails are 2 Block - 1 and the residue 0.
/// @req{CAT-FFT-FIR-005} A design set between calls applies from the next block to the window as
/// it stands; in q31 the contract is instantiated and a run does not compile until C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FFT_FIR_H
#define CATENAE_FFT_FIR_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/detail/fft_fir_kernel.h"
#include "catenae/fft.h"
#include "catenae/fft_fir_design.h"
#include "catenae/fir_design.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief An FIR of `Taps` taps by overlap-save in blocks of `Block` frames over a
        /// `port<S, Ch>`, from the spectrum its design holds.
        template <typename S, std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps),
                  fir_phase Phase = fir_phase::linear>
        class basic_fft_fir {
            static_assert(Ch >= 1, "catenae: an FFT FIR has at least one channel");

          public:
            using input_port  = port<S, Ch>;                           ///< what it reads
            using output_port = port<S, Ch>;                           ///< what it writes
            using design_type = fft_fir_design<S, Taps, Block, Phase>; ///< what it runs

          private:
            using value_type   = storage_t<S>;
            using complex_type = complex<value_type>;

          public:
            static constexpr ratio       rate_ratio{1, 1};                                        ///< 1:1
            static constexpr std::size_t input_group = Block;                                     ///< a block
            static constexpr ratio       max_shortfall{static_cast<std::uint32_t>(Block - 1), 1}; ///< waiting
            static constexpr ratio       max_lead{0, 1};                                          ///< never ahead
            static constexpr std::size_t output_quantum    = Block;                               ///< a block whole
            static constexpr bool        is_in_place       = false;                               ///< it bursts
            static constexpr bool        has_exact_latency = Phase == fir_phase::linear;          ///< linear phase only
            /// (Taps - 1) / 2 frames for linear phase, reduced; 0 otherwise.
            static constexpr ratio exact_latency =
                has_exact_latency ? reduce(ratio{static_cast<std::uint32_t>(Taps - 1), 2}) : ratio{0, 1};
            static constexpr bool rests_at_zero = true; ///< silence before, nothing gathered

            /// What one instance remembers.
            struct state {
                std::array<value_type, Ch * Block> held;     ///< this block's frames so far, interleaved
                std::array<value_type, Ch * Block> previous; ///< the block before, channel after channel
                std::uint32_t                      count;    ///< the frames in `held`, below Block
            };
            /// What a call needs: one channel's frame at a time.
            struct workspace {
                std::array<complex_type, Block + 1> frame; ///< the window, then its bins, then its samples
            };

            /// An FFT FIR running `design`.
            explicit constexpr basic_fft_fir(const design_type& design) noexcept
                : m_design(design) {}

            /// The design it runs.
            constexpr const design_type& design() const noexcept { return m_design; }
            /// A new design, applied from the next block to the window as it stands. Between
            /// calls only; it cannot fail.
            constexpr void set_design(const design_type& design) noexcept { m_design = design; }

            /// The frames into the block; each block, when its last frame arrives, out whole.
            std::size_t run(state& st, workspace& ws, span<const value_type> in, span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: a fixed-point FFT FIR is new design and arrives with its profile at C6 (D47)");
                const std::size_t frames = in.size() / Ch;
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(st.count < Block, check::invariant);
                    detail::check_audit(out.size() >= outputs_for(st, frames) * Ch, check::index_out_of_range);
                }
                const value_type* from    = in.data();
                value_type* const to      = out.data();
                std::size_t       count   = st.count;
                std::size_t       left    = frames;
                std::size_t       written = 0;
                while (left > 0) {
                    const std::size_t take = std::min(left, Block - count);
                    std::copy(from, from + take * Ch, st.held.data() + count * Ch);
                    from += take * Ch;
                    left -= take;
                    count += take;
                    if (count == Block) {
                        block(st, ws, to + written * Ch);
                        written += Block;
                        count = 0;
                    }
                }
                st.count = detail::narrow<std::uint32_t>(count);
                return written;
            }

            /// Whole blocks: what the frames so far and n more complete.
            static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
                return (st.count + n) / Block * Block;
            }
            /// The frames that complete enough blocks for k.
            static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
                return k == 0 ? 0 : (k + Block - 1) / Block * Block - st.count;
            }
            /// From any state: up to Block - 1 frames gathered, then n.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept {
                return (Block - 1 + n) / Block * Block;
            }
            /// From rest, a block is written when its last frame arrives.
            static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept { return n / Block * Block; }
            /// The design's `{nominal, deviation}`: (Taps - 1) / 2 and 0 for linear phase.
            stage_latency latency() const noexcept { return m_design.latency(); }
            /// 2 Block - 1: the last window holding a sample written, the rest are zeros.
            std::size_t tail_frames() const noexcept { return 2 * Block - 1; }
            /// 2 Block - 1, as exact.
            std::size_t practical_tail_frames() const noexcept { return 2 * Block - 1; }
            /// None: a window of zeros transforms to zero.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            /// The block just completed, each channel through its window, out interleaved.
            void block(state& st, workspace& ws, value_type* to) const noexcept {
                constexpr std::size_t half = Block / 2;
                constexpr auto        plan = fft_plan<S>::template of<2 * Block>();
                complex_type* const   f    = ws.frame.data();
                for (std::size_t c = 0; c < Ch; ++c) {
                    value_type* const       before = st.previous.data() + c * Block;
                    const value_type* const now    = st.held.data() + c; // every Ch-th
                    for (std::size_t n = 0; n < half; ++n) {             // {x[2n], x[2n + 1]}, the window
                        f[n]        = complex_type{before[2 * n], before[2 * n + 1]};
                        f[half + n] = complex_type{now[2 * n * Ch], now[(2 * n + 1) * Ch]};
                    }
                    for (std::size_t i = 0; i < Block; ++i) {
                        before[i] = now[i * Ch];
                    }
                    plan.forward(ws.frame);
                    detail::fft_fir_product(f, m_design.data(), Block + 1);
                    const int e = plan.inverse(ws.frame); // 2/N, so 2^-e, is in the spectrum
                    detail::check_audit(e == 1, check::invariant);
                    for (std::size_t n = 0; n < half; ++n) { // the window's last Block samples
                        to[2 * n * Ch + c]       = f[half + n].re;
                        to[(2 * n + 1) * Ch + c] = f[half + n].im;
                    }
                }
            }

            design_type m_design;
        };

        template <std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps)>
        using fft_fir = basic_fft_fir<float, Ch, Taps, Block>; ///< linear phase, in float (D24)
        template <std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps)>
        using fft_fir_f64 = basic_fft_fir<double, Ch, Taps, Block>; ///< linear phase, in double
        template <std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps)>
        using fft_fir_q31 = basic_fft_fir<q31, Ch, Taps, Block>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps)>
        using basic_fft_fir_any = basic_fft_fir<S, Ch, Taps, Block, fir_phase::any>; ///< any FIR
        template <std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps)>
        using fft_fir_any = basic_fft_fir_any<float, Ch, Taps, Block>; ///< any FIR, in float (D24)
        template <std::size_t Ch, std::size_t Taps, std::size_t Block = fft_fir_block_for(Taps)>
        using fft_fir_any_f64 = basic_fft_fir_any<double, Ch, Taps, Block>; ///< any FIR, in double

        template <typename S, std::size_t Ch, std::size_t Taps, std::size_t Block, fir_phase Phase>
        struct is_stage<basic_fft_fir<S, Ch, Taps, Block, Phase>> : std::true_type {};
        /// An FFT FIR views its design's spectrum.
        template <typename S, std::size_t Ch, std::size_t Taps, std::size_t Block, fir_phase Phase>
        struct holds_view<basic_fft_fir<S, Ch, Taps, Block, Phase>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FFT_FIR_H
