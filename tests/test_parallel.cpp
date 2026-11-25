/// @file test_parallel.cpp
/// @brief parallel<>'s contract (include/catenae/parallel.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/interleave.h"
#include "catenae/parallel.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/harness.h"

// A named namespace: in an anonymous one, the members no test calls would be unneeded to clang.
namespace parallel_fixture {

    /// Stereo summed to mono: an arm that changes its channel count.
    struct sum_to_mono {
        using input_port  = catenae::port<float, 2>;
        using output_port = catenae::port<float, 1>;
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = 1;
        static constexpr bool           is_in_place       = false;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};
        static constexpr bool           rests_at_zero = true;
        struct state {};
        using workspace = catenae::no_workspace;
        std::size_t run(state&, workspace&, catenae::span<const float> in, catenae::span<float> out) const noexcept {
            const std::size_t frames = in.size() / 2;
            for (std::size_t f = 0; f < frames; ++f) {
                out[f] = in[2 * f] + in[2 * f + 1];
            }
            return frames;
        }
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        catenae::stage_latency       latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t                  tail_frames() const noexcept { return 0; }
        std::size_t                  practical_tail_frames() const noexcept { return 0; }
        float                        flush_residue() const noexcept { return 0.0f; }
    };

} // namespace parallel_fixture

namespace catenae {
    template <>
    struct is_stage<parallel_fixture::sum_to_mono> : std::true_type {};
} // namespace catenae

namespace {

    using catenae::align;
    using catenae::chain;
    using catenae::parallel;
    using catenae::port;
    using catenae::ratio;
    using catenae::operator|;
    using catenae_fixtures::up2_toy;
    using parallel_fixture::sum_to_mono;

    constexpr bool same_terms(ratio a, ratio b) noexcept {
        return a.up == b.up && a.down == b.down;
    }

    std::vector<float> ramp_input(std::size_t values, float step) {
        std::vector<float> in(values);
        for (std::size_t i = 0; i < values; ++i) {
            in[i] = static_cast<float>(i % 89) * step - 0.3f;
        }
        return in;
    }

    /// Runs a chain over interleaved `in` from reset in calls of `call` frames.
    template <typename C>
    std::vector<float> run_in_calls(const C& c, const std::vector<float>& in, std::size_t call) {
        constexpr std::size_t in_ch  = C::input_port::values_per_frame;
        constexpr std::size_t out_ch = C::output_port::values_per_frame;
        auto                  m      = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> out;
        const std::size_t  frames = in.size() / in_ch;
        for (std::size_t at = 0; at < frames; at += call) {
            const std::size_t  n = std::min(call, frames - at);
            std::vector<float> part(C::max_outputs_for(n) * out_ch);
            const std::size_t  made = c.process(*m, catenae::span<const float>(in.data() + at * in_ch, n * in_ch),
                                                catenae::span<float>(part));
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(made * out_ch));
        }
        return out;
    }

    /// Channels [first, first + count) of interleaved frames of `width` values.
    std::vector<float> channels(const std::vector<float>& v, std::size_t width, std::size_t first, std::size_t count) {
        std::vector<float> part;
        for (std::size_t f = 0; f < v.size() / width; ++f) {
            for (std::size_t c = 0; c < count; ++c) {
                part.push_back(v[f * width + first + c]);
            }
        }
        return part;
    }

    /// `v`, `frames` frames of `width` values later, after silence; as long as `v`.
    std::vector<float> delayed_by(const std::vector<float>& v, std::size_t width, std::size_t frames) {
        std::vector<float> moved(v.size(), 0.0f);
        for (std::size_t i = frames * width; i < v.size(); ++i) {
            moved[i] = v[i - frames * width];
        }
        return moved;
    }

    // Verifies CAT-PARALLEL-001.
    TEST(parallel, PortsAreTheArmsSideBySide) {
        using p = parallel<sum_to_mono, catenae::delay<3, 4>, align::by_caller>;
        static_assert(std::is_same<p::input_port, port<float, 5>>::value, "two channels then three");
        static_assert(std::is_same<p::output_port, port<float, 4>>::value, "one channel then three");
        static_assert(!p::is_in_place, "an arm that changes its count");
        static_assert(parallel<catenae::gain<2>, catenae::gain<1>>::is_in_place, "one port type, 1:1");

        // Each arm reads its own channels and writes its own, bit for bit as it would alone.
        constexpr auto split = chain(p{sum_to_mono{}, catenae::delay<3, 4>{}});
        const auto     in    = ramp_input(5 * 700, 0.01f);
        const auto     out   = run_in_calls(split, in, 37);
        const auto     a     = run_in_calls(chain(sum_to_mono{}), channels(in, 5, 0, 2), 700);
        const auto     b     = run_in_calls(chain(catenae::delay<3, 4>{}), channels(in, 5, 2, 3), 700);
        EXPECT_TRUE(catenae_test::bit_equal(channels(out, 4, 0, 1), a));
        EXPECT_TRUE(catenae_test::bit_equal(channels(out, 4, 1, 3), b));
    }

    // Verifies CAT-PARALLEL-002.
    TEST(parallel, ByTypeDelaysTheArmAhead) {
        using p = parallel<catenae::delay<1, 5>, catenae::gain<2>>;
        static_assert(std::is_same<decltype(parallel{catenae::delay<1, 5>{}, catenae::gain<2>{0.5f, 0}}), p>::value,
                      "parallel{a, b} aligns by type");
        static_assert(p::has_exact_latency && same_terms(p::exact_latency, ratio{5, 1}), "the larger, 5 frames");
        const p pair{catenae::delay<1, 5>{}, catenae::gain<2>{0.5f, 0}};
        EXPECT_EQ(pair.latency().nominal, 5.0f);
        EXPECT_EQ(pair.latency().deviation, 0.0f);

        // The gain, 5 frames ahead of the delay, waits 5 frames: each channel is its arm, aligned.
        const auto in  = ramp_input(3 * 500, 0.02f);
        const auto out = run_in_calls(chain(pair), in, 64);
        const auto a   = run_in_calls(chain(catenae::delay<1, 5>{}), channels(in, 3, 0, 1), 500);
        const auto b   = run_in_calls(chain(catenae::gain<2>{0.5f, 0}), channels(in, 3, 1, 2), 500);
        EXPECT_TRUE(catenae_test::bit_equal(channels(out, 3, 0, 1), a));
        EXPECT_TRUE(catenae_test::bit_equal(channels(out, 3, 1, 2), delayed_by(b, 2, 5)));

        // Arms that are chains, and the other arm ahead.
        constexpr auto slow = catenae::gain<1>{2.0f, 0} | catenae::delay<1, 9>{};
        constexpr auto both = parallel{catenae::delay<1, 2>{}, slow};
        static_assert(same_terms(decltype(both)::exact_latency, ratio{9, 1}), "the chain's 9");
        const auto in2 = ramp_input(2 * 300, 0.03f);
        const auto o2  = run_in_calls(chain(both), in2, 11);
        const auto a2  = run_in_calls(chain(catenae::delay<1, 2>{}), channels(in2, 2, 0, 1), 300);
        const auto b2  = run_in_calls(chain(slow), channels(in2, 2, 1, 1), 300);
        EXPECT_TRUE(catenae_test::bit_equal(channels(o2, 2, 0, 1), delayed_by(a2, 1, 7)));
        EXPECT_TRUE(catenae_test::bit_equal(channels(o2, 2, 1, 1), b2));
    }

    // Verifies CAT-PARALLEL-003.
    TEST(parallel, ByCallerInsertsNoDelay) {
        using p = parallel<catenae::delay<1, 5>, catenae::gain<1>, align::by_caller>;
        static_assert(!p::has_exact_latency && same_terms(p::exact_latency, ratio{0, 1}), "not exact");
        const p pair{catenae::delay<1, 5>{}, catenae::gain<1>{0.25f, 0}};
        EXPECT_EQ(pair.latency().nominal, 5.0f);
        EXPECT_EQ(pair.latency().deviation, 5.0f);
        const auto in  = ramp_input(2 * 400, 0.01f);
        const auto out = run_in_calls(chain(pair), in, 50);
        EXPECT_TRUE(catenae_test::bit_equal(channels(out, 2, 0, 1),
                                            run_in_calls(chain(catenae::delay<1, 5>{}), channels(in, 2, 0, 1), 400)));
        EXPECT_TRUE(catenae_test::bit_equal(
            channels(out, 2, 1, 1), run_in_calls(chain(catenae::gain<1>{0.25f, 0}), channels(in, 2, 1, 1), 400)));

        // Half-frame latencies, which by_type refuses, at a ratio of 2.
        using up = parallel<up2_toy<1>, up2_toy<2>, align::by_caller>;
        static_assert(same_terms(up::rate_ratio, ratio{2, 1}) && up::output_quantum == 2, "2:1, two at a time");
        const auto in2 = ramp_input(3 * 200, 0.02f);
        const auto o2  = run_in_calls(chain(up{up2_toy<1>{}, up2_toy<2>{}}), in2, 17);
        ASSERT_EQ(o2.size(), 3u * 400u);
        EXPECT_TRUE(catenae_test::bit_equal(channels(o2, 3, 0, 1),
                                            run_in_calls(chain(up2_toy<1>{}), channels(in2, 3, 0, 1), 200)));
        EXPECT_TRUE(catenae_test::bit_equal(channels(o2, 3, 1, 2),
                                            run_in_calls(chain(up2_toy<2>{}), channels(in2, 3, 1, 2), 200)));
    }

    // Verifies CAT-PARALLEL-004.
    TEST(parallel, CountsAndTails) {
        using p = parallel<catenae::delay<1, 2>, catenae::delay<1, 9>>;
        const p pair{catenae::delay<1, 2>{}, catenae::delay<1, 9>{}};
        EXPECT_EQ(p::outputs_for(p::state{}, 10), 10u);
        EXPECT_EQ(pair.tail_frames(), 9u); // 2 + the 7 it waits, against 9
        EXPECT_EQ(pair.practical_tail_frames(), 9u);
        EXPECT_EQ(pair.flush_residue(), 0.0f);
        using up = parallel<up2_toy<1>, up2_toy<1>, align::by_caller>;
        EXPECT_EQ(up::outputs_for(up::state{}, 10), 20u);
        EXPECT_EQ(up::frames_needed(up::state{}, 7), 4u);
        EXPECT_EQ(up::max_outputs_for(3), 6u);
    }

    // Verifies CAT-STAGE-009, CAT-STAGE-010, CAT-STAGE-011 and CAT-STAGE-012 for parallel<>.
    TEST(parallel, PassesTheGenericTests) {
        const parallel<catenae::delay<1, 3>, catenae::gain<2>> aligned{catenae::delay<1, 3>{},
                                                                       catenae::gain<2>{0.5f, 0}};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(aligned));
        EXPECT_TRUE(catenae_test::counts_are_exact(aligned));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(aligned, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(aligned));
        const parallel<up2_toy<1>, up2_toy<1>, align::by_caller> doubled{up2_toy<1>{}, up2_toy<1>{}};
        EXPECT_TRUE(catenae_test::counts_are_exact(doubled));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(doubled, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(doubled));
        const auto inside = catenae::gain<3>{0.5f, 0} | aligned | catenae::gain<3>{2.0f, 0};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(inside));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(inside));
    }

    // Verifies CAT-PARALLEL-001: planar ports, in and out.
    TEST(parallel, CarriesPlanarPorts) {
        // Interleaved in, planar out: each frame's values land in their channels.
        using to_planar = parallel<catenae::deinterleave<2>, catenae::deinterleave<1>>;
        static_assert(std::is_same<to_planar::output_port, port<float, 3, catenae::layout::planar>>::value, "planar");
        constexpr auto           apart = chain(to_planar{catenae::deinterleave<2>{}, catenae::deinterleave<1>{}});
        auto                     m     = std::make_unique<catenae::memory_of<decltype(apart)>>();
        const std::vector<float> in    = ramp_input(3 * 100, 0.01f);
        std::array<std::vector<float>, 3>         planes{};
        std::array<catenae::span<float>, 3>       out{};
        std::array<catenae::span<const float>, 3> back{};
        for (std::size_t c = 0; c < 3; ++c) {
            planes[c].assign(100, 0.0f);
            out[c]  = catenae::span<float>(planes[c]);
            back[c] = catenae::span<const float>(planes[c]);
        }
        apart.reset(m->state);
        ASSERT_EQ(apart.process(*m, catenae::span<const float>(in), out), 100u);
        for (std::size_t f = 0; f < 100; ++f) {
            for (std::size_t c = 0; c < 3; ++c) {
                ASSERT_TRUE(catenae::detail::same_bits(planes[c][f], in[f * 3 + c])) << f << ' ' << c;
            }
        }
        // Planar in, interleaved out: the round trip is the input.
        constexpr auto     together = chain(parallel{catenae::interleave<2>{}, catenae::interleave<1>{}});
        auto               m2       = std::make_unique<catenae::memory_of<decltype(together)>>();
        std::vector<float> rejoined(3 * 100, 0.0f);
        together.reset(m2->state);
        ASSERT_EQ(together.process(*m2, back, catenae::span<float>(rejoined)), 100u);
        EXPECT_TRUE(catenae_test::bit_equal(rejoined, in));
    }

} // namespace
