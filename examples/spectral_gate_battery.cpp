/// @file spectral_gate_battery.cpp
/// @brief The battery of the gate in spectral_gate.h and of PLAN.md 9.5's region around it
/// (guide: The transform domain), run with the library's own on every leg.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/steady.h"
#include "catenae/stft.h"
#include "catenae_test/harness.h"
#include "spectral_gate.h"

namespace {

    using cfloat = catenae::complex<float>;

    // Verifies CAT-STAGE-009, CAT-STAGE-010, CAT-STAGE-011 and CAT-STAGE-012.
    TEST(spectral_gate, PassesTheGenericTests) {
        // The harness's bins are within [-1, 1] each part: some reach 0.25 in power, some not.
        constexpr example::spectral_gate<9, 2> stage{example::gate_design{0.25f, 0.1f, 0.5f, 0.125f}};
        // 400 frames of 18 bins each: a spectrum's frames are large, and a bare-metal heap small.
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage, 400));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage, 400));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage, 400));
    }

    // The design's numbers: a full-scale sine's bin is n/4, and the dB are tenths of decades.
    TEST(spectral_gate, DesignIsItsDecibels) {
        EXPECT_EQ(example::amplitude_of_db(0), 1.0);
        EXPECT_EQ(example::amplitude_of_db(20), 10.0);
        EXPECT_NEAR(example::amplitude_of_db(-20), 0.1, 1e-17);
        EXPECT_NEAR(example::amplitude_of_db(-40), 0.01, 1e-18);
        EXPECT_NEAR(example::amplitude_of_db(-6), 0.501187233627272285, 1e-16);
        EXPECT_NEAR(example::amplitude_of_db(-63), 7.07945784384138e-4, 1e-18);
        constexpr example::gate_design d = example::hann_gate(512, -40, -20, 2, 8);
        EXPECT_NEAR(d.open_power, 1.6384f, 1e-6f); // (128 x 0.01)^2
        EXPECT_NEAR(d.floor, 0.1f, 1e-8f);
        EXPECT_EQ(d.attack_step, 0.5f);
        EXPECT_EQ(d.release_step, 0.125f);
        EXPECT_EQ(example::hann_gate(512, -40, -20, 0, 0).attack_step, 1.0f); // at least one frame
    }

    /// One bin through the gate, frame by frame: its gain each frame.
    template <std::size_t Frames>
    std::array<float, Frames> gains(const example::spectral_gate<3, 1>& stage, example::spectral_gate<3, 1>::state& st,
                                    float level) {
        example::spectral_gate<3, 1>::workspace ws{};
        std::array<float, Frames>               g{};
        for (std::size_t k = 0; k < Frames; ++k) {
            std::array<cfloat, 3> frame{cfloat{level, 0.0f}, cfloat{0.0f, level}, cfloat{0.0f, 0.0f}};
            const std::size_t     made =
                stage.run(st, ws, catenae::span<const cfloat>(frame), catenae::span<cfloat>(frame)); // in place
            EXPECT_EQ(made, 1u);
            g[k] = frame[0].re / level;
            EXPECT_EQ(frame[1].im, frame[0].re); // the same power, the same gain
            EXPECT_EQ(frame[1].re, 0.0f);
            EXPECT_EQ(frame[2].re, 0.0f); // a zero bin is zero at any gain
            EXPECT_EQ(frame[2].im, 0.0f);
        }
        return g;
    }

    // The card's Gain: from rest every bin is closed, at the floor; a bin at the threshold opens
    // over the attack frames, and one below it closes over the release frames.
    TEST(spectral_gate, OpensOverAttackAndClosesOverRelease) {
        constexpr example::spectral_gate<3, 1> stage{example::gate_design{1.0f, 0.25f, 0.25f, 0.125f}};
        example::spectral_gate<3, 1>::state    st{};
        const auto                             open = gains<6>(stage, st, 1.0f); // power 1: at the threshold
        const std::array<float, 6>             opening{0.4375f, 0.625f, 0.8125f, 1.0f, 1.0f, 1.0f};
        EXPECT_EQ(std::memcmp(open.data(), opening.data(), sizeof open), 0);
        const auto                  closed = gains<10>(stage, st, 0.5f); // power 0.25: below
        const std::array<float, 10> closing{0.90625f, 0.8125f,  0.71875f, 0.625f, 0.53125f,
                                            0.4375f,  0.34375f, 0.25f,    0.25f,  0.25f};
        EXPECT_EQ(std::memcmp(closed.data(), closing.data(), sizeof closed), 0);
        // The third bin, always zero, stayed closed.
        EXPECT_EQ(st.openness[2], 0.0f);
    }

    // The card's Limits: a NaN bin comes out NaN and closes; its neighbours are untouched.
    TEST(spectral_gate, NanBinComesOutNanAndCloses) {
        constexpr example::spectral_gate<3, 1>  stage{example::gate_design{0.0f, 0.5f, 1.0f, 1.0f}};
        example::spectral_gate<3, 1>::state     st{};
        example::spectral_gate<3, 1>::workspace ws{};
        std::array<cfloat, 3> frame{cfloat{std::nanf(""), 0.0f}, cfloat{0.5f, 0.0f}, cfloat{0.0f, 0.0f}};
        stage.run(st, ws, catenae::span<const cfloat>(frame), catenae::span<cfloat>(frame));
        EXPECT_TRUE(std::isnan(frame[0].re));
        EXPECT_EQ(st.openness[0], 0.0f);
        EXPECT_EQ(frame[1].re, 0.5f); // open from the first frame: a threshold of 0 and one frame
        EXPECT_EQ(st.openness[1], 1.0f);
    }

    /// Noise in [-0.5, 0.5): the same on every target.
    float noise(std::uint32_t& s) noexcept {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return static_cast<float>(s / 256u) / 16777216.0f - 0.5f;
    }

    /// What a chain writes over `in` in calls of `sizes` frames, cycled.
    template <typename Chain>
    std::vector<float> run_calls(const Chain& c, const std::vector<float>& in, const std::vector<std::size_t>& sizes) {
        auto m = std::make_unique<catenae::memory_of<Chain>>();
        c.reset(m->state);
        std::vector<float> out;
        std::vector<float> block(1024);
        std::size_t        at = 0;
        for (std::size_t call = 0; at < in.size(); ++call) {
            const std::size_t n    = std::min(sizes[call % sizes.size()], in.size() - at);
            const std::size_t made = c.process(*m, catenae::span<const float>(in.data() + at, n),
                                               catenae::span<float>(block.data(), block.size()));
            out.insert(out.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(made));
            at += n;
        }
        return out;
    }

    // PLAN.md 9.5: the region is bare at its hop and frame-synchronous inside steady, which is
    // the bare region 127 frames later, bit for bit, for any call sizes.
    TEST(spectral_gate, SteadyIsTheBareRegionLater) {
        std::vector<float> in(48 * 128);
        std::uint32_t      s = 0x9E3779B9u;
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = 0.5f * std::sin(0.3681553890925539f * static_cast<float>(i)) + 0.02f * noise(s);
        }
        const std::vector<float> bare = run_calls(example::gate, in, {128});
        ASSERT_EQ(bare.size(), in.size());
        for (const std::vector<std::size_t>& sizes :
             {std::vector<std::size_t>{64}, std::vector<std::size_t>{48}, std::vector<std::size_t>{1, 127, 300, 5}}) {
            const std::vector<float> steady = run_calls(example::denoise, in, sizes);
            ASSERT_EQ(steady.size(), in.size());
            for (std::size_t i = 0; i < steady.size(); ++i) {
                const float owed = i < 127 ? 0.0f : bare[i - 127];
                ASSERT_EQ(std::memcmp(&steady[i], &owed, sizeof owed), 0) << "frame " << i << ", calls of " << sizes[0];
            }
        }
    }

    // A gate open from the first frame passes every bin at gain 1: the region is the pair alone,
    // bit for bit.
    TEST(spectral_gate, OpenGateIsThePairAlone) {
        constexpr auto open = catenae::stft<512, 128, 1>{catenae::window::hann}
                              | example::spectral_gate<257, 1>{example::gate_design{0.0f, 0.0f, 1.0f, 1.0f}}
                              | catenae::istft<512, 128, 1>{catenae::window::hann};
        constexpr auto pair =
            catenae::stft<512, 128, 1>{catenae::window::hann} | catenae::istft<512, 128, 1>{catenae::window::hann};
        std::vector<float> in(16 * 128);
        std::uint32_t      s = 0x2545F491u;
        for (float& x : in) {
            x = noise(s);
        }
        const std::vector<float> gated = run_calls(open, in, {128});
        const std::vector<float> alone = run_calls(pair, in, {128});
        ASSERT_EQ(gated.size(), alone.size());
        EXPECT_EQ(std::memcmp(gated.data(), alone.data(), gated.size() * sizeof(float)), 0);
    }

    // The example's gate on a tone at a bin's centre in noise: the tone's three bins stay open and
    // the other 254 close to the floor, 20 dB down, so about (3 + 2.54) / 257 of the noise's power
    // is left, 16.7 dB down (17.1 measured here and by the denoise example).
    TEST(spectral_gate, GatesTheNoiseAroundATone) {
        constexpr std::size_t frames = 64 * 128;
        constexpr float       w      = 2.0f * 3.14159265358979f * 32.0f / 512.0f; // bin 32's centre
        std::vector<float>    tone(frames);
        std::vector<float>    in(frames);
        std::uint32_t         s = 0x6A09E667u;
        for (std::size_t i = 0; i < frames; ++i) {
            tone[i] = 0.25f * std::sin(w * static_cast<float>(i));
            in[i]   = tone[i] + 0.04f * noise(s);
        }
        const std::vector<float> out = run_calls(example::gate, in, {128});
        // After 16 hops, every bin has settled: out against the tone 384 frames earlier.
        double noise_in  = 0.0;
        double noise_out = 0.0;
        for (std::size_t i = 16 * 128; i < frames; ++i) {
            const double n_in  = static_cast<double>(in[i - 384]) - static_cast<double>(tone[i - 384]);
            const double n_out = static_cast<double>(out[i]) - static_cast<double>(tone[i - 384]);
            noise_in += n_in * n_in;
            noise_out += n_out * n_out;
        }
        const double down_db = 10.0 * std::log10(noise_in / noise_out);
        EXPECT_GT(down_db, 16.0) << down_db;
        EXPECT_LT(down_db, 18.0) << down_db;
    }

} // namespace
