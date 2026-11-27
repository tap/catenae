/// @file test_tap.cpp
/// @brief tap's contract (include/catenae/tap.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "analyzer_support.h"
#include "catenae/chain.h"
#include "catenae/gain.h"
#include "catenae/peak_meter.h"
#include "catenae/rms_meter.h"
#include "catenae/tap.h"
#include "catenae_fixtures/edges.h"
#include "catenae_test/harness.h"

namespace {

    /// An analyzer that rests by its own reset: it counts frames from a start it sets.
    class counter {
      public:
        using input_port                           = catenae::port<float, 1>;
        using result_type                          = std::uint32_t;
        static constexpr std::size_t hop_frames    = 4;
        static constexpr std::size_t window_frames = 4;

        struct state {
            std::uint32_t frames;
            std::uint32_t ready;
        };

        void reset(state& st) const noexcept {
            st.frames = 1000;
            st.ready  = 0;
        }
        void analyze(state& st, catenae::span<const float> in) const noexcept {
            for (std::size_t i = 0; i < in.size(); ++i) {
                if (++st.frames % 4 == 0) {
                    ++st.ready;
                }
            }
        }
        std::uint32_t result(const state& st) const noexcept { return st.frames; }
        std::uint32_t results_ready(const state& st) const noexcept { return st.ready; }
    };

} // namespace

namespace catenae {
    template <>
    struct is_analyzer<counter> : std::true_type {};
} // namespace catenae

namespace {

    using rms_tap = catenae::tap<2, catenae::rms_meter<2, 32, 128>>;

    // Verifies CAT-TAP-001.
    TEST(tap, ItsOutputIsItsInput) {
        static_assert(std::is_same<rms_tap::input_port, catenae::port<float, 2>>::value
                          && std::is_same<rms_tap::output_port, catenae::port<float, 2>>::value,
                      "the analyzer's port, in and out");
        using traits = catenae::stage_traits<rms_tap>;
        static_assert(traits::opted_in && traits::is_frame_synchronous && traits::is_in_place, "1:1, in place");
        static_assert(traits::has_exact_latency && traits::exact_latency == (catenae::ratio{0, 1}), "no latency");
        static_assert(traits::rests_at_zero, "as the meter does");
        const rms_tap t{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(t));
        EXPECT_TRUE(catenae_test::counts_are_exact(t));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(t, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(t));
        // Out of place and in place, the input's bits.
        const std::vector<float> in = analyzer_test::noise<float>(2 * 300, 9);
        std::vector<float>       out(in.size());
        std::vector<float>       buffer = in;
        auto                     a      = std::make_unique<rms_tap::state>();
        auto                     b      = std::make_unique<rms_tap::state>();
        traits::reset(t, *a);
        traits::reset(t, *b);
        catenae::no_workspace ws{};
        EXPECT_EQ(t.run(*a, ws, catenae::span<const float>(in), catenae::span<float>(out)), 300u);
        EXPECT_EQ(t.run(*b, ws, catenae::span<const float>(buffer), catenae::span<float>(buffer)), 300u);
        EXPECT_EQ(std::memcmp(out.data(), in.data(), in.size() * sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(buffer.data(), in.data(), in.size() * sizeof(float)), 0);
        EXPECT_EQ(t.latency().nominal, 0.0f);
        EXPECT_EQ(t.tail_frames(), 0u);
    }

    // Verifies CAT-TAP-002.
    TEST(tap, ItsResultsAreItsAnalyzers) {
        // In a chain, after a gain: the meter's results are those of the gained signal, read
        // from the chain's state by stage_state<1>.
        constexpr auto strip = catenae::gain<2>{0.5f, 0} | rms_tap{};
        using strip_t        = std::remove_cv_t<decltype(strip)>;
        auto memory          = std::make_unique<catenae::memory_of<strip_t>>();
        strip.reset(memory->state);
        const std::vector<float> in = analyzer_test::noise<float>(2 * 1024, 13);
        std::vector<float>       halved(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            halved[i] = in[i] * 0.5f;
        }
        const auto         expected = analyzer_test::results_of(catenae::rms_meter<2, 32, 128>{}, halved, 32, 3);
        std::vector<float> out(in.size());
        std::uint32_t      seen = 0;
        std::size_t        next = 0;
        for (std::size_t at = 0; at < 1024; at += 32) {
            static_cast<void>(strip.process(*memory, catenae::span<const float>(in.data() + at * 2, 64),
                                            catenae::span<float>(out.data() + at * 2, 64)));
            const auto& meter_state = strip_t::stage_state<1>(memory->state);
            const auto& meter       = strip.stage<1>();
            if (meter.results_ready(meter_state) != seen) {
                seen = meter.results_ready(meter_state);
                ASSERT_LT(next, expected.size());
                EXPECT_TRUE(analyzer_test::same_bits(meter.result(meter_state), expected[next])) << "result " << next;
                ++next;
            }
        }
        EXPECT_EQ(next, expected.size());
        EXPECT_EQ(std::memcmp(out.data(), halved.data(), out.size() * sizeof(float)), 0);
        // An analyzer that rests by its own reset: the tap does too.
        using count_tap = catenae::tap<1, counter>;
        static_assert(!catenae::stage_traits<count_tap>::rests_at_zero, "the analyzer's reset");
        const count_tap       c{};
        count_tap::state      st{};
        catenae::no_workspace ws{};
        catenae::stage_traits<count_tap>::reset(c, st);
        EXPECT_EQ(c.result(st), 1000u);
        const std::vector<float> eight(8, 0.0f);
        std::vector<float>       sink(8);
        static_cast<void>(c.run(st, ws, catenae::span<const float>(eight), catenae::span<float>(sink)));
        EXPECT_EQ(c.result(st), 1008u);
        EXPECT_EQ(c.results_ready(st), 2u);
    }

    // Verifies CAT-TAP-001.
    TEST(tap, ItPassesAFixedPointPortThrough) {
        using t = catenae::tap<2, catenae::peak_meter_q31<2, 16>>;
        const t q{};
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(q));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(q));
        using fixture = catenae::tap<3, catenae_fixtures::peak_meter<3, 8>>;
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(fixture{}));
    }

} // namespace
