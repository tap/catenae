/// @file test_stft.cpp
/// @brief The STFT and its inverse (include/catenae/stft.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "analyzer_support.h"
#include "catenae/chain.h"
#include "catenae/fft.h"
#include "catenae/steady.h"
#include "catenae/stft.h"
#include "catenae/window.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T>
    class stft : public ::testing::Test {};
    using floating = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(stft, floating, );

    /// What a stage writes over `in`, in calls of 1 to `largest` frames (all at once for 0),
    /// from storage filled with garbage and reset.
    template <typename Stage>
    std::vector<typename catenae_test::driver<Stage>::out_value>
    run_chunked(const Stage& stage, const std::vector<typename catenae_test::driver<Stage>::value_type>& in,
                std::size_t largest, std::uint32_t seed) {
        using driver_t           = catenae_test::driver<Stage>;
        auto              d      = std::make_unique<driver_t>(stage, static_cast<unsigned char>(0x5A));
        const std::size_t frames = in.size() / driver_t::in_channels;
        std::vector<typename driver_t::out_value> out;
        const std::vector<std::size_t>            sizes =
            largest == 0 ? std::vector<std::size_t>{frames} : catenae_test::chunks(frames, largest, seed);
        std::size_t from = 0;
        for (const std::size_t n : sizes) {
            d->run(in, from, n, out);
            from += n;
        }
        return out;
    }

    /// The forward of each hop's window, written out: frame k's window is samples (k + 1) Hop - N
    /// to (k + 1) Hop - 1, 0 before the first, each times w.
    template <typename T, std::size_t N, std::size_t Hop, std::size_t Ch>
    std::vector<catenae::complex<T>> expected_frames(const std::vector<T>& x, const catenae::window_design<T, N>& w) {
        constexpr std::size_t                 bins   = N / 2 + 1;
        const std::size_t                     frames = x.size() / Ch;
        std::vector<catenae::complex<T>>      out;
        std::array<catenae::complex<T>, bins> f{};
        for (std::size_t end = Hop; end <= frames; end += Hop) {
            for (std::size_t c = 0; c < Ch; ++c) {
                const auto at = [&](std::size_t n) {
                    const auto i = static_cast<std::ptrdiff_t>(end) - static_cast<std::ptrdiff_t>(N)
                                   + static_cast<std::ptrdiff_t>(n);
                    return i < 0 ? T{0} : x[std::size_t(i) * Ch + c];
                };
                for (std::size_t n = 0; n < N / 2; ++n) {
                    f[n] = catenae::complex<T>{at(2 * n) * w[2 * n], at(2 * n + 1) * w[2 * n + 1]};
                }
                f[N / 2] = catenae::complex<T>{T{0}, T{0}};
                catenae::fft_plan<T>::template of<N>().forward(f);
                out.insert(out.end(), f.begin(), f.end());
            }
        }
        return out;
    }

    // Verifies CAT-STFT-001.
    TYPED_TEST(stft, FramesAreTheWindowsForward) {
        using sample             = TypeParam;
        constexpr std::size_t n  = 64;
        constexpr std::size_t hp = 16;
        constexpr std::size_t ch = 2;
        using stage_t            = catenae::basic_stft<sample, n, hp, ch>;
        static_assert(stage_t::rate_ratio == (catenae::ratio{1, hp}) && stage_t::input_group == hp, "a frame a hop");
        static_assert(stage_t::max_shortfall == (catenae::ratio{hp - 1, hp})
                          && stage_t::max_lead == (catenae::ratio{0, 1}),
                      "behind by under a frame, never ahead");
        static_assert(stage_t::exact_latency == (catenae::ratio{3, 1}), "(N - Hop) / Hop of its frames");
        const stage_t stage{catenae::window::hann};
        const auto    in       = analyzer_test::noise<sample>(ch * (hp * 23 + 5), 3);
        const auto    expected = expected_frames<sample, n, hp, ch>(in, stage.window());
        for (const std::size_t largest : {std::size_t{0}, std::size_t{1}, std::size_t{7}, hp, 3 * hp}) {
            const auto got = run_chunked(stage, in, largest, 9);
            ASSERT_EQ(got.size(), expected.size()) << largest;
            EXPECT_EQ(std::memcmp(got.data(), expected.data(), got.size() * sizeof(got[0])), 0) << largest;
        }
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
    }

    /// The overlap-add of each frame's inverse times w, divided as the card says, written out.
    template <typename T, std::size_t N, std::size_t Hop, std::size_t Ch>
    std::vector<T> expected_samples(const std::vector<catenae::complex<T>>& spectra,
                                    const catenae::window_design<T, N>&     w) {
        constexpr std::size_t bins = N / 2 + 1;
        std::array<T, Hop>    scale{};
        for (std::size_t r = 0; r < Hop; ++r) {
            T squares = T{0};
            for (std::size_t n = r; n < N; n += Hop) {
                squares += w[n] * w[n];
            }
            scale[r] = (T{2} / static_cast<T>(N)) / squares;
        }
        const std::size_t                     frames = spectra.size() / (Ch * bins);
        std::vector<T>                        out(frames * Hop * Ch);
        std::vector<T>                        sum(Ch * (frames * Hop + N), T{0});
        std::array<catenae::complex<T>, bins> f{};
        for (std::size_t k = 0; k < frames; ++k) {
            for (std::size_t c = 0; c < Ch; ++c) {
                std::copy(spectra.begin() + std::ptrdiff_t((k * Ch + c) * bins),
                          spectra.begin() + std::ptrdiff_t((k * Ch + c + 1) * bins), f.begin());
                static_cast<void>(catenae::fft_plan<T>::template of<N>().inverse(f));
                T* const a = sum.data() + c * (frames * Hop + N) + k * Hop;
                for (std::size_t n = 0; n < N / 2; ++n) {
                    a[2 * n] += f[n].re * w[2 * n];
                    a[2 * n + 1] += f[n].im * w[2 * n + 1];
                }
                for (std::size_t i = 0; i < Hop; ++i) {
                    out[(k * Hop + i) * Ch + c] = a[i] * scale[i];
                }
            }
        }
        return out;
    }

    // Verifies CAT-STFT-002.
    TYPED_TEST(stft, SamplesAreTheLeastSquaresOverlapAdd) {
        using sample             = TypeParam;
        constexpr std::size_t n  = 64;
        constexpr std::size_t hp = 16;
        constexpr std::size_t ch = 2;
        using stage_t            = catenae::basic_istft<sample, n, hp, ch>;
        static_assert(stage_t::rate_ratio == (catenae::ratio{hp, 1}) && stage_t::input_group == 1
                          && stage_t::output_quantum == hp,
                      "a hop of samples a frame");
        const stage_t stage{catenae::window::hann};
        // Spectra of any values: the least-squares signal of a modified spectrum.
        const auto                            spectra = analyzer_test::noise<sample>(2 * ch * (n / 2 + 1) * 19, 5);
        std::vector<catenae::complex<sample>> in(spectra.size() / 2);
        std::memcpy(in.data(), spectra.data(), spectra.size() * sizeof(sample));
        const auto expected = expected_samples<sample, n, hp, ch>(in, stage.window());
        for (const std::size_t largest : {std::size_t{0}, std::size_t{1}, std::size_t{4}}) {
            const auto got = run_chunked(stage, in, largest, 11);
            ASSERT_EQ(got.size(), expected.size()) << largest;
            EXPECT_EQ(std::memcmp(got.data(), expected.data(), got.size() * sizeof(got[0])), 0) << largest;
        }
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
    }

    /// The largest |y[m] - x[m - delay]| of a pair over noise, past its first frame of output.
    template <typename T, std::size_t N, std::size_t Hop>
    double reconstruction_error(const catenae::window_design<T, N>& w) {
        const auto        pair  = catenae::basic_stft<T, N, Hop, 1>{w} | catenae::basic_istft<T, N, Hop, 1>{w};
        const std::size_t delay = N - Hop;
        const auto        in    = analyzer_test::noise<T>(Hop * 40, 17);
        const auto        out   = run_chunked(pair, in, 0, 0);
        double            worst = 0.0;
        for (std::size_t m = delay; m < out.size(); ++m) {
            worst = std::fmax(worst, std::fabs(double(out[m]) - double(in[m - delay])));
        }
        for (std::size_t m = 0; m < delay; ++m) { // silence before the first sample arrives
            worst = std::fmax(worst, std::fabs(double(out[m])));
        }
        return worst;
    }

    template <typename T, std::size_t N, std::size_t Hop>
    void expect_reconstructs(double floor) {
        using d = catenae::window_design<T, N>;
        EXPECT_LE((reconstruction_error<T, N, Hop>(d(catenae::window::hann))), floor) << "hann " << N << "/" << Hop;
        EXPECT_LE((reconstruction_error<T, N, Hop>(d(catenae::window::sqrt_hann))), floor) << "sqrt-hann";
        EXPECT_LE((reconstruction_error<T, N, Hop>(d(catenae::window::hamming))), floor) << "hamming";
        EXPECT_LE((reconstruction_error<T, N, Hop>(d(catenae::window::blackman))), floor) << "blackman";
        EXPECT_LE((reconstruction_error<T, N, Hop>(d(catenae::window::blackman_harris))), floor) << "blackman-harris";
        std::vector<T> table(N / 2 + 1);
        const auto k = d::make(catenae::window::kaiser{8.6}, catenae::span<T, N / 2 + 1>(table.data(), table.size()));
        ASSERT_TRUE(k.has_value());
        EXPECT_LE((reconstruction_error<T, N, Hop>(*k)), floor) << "kaiser";
    }

    // Verifies CAT-STFT-003.
    TYPED_TEST(stft, PairReproducesItsInput) {
        using sample       = TypeParam;
        const double bound = std::is_same<sample, float>::value ? 6e-7 : 1.2e-15; // the card's, with margin
        expect_reconstructs<sample, 16, 8>(bound);
        expect_reconstructs<sample, 512, 256>(bound);
        expect_reconstructs<sample, 512, 128>(bound);
        expect_reconstructs<sample, 512, 64>(bound);
        // 9.5's region without its gate: 1:1, in groups of the hop, N - Hop frames late; in
        // steady<> frame-synchronous, Hop - 1 frames later still.
        constexpr auto pair = catenae::basic_stft<sample, 512, 128, 1>{catenae::window::hann}
                              | catenae::basic_istft<sample, 512, 128, 1>{catenae::window::hann};
        using pair_t = std::remove_cv_t<decltype(pair)>;
        static_assert(pair_t::rate_ratio == (catenae::ratio{1, 1}) && pair_t::input_group == 128
                          && !pair_t::is_frame_synchronous,
                      "1/128 then 128/1, exact in whole hops");
        static_assert(pair_t::exact_latency == (catenae::ratio{384, 1}), "N - Hop");
        const auto steady_pair = catenae::chain(catenae::steady<pair_t>{pair});
        using steady_t         = std::remove_cv_t<decltype(steady_pair)>;
        static_assert(steady_t::is_frame_synchronous && steady_t::exact_latency == (catenae::ratio{511, 1}),
                      "127 frames later, and it says so");
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(pair));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(steady_pair));
        // The tails: after the chain's exact tail of zeros, silence exactly. It composes the
        // stft's N - 1 with the frames that bring the istft's N/Hop - 1 zero frames, at most a
        // hop's 127 more than their 3 x 128.
        const std::size_t tail = pair.tail_frames();
        EXPECT_EQ(tail, 511u + 3u * 128u + 127u);
        std::vector<sample> in = analyzer_test::noise<sample>(128 * 6, 23);
        in.resize(in.size() + (tail + 127) / 128 * 128 + 128 * 2, sample{0});
        const auto        out   = run_chunked(pair, in, 0, 0);
        const std::size_t quiet = 128 * 6 + tail;
        for (std::size_t m = quiet; m < out.size(); ++m) {
            ASSERT_EQ(out[m], sample{0}) << m;
        }
    }

} // namespace
