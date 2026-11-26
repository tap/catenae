/// @file test_fir.cpp
/// @brief fir's contract (include/catenae/fir.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/detail/fir_kernel.h"
#include "catenae/detail/shift.h"
#include "catenae/fir.h"
#include "catenae/gain.h"
#include "catenae/parallel.h"
#include "catenae_test/harness.h"

namespace {

    template <typename T>
    class fir_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double, catenae::q15, catenae::q31, catenae::q4_27>;
    TYPED_TEST_SUITE(fir_formats, formats, );

    using catenae::fir_bandpass;
    using catenae::fir_highpass;
    using catenae::fir_lowpass;

    /// A design by whichever make() its size takes: a longer one into a table of its own, which
    /// the next design of the same type and spec reuses.
    template <typename S, std::size_t Taps, catenae::fir_phase Phase = catenae::fir_phase::linear, typename Spec>
    catenae::fir_design<S, Taps, Phase> design(const Spec& spec) {
        using d = catenae::fir_design<S, Taps, Phase>;
        if constexpr (d::holds_by_value) {
            const auto made = d::make(spec);
            EXPECT_TRUE(made.has_value());
            return *made;
        }
        else {
            static std::array<typename d::coefficient_type, Taps> table{};
            static std::array<double, d::workspace_size>          workspace{};
            const auto made = d::make(spec, catenae::span<typename d::coefficient_type, Taps>(table),
                                      catenae::span<double, d::workspace_size>(workspace));
            EXPECT_TRUE(made.has_value());
            return *made;
        }
    }

    template <typename S>
    std::vector<catenae::storage_t<S>> noise(std::size_t values, std::uint32_t seed) {
        catenae_test::signal               s(seed);
        std::vector<catenae::storage_t<S>> v(values);
        for (auto& x : v) {
            x = s.next<catenae::storage_t<S>>();
        }
        return v;
    }

    /// A stage run over `in` from reset, in one call.
    template <typename Stage, typename V>
    std::vector<V> run_all(const Stage& stage, const std::vector<V>& in) {
        static typename Stage::state st;
        st = typename Stage::state{};
        catenae::no_workspace ws{};
        std::vector<V>        out(in.size());
        static_cast<void>(stage.run(st, ws, catenae::span<const V>(in), catenae::span<V>(out)));
        return out;
    }

    template <typename S, std::size_t Taps, std::size_t Ch>
    void each_channel_by_the_kernel(const catenae::fir_design<S, Taps>& d) {
        using v                  = catenae::storage_t<S>;
        constexpr std::size_t frames = 400;
        const catenae::basic_fir<S, Ch, Taps> stage(d);
        const std::vector<v>                  in  = noise<S>(frames * Ch, 5u + Taps);
        const std::vector<v>                  out = run_all(stage, in);
        for (std::size_t c = 0; c < Ch; ++c) {
            std::vector<v> window(Taps, v{0});
            for (std::size_t f = 0; f < frames; ++f) {
                std::rotate(window.begin(), window.begin() + 1, window.end());
                window[Taps - 1] = in[f * Ch + c];
                const v want     = catenae::detail::fir_dot<v, Taps>(d.data(), window.data());
                ASSERT_EQ(std::memcmp(&want, &out[f * Ch + c], sizeof(v)), 0)
                    << Taps << " taps, channel " << c << " frame " << f;
            }
        }
        // In place, the same bits.
        static typename catenae::basic_fir<S, Ch, Taps>::state again;
        again                 = {};
        catenae::no_workspace ws{};
        std::vector<v>        buffer = in;
        static_cast<void>(stage.run(again, ws, catenae::span<const v>(buffer), catenae::span<v>(buffer)));
        EXPECT_EQ(std::memcmp(buffer.data(), out.data(), out.size() * sizeof(v)), 0);
    }

    // Verifies CAT-FIR-001.
    TYPED_TEST(fir_formats, EachChannelIsItsTapsByTheKernel) {
        each_channel_by_the_kernel<TypeParam, 63, 3>(design<TypeParam, 63>(fir_lowpass{0.1, 60.0}));
        each_channel_by_the_kernel<TypeParam, 1, 2>(design<TypeParam, 1>(fir_lowpass{0.1, 60.0}));
        each_channel_by_the_kernel<TypeParam, 300, 2>(design<TypeParam, 300>(fir_bandpass{0.05, 0.2, 80.0}));
        // The generic tests: reset over any storage, the counts, the arrival numbers, any chunking.
        const catenae::basic_fir<TypeParam, 2, 15>  short_one(design<TypeParam, 15>(fir_highpass{0.3, 40.0}));
        const catenae::basic_fir<TypeParam, 1, 300> long_one(design<TypeParam, 300>(fir_lowpass{0.05, 80.0}));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(short_one));
        EXPECT_TRUE(catenae_test::counts_are_exact(short_one));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(short_one, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(short_one));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(long_one));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(long_one));
    }

    /// Whether two floats are one value, without ==.
    constexpr bool same(float a, float b) { return !(a < b) && !(a > b); }

    // Verifies CAT-FIR-002.
    TYPED_TEST(fir_formats, ItsLatencyIsExactAndOfItsType) {
        using odd  = catenae::stage_traits<catenae::basic_fir<TypeParam, 2, 63>>;
        using even = catenae::stage_traits<catenae::basic_fir<TypeParam, 2, 64>>;
        using any  = catenae::stage_traits<catenae::basic_fir_any<TypeParam, 2, 63>>;
        static_assert(odd::has_exact_latency && odd::exact_latency.up == 31 && odd::exact_latency.down == 1, "31");
        static_assert(even::has_exact_latency && even::exact_latency.up == 63 && even::exact_latency.down == 2,
                      "half a frame at an even count");
        static_assert(!any::has_exact_latency, "any FIR: its design's");
        static_assert(odd::rests_at_zero && odd::is_in_place && odd::is_frame_synchronous, "the contract");
        const catenae::basic_fir<TypeParam, 1, 63> stage(design<TypeParam, 63>(fir_lowpass{0.1, 60.0}));
        EXPECT_TRUE(same(stage.latency().nominal, 31.0f) && same(stage.latency().deviation, 0.0f));
        const catenae::basic_fir_any<TypeParam, 1, 63> other(
            design<TypeParam, 63, catenae::fir_phase::any>(fir_lowpass{0.1, 60.0}));
        EXPECT_TRUE(same(other.latency().nominal, 31.0f) && same(other.latency().deviation, 0.0f));
        // A low-pass's impulse response peaks at its latency.
        using v = catenae::storage_t<TypeParam>;
        std::vector<v> impulse(100, v{0});
        impulse[0] = std::is_floating_point<v>::value ? v(1) : v(16384);
        const std::vector<v> h = run_all(stage, impulse);
        EXPECT_EQ(std::max_element(h.begin(), h.end()) - h.begin(), 31);
    }

    // Verifies CAT-FIR-002.
    TEST(fir, ParallelAlignsItByType) {
        // A dry arm beside a 63-tap FIR is delayed by its 31 frames, so the two meet in time.
        using arms = catenae::parallel<catenae::fir<1, 63>, catenae::gain<1>, catenae::align::by_type>;
        static_assert(catenae::stage_traits<arms>::opted_in, "aligned by type");
        static_assert(catenae::stage_traits<arms>::exact_latency.up == 31, "the larger");
        const arms        both(catenae::fir<1, 63>(design<float, 63>(fir_lowpass{0.1, 60.0})), catenae::gain<1>{1.0f, 0});
        std::vector<float> in(160, 0.0f); // the FIR's channel, then the dry arm's
        in[0]                       = 1.0f;
        in[1]                       = 1.0f;
        const std::vector<float> in2 = in;
        static arms::state       st;
        st = arms::state{};
        both.reset(st);
        arms::workspace    ws{};
        std::vector<float> out(160);
        ASSERT_EQ(both.run(st, ws, catenae::span<const float>(in2), catenae::span<float>(out)), 80u);
        // The dry arm, channel 1, delivers its impulse at frame 31, where the FIR's peaks.
        EXPECT_EQ(out[31 * 2 + 1], 1.0f);
        float peak = 0.0f;
        std::size_t at = 0;
        for (std::size_t f = 0; f < 80; ++f) {
            if (out[f * 2] > peak) {
                peak = out[f * 2];
                at   = f;
            }
        }
        EXPECT_EQ(at, 31u);
        // A chain sums it: a delay of 4 after the FIR is 35 frames.
        const auto strip = catenae::chain(catenae::fir<1, 63>(design<float, 63>(fir_lowpass{0.1, 60.0})))
                           | catenae::delay<1, 4>{};
        using strip_type = std::remove_const_t<decltype(strip)>;
        static_assert(strip_type::exact_latency.up == 35 && strip_type::exact_latency.down == 1, "31 + 4");
    }

    template <typename S, std::size_t Taps>
    void silence_after_the_tail(const catenae::fir_design<S, Taps>& d) {
        using v                                = catenae::storage_t<S>;
        const catenae::basic_fir<S, 2, Taps> stage(d);
        ASSERT_EQ(stage.tail_frames(), Taps - 1);
        ASSERT_EQ(stage.practical_tail_frames(), Taps - 1);
        ASSERT_TRUE(same(stage.flush_residue(), 0.0f));
        std::vector<v> in = noise<S>(2 * (50 + Taps + 20), 3);
        std::fill(in.begin() + 2 * 50, in.end(), v{0});
        const std::vector<v> out = run_all(stage, in);
        // The last tail frame can still carry the last input; every frame after is zero.
        bool any_in_tail = false;
        for (std::size_t f = 50; f < 50 + Taps - 1; ++f) {
            any_in_tail = any_in_tail || out[2 * f] < v{0} || out[2 * f] > v{0} || out[2 * f + 1] < v{0}
                          || out[2 * f + 1] > v{0};
        }
        EXPECT_TRUE(any_in_tail || Taps == 1);
        for (std::size_t f = 50 + Taps - 1; f < 50 + Taps + 20; ++f) {
            ASSERT_EQ(out[2 * f], v{0}) << Taps << " taps, frame " << f;
            ASSERT_EQ(out[2 * f + 1], v{0}) << Taps << " taps, frame " << f;
        }
    }

    // Verifies CAT-FIR-003.
    TYPED_TEST(fir_formats, AfterItsTailTheOutputIsSilence) {
        silence_after_the_tail<TypeParam, 1>(design<TypeParam, 1>(fir_lowpass{0.1, 60.0}));
        silence_after_the_tail<TypeParam, 2>(design<TypeParam, 2>(fir_lowpass{0.1, 60.0}));
        silence_after_the_tail<TypeParam, 63>(design<TypeParam, 63>(fir_highpass{0.2, 60.0}));
        silence_after_the_tail<TypeParam, 300>(design<TypeParam, 300>(fir_lowpass{0.05, 80.0}));
    }

    // Verifies CAT-FIR-004.
    TYPED_TEST(fir_formats, ANewDesignAppliesToTheHistoryAsItStands) {
        using v                           = catenae::storage_t<TypeParam>;
        constexpr std::size_t frames      = 200;
        const auto            first       = design<TypeParam, 31>(fir_lowpass{0.05, 60.0});
        const auto            second      = design<TypeParam, 31>(fir_highpass{0.3, 60.0});
        catenae::basic_fir<TypeParam, 2, 31> stage(first);
        typename catenae::basic_fir<TypeParam, 2, 31>::state st{};
        catenae::no_workspace                                ws{};
        const std::vector<v>                                 in = noise<TypeParam>(frames * 2, 9);
        std::vector<v>                                       out(in.size());
        static_cast<void>(
            stage.run(st, ws, catenae::span<const v>(in.data(), frames), catenae::span<v>(out.data(), frames)));
        auto held = st; // the history the new design inherits
        stage.set_design(second);
        static_cast<void>(stage.run(st, ws, catenae::span<const v>(in.data() + frames, frames),
                                    catenae::span<v>(out.data() + frames, frames)));
        const catenae::basic_fir<TypeParam, 2, 31> fresh(second);
        std::vector<v>                             want(frames);
        static_cast<void>(
            fresh.run(held, ws, catenae::span<const v>(in.data() + frames, frames), catenae::span<v>(want)));
        EXPECT_EQ(std::memcmp(want.data(), out.data() + frames, frames * sizeof(v)), 0);
        EXPECT_EQ(std::memcmp(&stage.design(), &second, sizeof(second)), 0);
    }

    /// The largest |float stage - double stage| over the same float input, the double stage the
    /// double design: the float profile's floor, in full scale.
    template <std::size_t Taps, typename Spec>
    double float_floor(const Spec& spec, double level) {
        constexpr std::size_t frames = 4000;
        catenae_test::signal  s(99);
        std::vector<float>    x(frames);
        for (float& v : x) {
            v = static_cast<float>(level * static_cast<double>(s.next<float>()));
        }
        const std::vector<double> xd(x.begin(), x.end());
        const auto y  = run_all(catenae::basic_fir<float, 1, Taps>(design<float, Taps>(spec)), x);
        const auto yd = run_all(catenae::basic_fir<double, 1, Taps>(design<double, Taps>(spec)), xd);
        double     worst = 0.0;
        for (std::size_t i = 0; i < frames; ++i) {
            worst = std::max(worst, std::fabs(static_cast<double>(y[i]) - yd[i]));
        }
        return worst;
    }

    // Verifies CAT-FIR-005.
    TEST(fir, FloatFollowsTheDoubleFilterWithinItsFloor) {
        // Measured on the hosted legs at contraction off (the floats are IEEE's on every leg):
        // 6.4e-8 (-143.9 dBFS) at 63 taps on noise at -6 dBFS, 1.1e-7 (-139.0) at -0.1 dBFS,
        // 7.7e-8 (-142.2) at 255 taps.
        EXPECT_LT(float_floor<63>(fir_lowpass{0.1, 60.0}, 0.5), 8e-8);
        EXPECT_LT(float_floor<63>(fir_lowpass{0.1, 60.0}, 0.99), 1.4e-7);
        EXPECT_LT(float_floor<255>(fir_lowpass{0.05, 80.0}, 0.5), 1e-7);
    }

    /// The largest distance, in output LSBs, of a fixed stage from the exact sum of its own
    /// taps over its own input, and from the double design over that input.
    struct fixed_floor {
        double own;    // the arithmetic's
        double design; // the arithmetic's and the taps' rounding together
    };

    template <typename S, std::size_t Taps, typename Spec>
    fixed_floor fixed_floor_of(const Spec& spec) {
        using v                     = catenae::storage_t<S>;
        using traits                = catenae::sample_traits<v>;
        constexpr std::size_t frames = 3000;
        constexpr double      lsb    = std::is_same<v, std::int16_t>::value ? 32768.0 : 2147483648.0;
        catenae_test::signal  s(99);
        std::vector<v>        x(frames);
        for (v& q : x) {
            q = static_cast<v>(std::lround(0.5 * static_cast<double>(s.next<float>()) * lsb));
        }
        const auto d  = design<S, Taps>(spec);
        const auto y  = run_all(catenae::basic_fir<S, 1, Taps>(d), x);
        const auto dd = design<double, Taps>(spec);
        fixed_floor   f{0.0, 0.0};
        for (std::size_t i = Taps; i < frames; ++i) {
            // The exact sum of the stage's own taps: Q29 in Q15; in Q31, Q61 split at the
            // pre-shift into what the kernel keeps and what it drops.
            std::int64_t kept    = 0;
            std::int64_t dropped = 0;
            double       ideal   = 0.0;
            for (std::size_t k = 0; k < Taps; ++k) {
                const std::int64_t p = static_cast<std::int64_t>(x[i - k]) * d.table()[k];
                kept += catenae::detail::shift_right(p, traits::accum_pre_shift);
                dropped += p - catenae::detail::shift_left(catenae::detail::shift_right(p, traits::accum_pre_shift),
                                                           traits::accum_pre_shift);
                ideal += dd.table()[k] * (static_cast<double>(x[i - k]) / lsb);
            }
            const double exact =
                (static_cast<double>(kept) + static_cast<double>(dropped) / std::ldexp(1.0, traits::accum_pre_shift))
                / std::ldexp(1.0, traits::finalize_shift);
            f.own    = std::max(f.own, std::fabs(static_cast<double>(y[i]) - exact));
            f.design = std::max(f.design, std::fabs(static_cast<double>(y[i]) - ideal * lsb));
        }
        return f;
    }

    // Verifies CAT-FIR-005.
    TEST(fir, FixedFormatsFollowTheDoubleFilterWithinTheirFloors) {
        // The arithmetic: one rounding, so half an LSB of the exact sum of the stage's own taps
        // in Q15; in Q31 the pre-shift's truncation adds less than Taps 2^-14 LSB. The taps'
        // rounding (Q1.14, Q1.30) takes the rest: measured 5.1 LSB from the double design at
        // 63 taps and 11.9 (Q15) and 13.3 (Q31) at 255, on noise at -6 dBFS.
        const fixed_floor q15_63  = fixed_floor_of<catenae::q15, 63>(fir_lowpass{0.1, 60.0});
        const fixed_floor q31_63  = fixed_floor_of<catenae::q31, 63>(fir_lowpass{0.1, 60.0});
        const fixed_floor q15_255 = fixed_floor_of<catenae::q15, 255>(fir_lowpass{0.05, 80.0});
        const fixed_floor q31_255 = fixed_floor_of<catenae::q31, 255>(fir_lowpass{0.05, 80.0});
        EXPECT_LE(q15_63.own, 0.5);
        EXPECT_LE(q15_255.own, 0.5);
        EXPECT_LE(q31_63.own, 0.5 + 63.0 / 16384.0);
        EXPECT_LE(q31_255.own, 0.5 + 255.0 / 16384.0);
        EXPECT_LT(q15_63.design, 6.0);
        EXPECT_LT(q31_63.design, 6.0);
        EXPECT_LT(q15_255.design, 13.0);
        EXPECT_LT(q31_255.design, 15.0);
    }

    // Verifies CAT-FIR-005.
    TEST(fir, Q4_27IsQ31sArithmetic) {
        // The kernel does not know the binary point: the same raw words through the same taps.
        const auto        d31 = design<catenae::q31, 63>(fir_lowpass{0.1, 60.0});
        const auto        d27 = design<catenae::q4_27, 63>(fir_lowpass{0.1, 60.0});
        const std::vector<std::int32_t> in = noise<catenae::q31>(2 * 500, 4);
        const auto y31 = run_all(catenae::fir_q31<2, 63>(d31), in);
        const auto y27 = run_all(catenae::fir_q4_27<2, 63>(d27), in);
        EXPECT_EQ(std::memcmp(d31.data(), d27.data(), 63 * sizeof(std::int32_t)), 0);
        EXPECT_EQ(y31, y27);
    }

    // Verifies CAT-FIR-005.
    TEST(fir, AFixedOutputSaturates) {
        // Three taps of 1.0 (Q1.14 16384): sums of up to three full-scale samples, held at the
        // rails, never wrapped; 32767 + 32767 - 32768 is in range and passes.
        constexpr std::array<std::int16_t, 3> k_ones = {16384, 16384, 16384};
        const catenae::fir_q15<1, 3>          stage(catenae::fir_design<catenae::q15, 3>::from_table(k_ones));
        const std::vector<std::int16_t>       in  = {32767, 32767, 32767, -32768, -32768, -32768};
        const std::vector<std::int16_t>       out = run_all(stage, in);
        EXPECT_EQ(out, (std::vector<std::int16_t>{32767, 32767, 32767, 32766, -32768, -32768}));
    }

} // namespace
