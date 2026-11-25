/// @file test_async.cpp
/// @brief The async skeleton (fit/async/junction.h): SampleRateTap's asynchronous converter as a
/// junction behind fed_by, its servo locking at two clock offsets (PLAN.md section 10, CF).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Each lock test is a deterministic simulation on one thread: a producer pushes blocks through
// `feeding` at 48 kHz (1 + ppm 1e-6), a consumer requests frames through `fed_by` at the chain's
// output clock (48 kHz times the chain's ratio), and whichever clock is due next runs. The runs
// last 10 simulated seconds; the estimate is averaged over the last 20/3 s, two whole periods of
// the beat between 32-frame pushes and the pulls at 200 ppm (32 / (200e-6 * 48000) = 10/3 s).
//
// Measured on the host (GCC 13): locked at 1.62 s and 1.17 s behind the 128-frame block (+200,
// -200 ppm), 0.74 and 0.98 s behind the chain by 4, 0.62 and 0.68 s behind 3/2, 1.84 and 1.40 s
// through steady<>. The holdover patterns lock with one-frame pushes; with 32-frame pushes they do
// not, and neither do regular pulls of 24, 25 or 26 frames on SampleRateTap's own converter: the
// carried servo locks when pushes and pulls are commensurate, or when the producer pushes frame by
// frame. The same converter with this skeleton's bank gives these runs' numbers to the printed digit.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "async/junction.h"
#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/compiler.h"
#include "catenae/fed_by.h"
#include "catenae/feeding.h"
#include "catenae/gain.h"
#include "catenae/junction_traits.h"
#include "catenae/steady.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

// A named namespace: the test's own toy stage, and a junction that records its pulls.
namespace async_fit_fixture {

    /// 1:3, each frame written three times: with the fixtures' down2_first, a chain by 3/2.
    template <std::size_t Ch>
    class up3_toy {
      public:
        using input_port  = catenae::port<float, Ch>;
        using output_port = catenae::port<float, Ch>;
        static constexpr catenae::ratio rate_ratio{3, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = 3;
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{1, 1};
        static constexpr bool           rests_at_zero = true;

        struct state {};
        using workspace = catenae::no_workspace;

        std::size_t run(state&, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t copy = 0; copy < 3; ++copy) {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[(3 * frame + copy) * Ch + c] = in[frame * Ch + c];
                    }
                }
            }
            return 3 * frames;
        }

        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return 3 * n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return (k + 2) / 3; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return 3 * n; }
        catenae::stage_latency       latency() const noexcept { return {1.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

    /// What a recorded junction writes down: the size of the latest pull, and, once armed, the
    /// farthest the backlog a pull found was from the setpoint.
    struct record {
        std::size_t last;   ///< the latest pull's frames
        bool        armed;  ///< whether to measure the backlog
        float       breath; ///< the farthest |backlog - setpoint| a pull found while armed
    };

    /// A junction that writes its pulls into a record.
    template <typename J>
    struct recorded : J {
        record*     log;
        std::size_t pull(typename J::state& st, catenae::span<float> out) const noexcept {
            log->last = out.size() / J::sink_port::channels;
            if (log->armed) { // the backlog the junction's pull reads: the ring and the staged chunk
                const auto  staged  = st.resampler.staged_frames - st.resampler.staged_read;
                const float backlog = static_cast<float>(st.head - st.tail + staged);
                const float off     = backlog - static_cast<float>(J::report(st).target_frames);
                log->breath         = std::max(log->breath, off < 0.0f ? -off : off);
            }
            return J::pull(st, out);
        }
    };

} // namespace async_fit_fixture

namespace catenae {
    template <std::size_t Ch>
    struct is_stage<async_fit_fixture::up3_toy<Ch>> : std::true_type {};
    template <typename J>
    struct is_junction<async_fit_fixture::recorded<J>> : std::true_type {};
} // namespace catenae

namespace {

    using catenae::operator|;
    using async_fit_fixture::recorded;
    using catenae_fit::async::lock_state;

    using asrc_1k = catenae_fit::async::junction<2, 1024, 64>; // stereo, 1,024 frames, pushes up to 64
    using asrc_2k = catenae_fit::async::junction<2, 2048, 64>;

    constexpr auto block_128 = catenae::chain(catenae_fixtures::block_stage<2, 128>{});
    constexpr auto up_by_4   = catenae_fixtures::up2_toy<2>{} | catenae_fixtures::up2_toy<2>{};
    constexpr auto by_3_2    = async_fit_fixture::up3_toy<2>{} | catenae_fixtures::down2_first<2>{};
    constexpr auto steady_128 =
        catenae::chain(catenae::steady<catenae_fixtures::block_stage<2, 128>>(catenae_fixtures::block_stage<2, 128>{}));

    constexpr double k_rate    = 48000.0;
    constexpr double k_seconds = 10.0;

    /// What one run shows.
    struct outcome {
        double                           locked_at;     ///< simulated seconds at the first locked report, or -1
        catenae_fit::async::async_report end;           ///< the report at the end
        double                           mean_ppm;      ///< the estimate averaged over the last 20/3 s
        std::size_t                      starved_first; ///< fed_by's starved count when real frames first came
        std::size_t                      starved_end;   ///< and at the end
        std::vector<std::size_t>         pulls;         ///< the pull of each of the first 12 requests, 0 if none
        catenae::junction_latency        latency;       ///< at the end
        float                            last_sample;   ///< the end's last output sample
        float                            breath;        ///< the farthest the backlog was from the setpoint once locked
    };

    // CF finding: fed_by inlined here, over a state from make_unique, draws GCC 13's -Warray-bounds
    // at -O2, and at -O3 with CATENAE_CHECKS=audit: it cannot bound `held` (at most the holdover,
    // an invariant), so the holdover's copies in fed_by.h (lines 132 and 134) look out of bounds.
    // The request goes through a function the compiler keeps out of line, which knows no size.
    template <typename L, typename C, typename Memory, typename Scratch>
    CATENAE_NOINLINE std::size_t request_once(const L& link, typename L::state& shared, const C& chain, Memory& st,
                                              Scratch& ws, catenae::span<float> out) noexcept {
        return catenae::fed_by(link, shared, chain, st, ws, out);
    }

    /// Two clocks on one thread: `push`-frame blocks of 0.5 pushed through `feeding` at 48 kHz
    /// (1 + ppm 1e-6), `request`-frame requests through `fed_by` at 48 kHz times the chain's ratio.
    template <std::size_t MaxRequest, typename J, typename C>
    outcome two_clocks(const J& asrc, typename J::state& shared, const C& chain, std::size_t request, std::size_t push,
                       double ppm, double seconds = k_seconds) {
        constexpr std::size_t ch = J::sink_port::channels;
        using chain_t            = std::remove_cv_t<C>;
        async_fit_fixture::record seen{0, false, 0.0f};
        const recorded<J>         link{asrc, &seen};
        auto                      st = std::make_unique<catenae::fed_by_state<chain_t>>();
        auto                      ws = std::make_unique<catenae::fed_by_workspace<chain_t, MaxRequest>>();
        reset(chain, *st);
        constexpr auto feed = catenae::chain(catenae::gain<ch>{1.0f, 0});
        using feed_t        = std::remove_cv_t<decltype(feed)>;
        auto fst            = std::make_unique<catenae::feeding_state<feed_t>>();
        auto fws            = std::make_unique<catenae::feeding_workspace<feed_t>>();
        feed.reset(*fst);

        const std::vector<float> in(push * ch, 0.5f);
        std::vector<float>       out(request * ch);
        const double             in_rate  = k_rate * (1.0 + ppm * 1e-6);
        const double             out_rate = k_rate * chain_t::rate_ratio.up / chain_t::rate_ratio.down;
        outcome                  o{-1.0, {}, 0.0, 0, 0, {}, {}, 0.0f, 0.0f};
        const double             average_after = seconds - 20.0 / 3.0; // two beat periods before the end
        double                   t_in = 0.0, t_out = 0.0, sum = 0.0;
        std::size_t              averaged = 0, requests = 0;
        bool                     real = false;
        while (t_out < seconds) {
            if (t_in <= t_out) {
                static_cast<void>(catenae::feeding(link, shared, feed, *fst, *fws, catenae::span<const float>(in)));
                t_in += static_cast<double>(push) / in_rate;
                continue;
            }
            seen.last = 0;
            static_cast<void>(request_once(link, shared, chain, *st, *ws, catenae::span<float>(out)));
            t_out += static_cast<double>(request) / out_rate;
            if (++requests <= 12) {
                o.pulls.push_back(seen.last);
            }
            const auto r = asrc.report(shared);
            if (!real && r.state != lock_state::filling) {
                real            = true;
                o.starved_first = st->starved;
            }
            if (o.locked_at < 0.0 && r.state == lock_state::locked) {
                o.locked_at = t_out;
                seen.armed  = true;
            }
            if (t_out > average_after) {
                sum += static_cast<double>(r.ppm);
                ++averaged;
            }
        }
        o.end         = asrc.report(shared);
        o.mean_ppm    = sum / static_cast<double>(averaged);
        o.starved_end = st->starved;
        o.latency     = asrc.latency(shared);
        o.last_sample = out.back();
        o.breath      = seen.breath;
        return o;
    }

    /// The same, from a state at rest of its own.
    template <std::size_t MaxRequest, typename J, typename C>
    outcome two_clocks(const J& asrc, const C& chain, std::size_t request, std::size_t push, double ppm) {
        auto shared = std::make_unique<typename J::state>(); // value-initialised: all zero, at rest
        return two_clocks<MaxRequest>(asrc, *shared, chain, request, push, ppm);
    }

    /// Locked within `by` seconds and still locked at the end, the estimate within `ppm_within` of
    /// the truth, no underrun, overrun or resync, and no frame starved once the first real one came.
    ::testing::AssertionResult locks(const outcome& o, double ppm, double by, double ppm_within) {
        if (o.locked_at < 0.0 || o.locked_at > by || o.end.state != lock_state::locked) {
            return ::testing::AssertionFailure()
                   << "locked at " << o.locked_at << " s, state " << static_cast<unsigned>(o.end.state);
        }
        if (o.end.underruns != 0 || o.end.overruns != 0 || o.end.resyncs != 0 || o.starved_end != o.starved_first) {
            return ::testing::AssertionFailure()
                   << o.end.underruns << " underruns, " << o.end.overruns << " overruns, " << o.end.resyncs
                   << " resyncs, starved " << o.starved_first << " then " << o.starved_end;
        }
        if (std::fabs(o.mean_ppm - ppm) > ppm_within) {
            return ::testing::AssertionFailure() << "estimate " << o.mean_ppm << " ppm for " << ppm;
        }
        return ::testing::AssertionSuccess() << "locked at " << o.locked_at << " s, estimate " << o.mean_ppm;
    }

    // The contract: junction_traits reads every member; the state is plain storage at rest when
    // zero, so a static one is ready as it stands; the configuration is small and constant.
    TEST(async_junction, MeetsTheJunctionContract) {
        using traits = catenae::junction_traits<asrc_1k>;
        static_assert(traits::opted_in && traits::clocking == catenae::clocks::independent, "two clocks");
        static_assert(traits::rate_ratio.up == 1 && traits::rate_ratio.down == 1, "nominally 1:1");
        static_assert(traits::capacity_frames == 1024, "its capacity, a template argument");
        static_assert(catenae::detail::k_is_plain_storage<asrc_1k::state>, "plain storage");
        static_assert(std::is_trivially_copyable<asrc_1k>::value && sizeof(asrc_1k) <= 512, "D54");
        // The setpoint arithmetic: SampleRateTap's rule and its ring bound, as constants.
        static_assert(asrc_1k::max_target == 341 && asrc_2k::max_target == 682, "(capacity - 1) / 3");
        static_assert(asrc_1k::target_for(128) == 192 && asrc_1k::target_for(26) == 42, "pull + max(pull / 2, 16)");
        static_assert(asrc_1k::largest_pull == 227, "227 + 113 = 340");
        static_assert(traits::serves(191, 1) && !traits::serves(228, 1), "behind a 128 block at 64: 191");
        static_assert(catenae::junction_traits<asrc_2k>::serves(287, 1), "behind a 128 block at 160: 287");

        CATENAE_CONSTINIT static const asrc_1k  asrc{};
        CATENAE_CONSTINIT static asrc_1k::state shared{}; // zero: at rest, in .bss
        EXPECT_FLOAT_EQ(asrc.tolerance_ppm(), 1000.0f);
        const auto r = asrc.report(shared);
        EXPECT_EQ(r.state, lock_state::filling);
        EXPECT_EQ(r.target_frames, 48u);
        EXPECT_EQ(r.underruns + r.overruns + r.resyncs, 0u);
        const auto latency = asrc.latency(shared);
        EXPECT_FLOAT_EQ(latency.nominal, 48.0f + 511.0f / 64.0f); // the target and the bank's group delay
        EXPECT_FLOAT_EQ(latency.range, 64.0f + 16.0f);
        std::array<float, 64> out{};
        out.fill(1.0f);
        EXPECT_EQ(asrc.pull(shared, catenae::span<float>(out)), 0u); // filling: silence
        EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](float v) { return v <= 0.0f && v >= 0.0f; }));
    }

    // Behind a 128-frame block stage, 64-frame requests pull 128, none, 128, none: the servo is
    // clocked every other request and locks as under a regular 128, its setpoint raised to 192.
    TEST(async_junction, LocksBehindABlockStageWithRequestsThatPullNothing) {
        for (const double ppm : {200.0, -200.0}) {
            const outcome o = two_clocks<64>(asrc_1k{}, block_128, 64, 32, ppm);
            EXPECT_TRUE(locks(o, ppm, 2.0, 1.0)) << ppm;
            const std::vector<std::size_t> pattern = {128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0};
            EXPECT_EQ(o.pulls, pattern);
            EXPECT_EQ(o.end.target_frames, 192u);
            EXPECT_FLOAT_EQ(o.latency.nominal, 192.0f + 511.0f / 64.0f);
            EXPECT_LE(o.breath, 34.0f) << ppm; // within a 32-frame push and two of the setpoint
            EXPECT_LE(o.breath, o.latency.range);
            EXPECT_NEAR(o.last_sample, 0.5f, 1e-4f); // a constant 0.5 comes through: the frames are real
        }
    }

    // Behind chains that leave a holdover the pulls vary by a frame: by 4 at 102-frame requests,
    // 26, 25; by 3/2 at 64, 43, 42, 43 in turn. With a producer of one frame a push, both lock.
    TEST(async_junction, LocksBehindAChainThatLeavesAHoldover) {
        for (const double ppm : {200.0, -200.0}) {
            const outcome by_4 = two_clocks<102>(asrc_1k{}, up_by_4, 102, 1, ppm);
            EXPECT_TRUE(locks(by_4, ppm, 1.5, 1.0)) << ppm;
            const std::vector<std::size_t> pattern_4 = {26, 25, 26, 25, 26, 25, 26, 25, 26, 25, 26, 25};
            EXPECT_EQ(by_4.pulls, pattern_4);
            EXPECT_EQ(by_4.end.target_frames, 48u); // 26 needs 42: the configured 48 stands

            const outcome by_3_2_run = two_clocks<64>(asrc_1k{}, by_3_2, 64, 1, ppm);
            EXPECT_TRUE(locks(by_3_2_run, ppm, 1.5, 1.0)) << ppm;
            const std::vector<std::size_t> pattern_3_2 = {43, 42, 43, 43, 42, 43, 43, 42, 43, 43, 42, 43};
            EXPECT_EQ(by_3_2_run.pulls, pattern_3_2);
            EXPECT_EQ(by_3_2_run.end.target_frames, 64u); // 43 needs 64
        }
    }

    // A request larger than the chain's block: 160 behind a 128-frame block stage pulls 256, 128,
    // 128, 128 at equal intervals. The backlog a pull sees then steps by up to 96 frames at the
    // request rate, which the servo's smoothers do not reject: it does not lock in 10 s and its
    // estimate is far from the truth, though nothing underruns. SampleRateTap's converter does the
    // same with this pattern. Through steady<> the same chain is pulled 160 at a time, and locks.
    TEST(async_junction, ARequestLargerThanTheBlockLocksOnlyThroughSteady) {
        for (const double ppm : {200.0, -200.0}) {
            const outcome                  bare    = two_clocks<160>(asrc_2k{}, block_128, 160, 32, ppm);
            const std::vector<std::size_t> pattern = {256, 128, 128, 128, 256, 128, 128, 128, 256, 128, 128, 128};
            EXPECT_EQ(bare.pulls, pattern);
            EXPECT_LT(bare.locked_at, 0.0) << ppm; // never locked
            EXPECT_EQ(bare.end.state, lock_state::acquiring);
            EXPECT_EQ(bare.end.underruns + bare.end.overruns + bare.end.resyncs, 0u);
            EXPECT_GT(std::fabs(bare.mean_ppm - ppm), 100.0) << bare.mean_ppm;

            const outcome even = two_clocks<160>(asrc_1k{}, steady_128, 160, 32, ppm);
            EXPECT_TRUE(locks(even, ppm, 2.0, 1.0)) << ppm;
            const std::vector<std::size_t> regular(12, 160);
            EXPECT_EQ(even.pulls, regular);
            EXPECT_EQ(even.end.target_frames, 240u);
        }
    }

    // The setpoint configured from fed_by's largest pull instead of raised at run time: 286 for a
    // 128-frame block at 64-frame requests (largest pull 191), 94 frames above the raise to 192.
    TEST(async_junction, ATargetConfiguredFromTheLargestPullIsNeverRaised) {
        constexpr std::size_t largest = catenae::fed_by_workspace<decltype(block_128), 64>::largest_pull;
        static_assert(largest == 191 && asrc_1k::target_for(largest) == 286, "64 + 127, then 191 + 95");
        asrc_1k::config cfg{};
        cfg.target_latency_frames = static_cast<std::uint32_t>(asrc_1k::target_for(largest));
        const auto made           = asrc_1k::make(cfg);
        ASSERT_TRUE(made.has_value());
        const outcome o = two_clocks<64>(*made, block_128, 64, 32, 200.0);
        EXPECT_TRUE(locks(o, 200.0, 3.0, 1.0));
        EXPECT_EQ(o.end.target_frames, 286u);
        EXPECT_FLOAT_EQ(o.latency.nominal, 286.0f + 511.0f / 64.0f);

        // make() refuses what the type does not already fix.
        cfg.target_latency_frames = 342;
        EXPECT_EQ(asrc_1k::make(cfg).error(), catenae::status::out_of_range);
        cfg.target_latency_frames = 0;
        EXPECT_EQ(asrc_1k::make(cfg).error(), catenae::status::invalid_argument);
        cfg                = asrc_1k::config{};
        cfg.sample_rate_hz = std::nan("");
        EXPECT_EQ(asrc_1k::make(cfg).error(), catenae::status::invalid_argument);
    }

    // restart, on the consumer's thread: what is buffered goes, the estimate is forgotten, the
    // raised setpoint stays, and it fills and locks again exactly as it first did. reset: a state
    // that ran is at rest again, and a run from it is a run from a zero state, bit for bit.
    TEST(async_junction, RestartRefillsAndResetReturnsToRest) {
        const asrc_1k asrc{};
        auto          shared = std::make_unique<asrc_1k::state>();
        const outcome first  = two_clocks<64>(asrc, *shared, block_128, 64, 32, 200.0, 5.0);
        ASSERT_EQ(first.end.state, lock_state::locked);
        asrc.restart(*shared);
        const auto after = asrc.report(*shared);
        EXPECT_EQ(after.state, lock_state::filling);
        EXPECT_FLOAT_EQ(after.ppm, 0.0f);
        EXPECT_EQ(after.target_frames, 192u);
        const outcome again = two_clocks<64>(asrc, *shared, block_128, 64, 32, 200.0, 5.0);
        EXPECT_EQ(again.end.state, lock_state::locked);
        EXPECT_DOUBLE_EQ(again.locked_at, first.locked_at); // filled and locked as it did from rest
        EXPECT_EQ(again.end.underruns + again.end.resyncs, 0u);

        asrc.reset(*shared);
        const auto rest = asrc.report(*shared);
        EXPECT_EQ(rest.state, lock_state::filling);
        EXPECT_EQ(rest.target_frames, 48u);
        EXPECT_EQ(rest.underruns + rest.overruns + rest.resyncs, 0u);
        auto          fresh   = std::make_unique<asrc_1k::state>();
        const outcome from_0  = two_clocks<64>(asrc, *fresh, block_128, 64, 32, -200.0, 3.0);
        const outcome from_rs = two_clocks<64>(asrc, *shared, block_128, 64, 32, -200.0, 3.0);
        EXPECT_DOUBLE_EQ(from_0.locked_at, from_rs.locked_at);
        EXPECT_EQ(std::memcmp(&from_0.end, &from_rs.end, sizeof from_0.end), 0);
    }

    // The fill policy carried: a producer that stops costs one underrun, and the junction fills
    // again keeping its estimate; a consumer that stops lets the ring overrun, and the first pull
    // after it drops the backlog above the setpoint (a resync).
    TEST(async_junction, AnUnderrunRefillsAndAStallResyncs) {
        const asrc_1k asrc{};
        auto          shared = std::make_unique<asrc_1k::state>();
        const outcome first  = two_clocks<64>(asrc, *shared, block_128, 64, 1, 200.0, 5.0);
        ASSERT_EQ(first.end.state, lock_state::locked);

        std::array<float, 2 * 128> out{};
        for (int i = 0; i < 4; ++i) { // the producer has stopped
            static_cast<void>(asrc.pull(*shared, catenae::span<float>(out)));
        }
        const auto dry = asrc.report(*shared);
        EXPECT_EQ(dry.underruns, 1u);
        EXPECT_EQ(dry.state, lock_state::filling);
        EXPECT_NEAR(dry.ppm, 200.0f, 10.0f); // the integrator's estimate (207), kept across the dropout

        const std::array<float, 2 * 64> in{};
        for (int i = 0; i < 40; ++i) { // the consumer has stopped
            static_cast<void>(asrc.push(*shared, catenae::span<const float>(in)));
        }
        EXPECT_EQ(asrc.report(*shared).overruns, 24u); // the empty ring holds 16 of the 40 pushes of 64
        static_cast<void>(asrc.pull(*shared, catenae::span<float>(out))); // primes; fill > high water
        EXPECT_EQ(asrc.report(*shared).resyncs, 1u);
    }

    // The boundary: whole frames, a push within Push, a pull within the largest pull it serves.
    TEST(async_junction, ChecksTheCallersBuffers) {
        const asrc_1k            asrc{};
        auto                     shared = std::make_unique<asrc_1k::state>();
        const std::vector<float> odd(3);
        const std::vector<float> too_many_in(2 * 65);
        std::vector<float>       too_many_out(2 * 228);
        CATENAE_EXPECT_CHECK(catenae::check::input_not_whole_frames,
                             static_cast<void>(asrc.push(*shared, catenae::span<const float>(odd))));
        CATENAE_EXPECT_CHECK(catenae::check::push_too_large,
                             static_cast<void>(asrc.push(*shared, catenae::span<const float>(too_many_in))));
        CATENAE_EXPECT_CHECK(catenae::check::pull_too_large,
                             static_cast<void>(asrc.pull(*shared, catenae::span<float>(too_many_out))));
    }

    // The test's toy 1:3 is a stage, and the generic harness holds for it.
    TEST(async_junction, TheToyUpByThreeIsAStage) {
        const async_fit_fixture::up3_toy<2> up3{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(up3));
        EXPECT_TRUE(catenae_test::counts_are_exact(up3));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(up3, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(up3));
    }

} // namespace
