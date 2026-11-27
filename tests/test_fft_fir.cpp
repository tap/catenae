/// @file test_fft_fir.cpp
/// @brief fft_fir's contract (include/catenae/fft_fir.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/fft.h"
#include "catenae/fft_fir.h"
#include "catenae/fir.h"
#include "catenae/steady.h"
#include "catenae_test/harness.h"
#include "fft_fir_tables.h"

namespace {

    template <typename T>
    class fft_fir_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(fft_fir_formats, formats, );

    using catenae::fir_lowpass;
    using catenae::fir_phase;

    /// An FIR design by whichever make() its size takes. A longer one is written into a table
    /// of its type's, which the next of that type reuses: the spectrum is made from it at once.
    template <typename S, std::size_t Taps, fir_phase Phase = fir_phase::linear>
    catenae::fir_design<S, Taps, Phase> taps_of(const fir_lowpass& spec) {
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

    /// A spectrum, the storage it is made into, and the low-pass's taps it was made from.
    template <typename S, std::size_t Taps, std::size_t Block, fir_phase Phase = fir_phase::linear>
    struct made {
        using design_type = catenae::fft_fir_design<S, Taps, Block, Phase>;
        using taps_type   = catenae::fir_design<S, Taps, Phase>;
        using bin         = typename design_type::bin_type;

        std::vector<bin>                      table = std::vector<bin>(design_type::bins);
        std::vector<catenae::complex<double>> work = std::vector<catenae::complex<double>>(design_type::workspace_size);
        std::vector<double>                   taps;
        design_type                           design;

        explicit made(const fir_lowpass& spec)
            : made(taps_of<S, Taps, Phase>(spec)) {}

        explicit made(const taps_type& h)
            : taps(h.table().begin(), h.table().end())
            , design(*design_type::make(h, catenae::span<bin, design_type::bins>(table.data(), design_type::bins),
                                        catenae::span<catenae::complex<double>, design_type::workspace_size>(
                                            work.data(), design_type::workspace_size))) {}
    };

    template <typename V>
    std::vector<V> noise(std::size_t values, std::uint32_t seed) {
        catenae_test::signal s(seed);
        std::vector<V>       v(values);
        for (auto& x : v) {
            x = s.next<V>();
        }
        return v;
    }

    /// A stage run from reset over `in`, in calls of `call` frames; what it wrote.
    template <typename Stage, typename V>
    std::vector<V> run_in(const Stage& stage, const std::vector<V>& in, std::size_t call) {
        constexpr std::size_t ch = Stage::input_port::values_per_frame;
        auto                  st = std::make_unique<typename Stage::state>();
        auto                  ws = std::make_unique<typename Stage::workspace>();
        std::vector<V>        out;
        const std::size_t     frames = in.size() / ch;
        for (std::size_t at = 0; at < frames; at += call) {
            const std::size_t n = std::min(call, frames - at);
            std::vector<V>    part(Stage::max_outputs_for(n) * ch);
            const std::size_t w =
                stage.run(*st, *ws, catenae::span<const V>(in.data() + at * ch, n * ch), catenae::span<V>(part));
            EXPECT_EQ(w % catenae::stage_traits<Stage>::output_quantum, 0u); // whole blocks
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(w * ch));
        }
        return out;
    }

    /// The card's method in the test: each channel's windows of 2 Block, through the forward,
    /// times the spectrum ((a + ib)(c + id) in the card's order), through the inverse, keeping
    /// the last Block samples.
    template <typename D, typename V>
    std::vector<V> by_windows(const D& d, const std::vector<V>& in, std::size_t ch) {
        constexpr std::size_t b      = D::block;
        const std::size_t     blocks = in.size() / ch / b;
        std::vector<V>        out(blocks * b * ch);
        constexpr auto        plan = catenae::fft_plan<V>::template of<2 * b>();
        for (std::size_t c = 0; c < ch; ++c) {
            for (std::size_t j = 0; j < blocks; ++j) {
                std::array<catenae::complex<V>, b + 1> f{};
                for (std::size_t i = 0; i < 2 * b; ++i) {
                    const V x                                = j == 0 && i < b ? V{0} : in[((j * b + i) - b) * ch + c];
                    (i % 2 == 0 ? f[i / 2].re : f[i / 2].im) = x;
                }
                plan.forward(f);
                for (std::size_t k = 0; k <= b; ++k) {
                    const V p = f[k].re;
                    const V q = f[k].im;
                    const V r = d.table()[k].re;
                    const V s = d.table()[k].im;
                    f[k]      = catenae::complex<V>{p * r - q * s, p * s + q * r};
                }
                static_cast<void>(plan.inverse(f));
                for (std::size_t i = 0; i < b; ++i) {
                    const std::size_t at      = b + i;
                    out[(j * b + i) * ch + c] = at % 2 == 0 ? f[at / 2].re : f[at / 2].im;
                }
            }
        }
        return out;
    }

    /// The direct convolution of each channel by `taps`, in long double.
    template <typename V>
    std::vector<long double> direct(const std::vector<double>& taps, const std::vector<V>& in, std::size_t ch) {
        const std::size_t        frames = in.size() / ch;
        std::vector<long double> out(in.size(), 0.0L);
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t c = 0; c < ch; ++c) {
                long double y = 0.0L;
                for (std::size_t k = 0; k < taps.size() && k <= f; ++k) {
                    y += static_cast<long double>(taps[k]) * static_cast<long double>(in[(f - k) * ch + c]);
                }
                out[f * ch + c] = y;
            }
        }
        return out;
    }

    /// Whether x is zero, of either sign, without ==.
    template <typename V>
    bool is_zero(V x) {
        return !(x < V{0}) && !(x > V{0});
    }

    template <typename V>
    bool same_bits(const std::vector<V>& a, const std::vector<V>& b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(V)) == 0;
    }

    // Verifies CAT-FFT-FIR-001.
    TYPED_TEST(fft_fir_formats, EachChannelIsItsWindowsByTheSpectrum) {
        using v = TypeParam;
        const made<v, 31, 32>                      m(fir_lowpass{0.1, 60.0});
        const catenae::basic_fft_fir<v, 3, 31, 32> stage(m.design);
        const std::vector<v>                       in   = noise<v>(32 * 9 * 3, 5);
        const std::vector<v>                       want = by_windows(m.design, in, 3);
        for (const std::size_t call : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{100}, in.size()}) {
            EXPECT_TRUE(same_bits(run_in(stage, in, call), want)) << "calls of " << call;
        }
        // The generic tests: reset over any storage, the counts, the arrival numbers, any chunking.
        const made<v, 15, 16>                      short_m(fir_lowpass{0.2, 40.0});
        const catenae::basic_fft_fir<v, 2, 15, 16> short_one(short_m.design);
        const made<v, 33, 32>                      odd_m(fir_lowpass{0.05, 60.0});
        const catenae::basic_fft_fir<v, 1, 33, 32> longest(odd_m.design); // Taps - 1 = Block
        EXPECT_TRUE(catenae_test::reset_ignores_storage(short_one));
        EXPECT_TRUE(catenae_test::counts_are_exact(short_one));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(short_one, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(short_one));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(longest));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(longest));
    }

    // Verifies CAT-FFT-FIR-001.
    TEST(fft_fir, ItsBitsAreTheSameOnEveryTarget) {
        // The ROM low-pass of examples/fft_fir_tables.h over 16 blocks of stereo noise: every
        // operation is a copy or is in the compiled core at contraction off, so one checksum.
        const catenae::fft_fir<2, 255> stage(example::long_lowpass_fft);
        const std::vector<float>       out = run_in(stage, noise<float>(256 * 16 * 2, 7), 64);
        std::uint64_t                  h   = 14695981039346656037ull;
        for (const float x : out) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &x, sizeof x);
            h = (h ^ bits) * 1099511628211ull;
        }
        EXPECT_EQ(h, 0x85133308a8108cc1ull) << std::hex << h;
    }

    // Verifies CAT-FFT-FIR-002.
    TYPED_TEST(fft_fir_formats, ItIsTheDirectConvolutionWithinItsFloor) {
        using v = TypeParam;
        const made<v, 255, 256>                      m(fir_lowpass{0.1, 80.0});
        const catenae::basic_fft_fir<v, 2, 255, 256> stage(m.design);
        const std::vector<v>                         in    = noise<v>(256 * 12 * 2, 11);
        const std::vector<v>                         out   = run_in(stage, in, 64);
        const std::vector<long double>               want  = direct(m.taps, in, 2);
        long double                                  worst = 0.0L;
        for (std::size_t i = 0; i < out.size(); ++i) {
            worst = std::max(worst, std::fabs(static_cast<long double>(out[i]) - want[i]));
        }
        // Of full scale, over noise in [-1, 1), against a long double reference: measured 1.8e-7 in
        // float and 3.2e-16 in double (x86-64, GCC 13; the bits are the same on every target).
        // The bounds are about twice that, and three times in double, whose reference is itself
        // double where long double is (the Arm legs).
        const long double bound = std::is_same<v, float>::value ? 4e-7L : 1e-15L;
        EXPECT_LT(worst, bound) << static_cast<double>(worst);
        char measured[32];
        std::snprintf(measured, sizeof measured, "%.3g", static_cast<double>(worst));
        ::testing::Test::RecordProperty("worst", measured);
    }

    // Verifies CAT-FFT-FIR-002.
    TYPED_TEST(fft_fir_formats, ItsLatencyIsExactAndOfItsType) {
        using v    = TypeParam;
        using odd  = catenae::stage_traits<catenae::basic_fft_fir<v, 2, 63, 64>>;
        using even = catenae::stage_traits<catenae::basic_fft_fir<v, 2, 64, 64>>;
        using any  = catenae::stage_traits<catenae::basic_fft_fir_any<v, 2, 63, 64>>;
        static_assert(odd::has_exact_latency && odd::exact_latency.up == 31 && odd::exact_latency.down == 1, "31");
        static_assert(even::has_exact_latency && even::exact_latency.up == 63 && even::exact_latency.down == 2,
                      "half a frame at an even count");
        static_assert(!any::has_exact_latency, "any FIR: its design's");
        static_assert(odd::rests_at_zero && !odd::is_in_place && !odd::is_frame_synchronous, "the contract");
        static_assert(catenae::holds_view<catenae::basic_fft_fir<v, 2, 63, 64>>::value, "it views its spectrum");
        const made<v, 63, 64>                      m(fir_lowpass{0.1, 60.0});
        const catenae::basic_fft_fir<v, 1, 63, 64> stage(m.design);
        EXPECT_FLOAT_EQ(stage.latency().nominal, 31.0f);
        EXPECT_FLOAT_EQ(stage.latency().deviation, 0.0f);
        // A low-pass's impulse response peaks at its latency, the stream's frame 31.
        std::vector<v> impulse(256, v{0});
        impulse[0]             = v{1};
        const std::vector<v> h = run_in(stage, impulse, 256);
        EXPECT_EQ(std::max_element(h.begin(), h.end()) - h.begin(), 31);
        const made<v, 63, 64, fir_phase::any>          ma(fir_lowpass{0.1, 60.0});
        const catenae::basic_fft_fir_any<v, 1, 63, 64> other(ma.design);
        EXPECT_FLOAT_EQ(other.latency().nominal, 31.0f);
    }

    // Verifies CAT-FFT-FIR-003.
    TEST(fft_fir, ItArrivesInBlocksAndSteadyEvensThem) {
        using bare = catenae::fft_fir<2, 255>;
        using t    = catenae::stage_traits<bare>;
        static_assert(t::input_group == 256 && t::output_quantum == 256, "a block of 256, whole");
        static_assert(t::max_shortfall.up == 255 && t::max_shortfall.down == 1 && t::max_lead.up == 0, "behind only");
        static_assert(std::is_same<bare, catenae::basic_fft_fir<float, 2, 255, 256>>::value, "the default block");
        const bare stage(example::long_lowpass_fft);
        // Calls of 64: three write nothing, the fourth a block.
        auto                     st = std::make_unique<bare::state>();
        auto                     ws = std::make_unique<bare::workspace>();
        const std::vector<float> in = noise<float>(256 * 8 * 2, 3);
        std::vector<float>       part(256 * 2);
        for (std::size_t call = 0; call < 8; ++call) {
            const std::size_t w = stage.run(*st, *ws, catenae::span<const float>(in.data() + call * 128, 128),
                                            catenae::span<float>(part));
            EXPECT_EQ(w, call % 4 == 3 ? 256u : 0u) << "call " << call;
        }
        // In steady<>, 64 for 64, and the latency 255 frames more.
        using even = catenae::steady<bare>;
        static_assert(catenae::stage_traits<even>::is_frame_synchronous, "n frames for n");
        static_assert(catenae::stage_traits<even>::exact_latency.up == 382, "127 + 255");
        const auto callback = catenae::chain(even{stage});
        auto       mem      = std::make_unique<catenae::memory_of<decltype(callback)>>();
        callback.reset(mem->state);
        const std::vector<float> straight = run_in(stage, in, in.size());
        std::vector<float>       out;
        for (std::size_t at = 0; at < in.size(); at += 128) {
            std::array<float, 128> o{};
            EXPECT_EQ(callback.process(*mem, catenae::span<const float>(in.data() + at, 128), catenae::span<float>(o)),
                      64u);
            out.insert(out.end(), o.begin(), o.end());
        }
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float want = i < 255 * 2 ? 0.0f : straight[i - 255 * 2];
            ASSERT_EQ(std::memcmp(&out[i], &want, sizeof want), 0) << "value " << i;
        }
    }

    // Verifies CAT-FFT-FIR-004.
    TYPED_TEST(fft_fir_formats, ItsTailIsTwoBlocksLessOne) {
        using v = TypeParam;
        const made<v, 17, 16>                      m(fir_lowpass{0.2, 40.0});
        const catenae::basic_fft_fir<v, 2, 17, 16> stage(m.design);
        EXPECT_EQ(stage.tail_frames(), 31u);
        EXPECT_EQ(stage.practical_tail_frames(), 31u);
        EXPECT_FLOAT_EQ(stage.flush_residue(), 0.0f);
        // Noise to the first frame of block 3, then zeros: the 31st zero completes block 4, which
        // still holds the convolution's last sample; every block after it is exactly silence.
        std::vector<v> in = noise<v>(16 * 12 * 2, 13);
        std::fill(in.begin() + (16 * 3 + 1) * 2, in.end(), v{0});
        const std::vector<v> out = run_in(stage, in, 5);
        ASSERT_EQ(out.size(), in.size());
        bool block_4 = false;
        for (std::size_t i = 16 * 4 * 2; i < 16 * 5 * 2; ++i) {
            block_4 = block_4 || !is_zero(out[i]);
        }
        EXPECT_TRUE(block_4) << "the tail is not shorter";
        for (std::size_t i = 16 * 5 * 2; i < out.size(); ++i) {
            ASSERT_TRUE(is_zero(out[i])) << "value " << i;
        }
    }

    // Verifies CAT-FFT-FIR-001.
    TYPED_TEST(fft_fir_formats, ANonFiniteSampleSpoilsTwoBlocksOfItsChannel) {
        using v = TypeParam;
        const made<v, 17, 16>                      m(fir_lowpass{0.2, 40.0});
        const catenae::basic_fft_fir<v, 2, 17, 16> stage(m.design);
        std::vector<v>                             in = noise<v>(16 * 6 * 2, 17);
        in[(16 * 2 + 5) * 2]                          = std::numeric_limits<v>::quiet_NaN(); // channel 0, block 2
        const std::vector<v> out                      = run_in(stage, in, 16);
        for (std::size_t f = 0; f < 16 * 6; ++f) {
            const bool held = f >= 16 * 2 && f < 16 * 4;
            EXPECT_EQ(std::isfinite(out[f * 2]), !held) << "channel 0, frame " << f;
            EXPECT_TRUE(std::isfinite(out[f * 2 + 1])) << "channel 1, frame " << f;
        }
    }

    // Verifies CAT-FFT-FIR-005.
    TYPED_TEST(fft_fir_formats, ANewDesignAppliesFromTheNextBlock) {
        using v = TypeParam;
        using s = catenae::basic_fft_fir<v, 2, 31, 32>;
        const made<v, 31, 32> first(fir_lowpass{0.1, 60.0});
        const made<v, 31, 32> second(fir_lowpass{0.3, 60.0});
        s                     stage(first.design);
        const std::vector<v>  in = noise<v>(32 * 6 * 2, 19);
        auto                  st = std::make_unique<typename s::state>();
        auto                  ws = std::make_unique<typename s::workspace>();
        std::vector<v>        part(32 * 4 * 2);
        static_cast<void>(stage.run(*st, *ws, catenae::span<const v>(in.data(), 50 * 2), catenae::span<v>(part)));
        auto again = std::make_unique<typename s::state>(*st); // the same window, the new design
        stage.set_design(second.design);
        const s           fresh(second.design);
        std::vector<v>    a(part.size());
        std::vector<v>    b(part.size());
        const std::size_t wa =
            stage.run(*st, *ws, catenae::span<const v>(in.data() + 100, 100 * 2), catenae::span<v>(a));
        const std::size_t wb =
            fresh.run(*again, *ws, catenae::span<const v>(in.data() + 100, 100 * 2), catenae::span<v>(b));
        ASSERT_EQ(wa, wb);
        EXPECT_EQ(wa, 96u); // 18 frames held, 100 more: three blocks
        EXPECT_TRUE(same_bits(a, b));
        EXPECT_EQ(stage.design().data(), second.design.data());
    }

} // namespace
