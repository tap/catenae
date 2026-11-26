/// @file resampler.h
/// @brief The async skeleton's fractional resampler: a creeping fractional delay over a bank,
/// driven by the servo's estimate, as a configuration and a state (CF).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Ratio     | each output frame advances the read position 1 + eps_hat input frames            |
/// | Phase     | an unsigned Q0.64 fraction; a slip is a 64-bit wrap: 2 frames in, or 0          |
/// | Per frame | the two rows about the phase blended once, then one dot product a channel        |
/// | State     | `resampler_state`: the history, the frames popped and not yet read, the phase;  |
/// |           | plain storage, all zero at rest (not primed)                                     |
/// | Numbers   | `float` throughout the frame loop, accumulating in `float` (D36); the estimate   |
/// |           | becomes Q0.64 once a call                                                         |
///
/// Carried from SampleRateTap 4496888, async/include/tap/sr/async/polyphase_filter.h:
/// `fractional_resampler` (`reset`, `mu`, `buffered_frames`, `prime`, `process`, `append_one`,
/// the slip logic unchanged) and `blend_row_phase`, with `sample_traits<float>`'s blend
/// (async/include/tap/sr/async/sample_traits.h). Changed: the history is one frame-major line
/// (SampleRateTap's layout at high channel counts) for every count; the dot product accumulates
/// in `float`, not `double`; the vectors are arrays in the state, sized by template arguments.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_ASYNC_RESAMPLER_H
#define CATENAE_FIT_ASYNC_RESAMPLER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace catenae_fit {
    namespace async {

        /// What the resampler remembers: plain storage, all zero at rest.
        template <std::size_t Ch, std::size_t Taps, std::size_t Chunk>
        struct resampler_state {
            std::array<float, (Taps + Chunk) * Ch> history;       ///< frame-major, oldest first
            std::array<float, Chunk * Ch>          staged;        ///< popped from the ring, not yet read
            std::uint64_t                          phase;         ///< the fractional position, Q0.64
            std::uint32_t                          end;           ///< frames in the history
            std::uint32_t                          staged_frames; ///< frames in `staged`
            std::uint32_t                          staged_read;   ///< of which this many are read
            std::uint32_t                          primed;        ///< 1 once the history is full
        };

        /// @brief The fractional resampler over `Bank` for `Ch` channels, popping its input in
        /// chunks of `Chunk` frames: a configuration with no members.
        template <std::size_t Ch, typename Bank, std::size_t Chunk>
        class resampler {
          public:
            static constexpr std::size_t taps = Bank::taps; ///< frames in the window
            /// What one instance remembers.
            using state = resampler_state<Ch, Bank::taps, Chunk>;

            /// Clears the history and the phase. Frames popped and not read are dropped.
            static void reset(state& st) noexcept {
                st.phase         = 0;
                st.end           = 0;
                st.primed        = 0;
                st.staged_frames = 0;
                st.staged_read   = 0;
            }

            /// The fractional position in [0, 1), for the servo once a pull.
            static double mu(const state& st) noexcept { return static_cast<double>(st.phase) * 0x1p-64; }
            /// Frames popped from the ring and not yet read into the history: part of the backlog.
            static std::size_t buffered_frames(const state& st) noexcept { return st.staged_frames - st.staged_read; }

            /// Fills the window with `taps` frames from `pop`; false, and not primed, if it ran dry.
            template <typename Pop>
            static bool prime(state& st, Pop&& pop) noexcept {
                for (std::size_t i = 0; i < taps; ++i) {
                    if (!append_one(st, pop)) {
                        return false;
                    }
                }
                st.primed = 1;
                return true;
            }

            /// Up to `frames` frames into `out` (interleaved), the read position advancing
            /// 1 + `eps_hat` input frames each. Returns the frames made; fewer means `pop` ran dry.
            template <typename Pop>
            static std::size_t process(state& st, float* out, std::size_t frames, double eps_hat, Pop&& pop) noexcept {
                // eps in Q0.64, converted once a call. |eps| is clamped by the servo to ~1.5e-3.
                const auto eps_fix = static_cast<std::int64_t>(eps_hat * 0x1p64);
                const auto eps_u   = static_cast<std::uint64_t>(eps_fix);
                for (std::size_t n = 0; n < frames; ++n) {
                    const std::uint64_t m       = st.phase + eps_u; // mod 2^64
                    std::size_t         advance = 1;
                    if (eps_fix >= 0) {
                        if (m < st.phase) { // wrapped past 1.0: a forward slip, one more frame in
                            advance = 2;
                        }
                    }
                    else if (m > st.phase) { // wrapped below 0.0: a backward slip, the same window
                        advance = 0;
                    }
                    for (std::size_t a = 0; a < advance; ++a) {
                        if (!append_one(st, pop)) {
                            return n; // dry: the phase is not advanced for this frame
                        }
                    }
                    st.phase = m;
                    write_frame(st, m, out + n * Ch);
                }
                return frames;
            }

          private:
            /// One output frame at phase `m`: the rows about it blended, then a dot a channel.
            static void write_frame(const state& st, std::uint64_t m, float* frame) noexcept {
                const std::size_t row = static_cast<std::size_t>(m >> (64u - Bank::phase_bits));
                const float       blend =
                    static_cast<float>(static_cast<std::uint32_t>((m << Bank::phase_bits) >> 40u)) * 0x1p-24f;
                const float* c0 = Bank::table.data() + row * taps;
                const float* c1 = c0 + taps;
                // CF finding: a junction has no workspace (D50 gives a stage one), so the row of
                // blended coefficients, the call's only scratch, is on the stack: 64 bytes here, 320
                // at SampleRateTap's transparent profile (80 taps).
                std::array<float, taps> coeffs{};
                for (std::size_t t = 0; t < taps; ++t) {
                    coeffs[t] = c0[t] + blend * (c1[t] - c0[t]);
                }
                const float* window = st.history.data() + (st.end - taps) * Ch;
                for (std::size_t c = 0; c < Ch; ++c) {
                    float acc = 0.0f;
                    for (std::size_t t = 0; t < taps; ++t) {
                        acc += coeffs[t] * window[t * Ch + c];
                    }
                    frame[c] = acc;
                }
            }

            /// One frame from the staged chunk into the history, popping a chunk when it is empty.
            template <typename Pop>
            static bool append_one(state& st, Pop& pop) noexcept {
                if (st.staged_read == st.staged_frames) {
                    st.staged_frames = static_cast<std::uint32_t>(pop(st.staged.data(), Chunk));
                    st.staged_read   = 0;
                    if (st.staged_frames == 0) {
                        return false;
                    }
                }
                if (st.end == taps + Chunk) { // compact: the newest taps - 1 frames to the front
                    constexpr std::size_t keep = taps - 1;
                    std::memmove(st.history.data(), st.history.data() + (taps + Chunk - keep) * Ch,
                                 keep * Ch * sizeof(float));
                    st.end = static_cast<std::uint32_t>(keep);
                }
                std::memcpy(st.history.data() + std::size_t{st.end} * Ch,
                            st.staged.data() + std::size_t{st.staged_read} * Ch, Ch * sizeof(float));
                ++st.end;
                ++st.staged_read;
                return true;
            }
        };

    } // namespace async
} // namespace catenae_fit

#endif // CATENAE_FIT_ASYNC_RESAMPLER_H
