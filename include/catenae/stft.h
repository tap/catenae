/// @file stft.h
/// @brief `stft` and `istft`: a time-domain port into frames of spectra and back, windowed,
/// overlapped and resynthesized by Griffin and Lim's least-squares rule (PLAN.md 5.2, 9.5: C5).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `stft`: `port<S, Ch>` into `spectrum<S, Ch, N/2 + 1>`; `istft` the reverse   |
/// | Geometry   | frames of N, a power of two from 16 to 65536, every `Hop` frames; Hop       |
/// |            | divides N and is at most N/2, so every sample is in two frames at least      |
/// | Analysis   | per channel, the last N samples times the window w, through the forward     |
/// |            | transform (D25): frame k's window is the samples k Hop + Hop - N to k Hop +  |
/// |            | Hop - 1 since reset, samples before reset being 0. It is stamped by its     |
/// |            | first sample: so its exact latency is (N - Hop) / Hop of its frames         |
/// | Synthesis  | per channel, each frame through the inverse, times w, overlap-added, and   |
/// |            | each sample divided by sum_m w(n - m Hop)^2 (Griffin and Lim, "Signal       |
/// |            | estimation from modified short-time Fourier transform", IEEE Trans. ASSP    |
/// |            | 32(2), 1984): the least-squares signal of a modified spectrum, and the      |
/// |            | input itself of an unmodified one. A frame's first Hop samples are complete |
/// |            | when it arrives, so they are written then: latency 0                        |
/// | Pair       | `stft | istft` is 1:1, its input group Hop, and its output the input N -     |
/// |            | Hop frames later: for noise within [-1, 1], at most 4.2e-7 apart in float and |
/// |            | 8.9e-16 in double over every window at 512 by hops of 256, 128 and 64 and   |
/// |            | 4096 by 1024 (the battery pins 6e-7 and 1.2e-15)                             |
/// | Ratio      | `stft`: 1/Hop, input group Hop, up to (Hop - 1) / Hop of a frame behind, a  |
/// |            | frame at a time; `istft`: Hop/1, frame by frame, Hop frames at a time        |
/// | Configured | a `window_design<S, N>`, which views its table, so both hold a view (D54);  |
/// |            | `stft{window::hann}` takes a named window's constant                        |
/// | State      | `stft`: per channel the window's samples and the hop's count, at rest all   |
/// |            | zero; `istft`: per channel the overlap-add, and the Hop divisors, scaled by |
/// |            | 2/N, that its `reset` computes in S from the window                         |
/// | Workspace  | `stft`: none, each channel transformed in its own place in the output;      |
/// |            | `istft`: one frame of N/2 + 1 bins                                           |
/// | Tail       | `stft` N - 1 frames, `istft` N/Hop - 1 frames; exact, no residue: zeros in   |
/// |            | transform to zeros                                                           |
/// | Formats    | float and double; q31 declares its contract, and fixed-point spectra arrive |
/// |            | at C6 (D38, D47)                                                             |
/// | Contraction| pinned at contraction off: the transforms are the core's, and the windowing |
/// |            | and overlap-add are header templates under the caller's flags               |
/// | Rationale  | D24, D25, D35, D37, D38, D47, D49, D54, PLAN.md 5.2, 9.5                    |
///
/// @req{CAT-STFT-001} From reset, `stft` writes, each Hop frames, every channel's forward of
/// its last N samples times the window (samples before reset 0), bit for bit for any chunking:
/// input group Hop, shortfall (Hop - 1)/Hop, no lead, exact latency (N - Hop)/Hop frames.
/// @req{CAT-STFT-002} `istft` writes, for each frame, Hop samples per channel: the overlap-add of
/// each inverse times the window, each sample divided by its sum of squared windows, bit for bit
/// for any chunking, exact latency 0.
/// @req{CAT-STFT-003} `stft | istft` reproduces its input N - Hop frames later within the floor its
/// battery states, for every window and both formats, and its tails are exact with no residue.
/// @req{CAT-STFT-004} A geometry whose hop does not divide N, or is above N/2, does not compile;
/// in q31 the contract is instantiated and a run does not compile until C6 (D47).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_STFT_H
#define CATENAE_STFT_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/fft.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"
#include "catenae/window.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {
            /// What a frame geometry must be, asserted where a stage is formed.
            template <std::size_t N, std::size_t Hop>
            constexpr bool stft_geometry() noexcept {
                return fft_has_tables(N) && N >= window_min_size && Hop >= 1 && N % Hop == 0 && 2 * Hop <= N;
            }
        } // namespace detail

        /// @brief Frames of N windowed samples every `Hop`, into spectra, over a `port<S, Ch>`.
        template <typename S, std::size_t N, std::size_t Hop, std::size_t Ch>
        class basic_stft {
            static_assert(Ch >= 1, "catenae: an STFT has at least one channel");
            static_assert(
                detail::stft_geometry<N, Hop>(),
                "catenae: an STFT's frame is a power of two from 16 to 65536, its hop dividing it and at most "
                "half of it");

          public:
            static constexpr std::size_t bins = N / 2 + 1;             ///< per channel, DC to Nyquist
            using input_port                  = port<S, Ch>;           ///< what it reads
            using output_port                 = spectrum<S, Ch, bins>; ///< what it writes
            using window_type                 = window_design<S, N>;   ///< what it windows with

          private:
            using value_type   = storage_t<S>;
            using complex_type = complex<value_type>;

          public:
            static constexpr ratio       rate_ratio{1, static_cast<std::uint32_t>(Hop)}; ///< a frame each Hop
            static constexpr std::size_t input_group = Hop;                              ///< a hop
            static constexpr ratio max_shortfall{static_cast<std::uint32_t>(Hop - 1), static_cast<std::uint32_t>(Hop)};
            static constexpr ratio max_lead{0, 1};                  ///< never ahead
            static constexpr std::size_t output_quantum    = 1;     ///< a frame at a time
            static constexpr bool        is_in_place       = false; ///< two port types
            static constexpr bool        has_exact_latency = true;  ///< every frequency alike
            /// (N - Hop) / Hop of its frames: a frame is stamped by its window's first sample.
            static constexpr ratio exact_latency =
                reduce(ratio{static_cast<std::uint32_t>(N - Hop), static_cast<std::uint32_t>(Hop)});
            static constexpr bool rests_at_zero = true; ///< silence before, nothing gathered

            /// What one instance remembers.
            struct state {
                std::array<value_type, Ch * N> history; ///< each channel's window, oldest first
                std::uint32_t                  count;   ///< the hop's frames so far, below Hop
            };
            using workspace = no_workspace; ///< each channel transforms in its place in the output

            /// An STFT windowing with `window`.
            explicit constexpr basic_stft(const window_type& window) noexcept
                : m_window(window) {}
            /// An STFT windowing with a named window's constant.
            template <window_shape Shape>
            explicit constexpr basic_stft(window::named<Shape> shape) noexcept
                : m_window(shape) {}

            /// The window.
            constexpr const window_type& window() const noexcept { return m_window; }

            /// The frames into the hop; each hop, when its last frame arrives, a spectrum frame.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<complex_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point spectra arrive with their profile at C6 (D38, D47)");
                const std::size_t frames = in.size() / Ch;
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(st.count < Hop, check::invariant);
                    detail::check_audit(out.size() >= outputs_for(st, frames) * Ch * bins, check::index_out_of_range);
                }
                const value_type* from    = in.data();
                std::size_t       count   = st.count;
                std::size_t       left    = frames;
                std::size_t       written = 0;
                while (left > 0) {
                    const std::size_t take = std::min(left, Hop - count);
                    for (std::size_t f = 0; f < take; ++f) {
                        for (std::size_t c = 0; c < Ch; ++c) { // de-interleaved as it arrives
                            st.history[c * N + (N - Hop) + count + f] = from[f * Ch + c];
                        }
                    }
                    from += take * Ch;
                    left -= take;
                    count += take;
                    if (count == Hop) {
                        frame(st, out.data() + written * Ch * bins);
                        ++written;
                        count = 0;
                    }
                }
                st.count = detail::narrow<std::uint32_t>(count);
                return written;
            }

            /// Whole hops: the frames the hop so far and n more complete.
            static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
                return (st.count + n) / Hop;
            }
            /// The frames that complete k hops.
            static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
                return k == 0 ? 0 : k * Hop - st.count;
            }
            /// From any state: up to Hop - 1 frames gathered, then n.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return (Hop - 1 + n) / Hop; }
            /// From rest, a frame each whole hop.
            static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept { return n / Hop; }
            /// `{(N - Hop) / Hop, 0}` of its frames.
            stage_latency latency() const noexcept {
                return {static_cast<float>(N - Hop) / static_cast<float>(Hop), 0.0f};
            }
            /// N - 1: then every window still to be written holds only zeros.
            std::size_t tail_frames() const noexcept { return N - 1; }
            /// N - 1, as exact.
            std::size_t practical_tail_frames() const noexcept { return N - 1; }
            /// None: a window of zeros transforms to zero.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            /// Every channel's window, times w, through the forward into its place in `to`; then
            /// the window moves on by a hop.
            void frame(state& st, complex_type* to) const noexcept {
                constexpr auto        plan    = fft_plan<S>::template of<N>();
                constexpr std::size_t quarter = N / 4;
                const auto&           w       = m_window.half();
                for (std::size_t c = 0; c < Ch; ++c) {
                    value_type* const   h = st.history.data() + c * N;
                    complex_type* const f = to + c * bins;
                    for (std::size_t n = 0; n < quarter; ++n) { // {x[2n], x[2n + 1]}: the half before N/2
                        f[n] = complex_type{h[2 * n] * w[2 * n], h[2 * n + 1] * w[2 * n + 1]};
                    }
                    for (std::size_t n = quarter; n < N / 2; ++n) { // and after it, w mirrored
                        f[n] = complex_type{h[2 * n] * w[N - 2 * n], h[2 * n + 1] * w[N - 2 * n - 1]};
                    }
                    plan.forward(span<complex_type, bins>(f, bins));
                    std::copy(h + Hop, h + N, h);
                }
            }

            window_type m_window;
        };

        /// @brief Spectra back into a `port<S, Ch>`: each frame's inverse windowed and overlap-added,
        /// N samples every `Hop`, by the least-squares rule.
        template <typename S, std::size_t N, std::size_t Hop, std::size_t Ch>
        class basic_istft {
            static_assert(Ch >= 1, "catenae: an inverse STFT has at least one channel");
            static_assert(
                detail::stft_geometry<N, Hop>(),
                "catenae: an STFT's frame is a power of two from 16 to 65536, its hop dividing it and at most "
                "half of it");

          public:
            static constexpr std::size_t bins = N / 2 + 1;             ///< per channel, DC to Nyquist
            using input_port                  = spectrum<S, Ch, bins>; ///< what it reads
            using output_port                 = port<S, Ch>;           ///< what it writes
            using window_type                 = window_design<S, N>;   ///< what it windows with

          private:
            using value_type   = storage_t<S>;
            using complex_type = complex<value_type>;

          public:
            static constexpr ratio       rate_ratio{static_cast<std::uint32_t>(Hop), 1}; ///< Hop frames each
            static constexpr std::size_t input_group = 1;                                ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};                            ///< never behind
            static constexpr ratio       max_lead{0, 1};                                 ///< never ahead
            static constexpr std::size_t output_quantum    = Hop;                        ///< a hop at a time
            static constexpr bool        is_in_place       = false;                      ///< two port types
            static constexpr bool        has_exact_latency = true;                       ///< every frequency alike
            static constexpr ratio       exact_latency{0, 1}; ///< a frame's first Hop samples, when it arrives

            /// What one instance remembers.
            struct state {
                std::array<value_type, Ch * N> sum;   ///< each channel's overlap-add, its next N samples
                std::array<value_type, Hop>    scale; ///< 2/N over each phase's sum of squared windows
            };
            /// What a call needs: one channel's frame at a time.
            struct workspace {
                std::array<complex_type, bins> frame; ///< the bins, then the samples
            };

            /// An inverse STFT windowing with `window`.
            explicit constexpr basic_istft(const window_type& window) noexcept
                : m_window(window) {}
            /// An inverse STFT windowing with a named window's constant.
            template <window_shape Shape>
            explicit constexpr basic_istft(window::named<Shape> shape) noexcept
                : m_window(shape) {}

            /// The window.
            constexpr const window_type& window() const noexcept { return m_window; }

            /// Silence in the overlap-add, and each phase's divisor from the window, in S.
            void reset(state& st) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point spectra arrive with their profile at C6 (D38, D47)");
                st.sum = {};
                for (std::size_t r = 0; r < Hop; ++r) {
                    value_type squares = value_type{0};
                    for (std::size_t n = r; n < N; n += Hop) {
                        squares += m_window[n] * m_window[n];
                    }
                    st.scale[r] = (value_type{2} / static_cast<value_type>(N)) / squares;
                }
            }

            /// Each frame's Hop samples per channel, interleaved.
            std::size_t run(state& st, workspace& ws, span<const complex_type> in,
                            span<value_type> out) const noexcept {
                static_assert(!format_traits<S>::is_fixed_point || detail::k_never<S>,
                              "catenae: fixed-point spectra arrive with their profile at C6 (D38, D47)");
                const std::size_t frames = in.size() / (Ch * bins);
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(out.size() >= frames * Hop * Ch, check::index_out_of_range);
                }
                for (std::size_t k = 0; k < frames; ++k) {
                    frame(st, ws, in.data() + k * Ch * bins, out.data() + k * Hop * Ch);
                }
                return frames * Hop;
            }

            /// n frames in, n Hop out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n * Hop; }
            /// The frames that write k.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept {
                return (k + Hop - 1) / Hop;
            }
            /// n Hop, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n * Hop; }
            /// None: a frame's first Hop samples are written when it arrives.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// N/Hop - 1: then the overlap-add holds only what zero frames added.
            std::size_t tail_frames() const noexcept { return N / Hop - 1; }
            /// N/Hop - 1, as exact.
            std::size_t practical_tail_frames() const noexcept { return N / Hop - 1; }
            /// None: a frame of zeros transforms to zero.
            float flush_residue() const noexcept { return 0.0f; }

          private:
            /// One frame: per channel its inverse times w into the overlap-add, its first Hop
            /// samples out, divided; then the overlap-add moves on by a hop.
            void frame(state& st, workspace& ws, const complex_type* bins_in, value_type* to) const noexcept {
                constexpr auto      plan = fft_plan<S>::template of<N>();
                complex_type* const f    = ws.frame.data();
                const auto&         w    = m_window.half();
                for (std::size_t c = 0; c < Ch; ++c) {
                    std::copy(bins_in + c * bins, bins_in + (c + 1) * bins, f);
                    const int e = plan.inverse(ws.frame); // 2/N, so 2^-e, is in the divisor
                    detail::check_audit(e == 1, check::invariant);
                    value_type* const a = st.sum.data() + c * N;
                    for (std::size_t n = 0; n < N / 4; ++n) {
                        a[2 * n] += f[n].re * w[2 * n];
                        a[2 * n + 1] += f[n].im * w[2 * n + 1];
                    }
                    for (std::size_t n = N / 4; n < N / 2; ++n) {
                        a[2 * n] += f[n].re * w[N - 2 * n];
                        a[2 * n + 1] += f[n].im * w[N - 2 * n - 1];
                    }
                    for (std::size_t i = 0; i < Hop; ++i) {
                        to[i * Ch + c] = a[i] * st.scale[i];
                    }
                    std::copy(a + Hop, a + N, a);
                    std::fill(a + N - Hop, a + N, value_type{0});
                }
            }

            window_type m_window;
        };

        template <std::size_t N, std::size_t Hop, std::size_t Ch>
        using stft = basic_stft<float, N, Hop, Ch>; ///< in float (D24)
        template <std::size_t N, std::size_t Hop, std::size_t Ch>
        using stft_f64 = basic_stft<double, N, Hop, Ch>; ///< in double
        template <std::size_t N, std::size_t Hop, std::size_t Ch>
        using stft_q31 = basic_stft<q31, N, Hop, Ch>; ///< in Q31: the contract now, the arithmetic at C6
        template <std::size_t N, std::size_t Hop, std::size_t Ch>
        using istft = basic_istft<float, N, Hop, Ch>; ///< in float (D24)
        template <std::size_t N, std::size_t Hop, std::size_t Ch>
        using istft_f64 = basic_istft<double, N, Hop, Ch>; ///< in double
        template <std::size_t N, std::size_t Hop, std::size_t Ch>
        using istft_q31 = basic_istft<q31, N, Hop, Ch>; ///< in Q31: the contract now, the arithmetic at C6

        template <typename S, std::size_t N, std::size_t Hop, std::size_t Ch>
        struct is_stage<basic_stft<S, N, Hop, Ch>> : std::true_type {};
        template <typename S, std::size_t N, std::size_t Hop, std::size_t Ch>
        struct is_stage<basic_istft<S, N, Hop, Ch>> : std::true_type {};
        /// Both view their window's table.
        template <typename S, std::size_t N, std::size_t Hop, std::size_t Ch>
        struct holds_view<basic_stft<S, N, Hop, Ch>> : std::true_type {};
        template <typename S, std::size_t N, std::size_t Hop, std::size_t Ch>
        struct holds_view<basic_istft<S, N, Hop, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_STFT_H
