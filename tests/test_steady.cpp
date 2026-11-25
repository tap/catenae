/// @file test_steady.cpp
/// @brief steady<>'s contract (include/catenae/steady.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/check.h"
#include "catenae/gain.h"
#include "catenae/steady.h"
#include "catenae_fixtures/stages.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

namespace {

    using catenae::chain;
    using catenae::ratio;
    using catenae::steady;
    using catenae::operator|;
    using catenae_fixtures::block_stage;
    using catenae_fixtures::down2_first;
    using catenae_fixtures::down2_last;
    using catenae_fixtures::up2_toy;

    constexpr bool same_terms(ratio a, ratio b) noexcept {
        return a.up == b.up && a.down == b.down;
    }

    // The chains the plan names (C1): block sizes that do not nest, one that leads, one that trails.
    constexpr auto k_96_128  = block_stage<1, 96>{} | block_stage<1, 128>{};
    constexpr auto k_480_512 = block_stage<1, 480>{} | block_stage<1, 512>{};
    constexpr auto k_7_5     = block_stage<1, 7>{} | block_stage<1, 5>{};
    constexpr auto k_leads   = down2_first<1>{} | up2_toy<1>{};
    constexpr auto k_trails  = down2_last<1>{} | up2_toy<1>{};
    using nest_96_128        = std::remove_cv_t<decltype(k_96_128)>;
    using nest_480_512       = std::remove_cv_t<decltype(k_480_512)>;
    using nest_7_5           = std::remove_cv_t<decltype(k_7_5)>;
    using leads              = std::remove_cv_t<decltype(k_leads)>;
    using trails             = std::remove_cv_t<decltype(k_trails)>;

    /// A pattern of calls: the first call's frames, then the size every later call has.
    struct pattern {
        std::size_t first;
        std::size_t then;
    };

    /// Runs a chain over `in`, from reset, in calls of the pattern (the last call shorter).
    template <typename C>
    std::vector<float> run_pattern(const C& c, const std::vector<float>& in, pattern calls) {
        constexpr std::size_t in_ch  = C::input_port::values_per_frame;
        constexpr std::size_t out_ch = C::output_port::values_per_frame;
        auto                  m      = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> out;
        const std::size_t  frames = in.size() / in_ch;
        std::size_t        call   = calls.first;
        for (std::size_t at = 0; at < frames; at += call, call = calls.then) {
            const std::size_t  n = std::min(call, frames - at);
            std::vector<float> part(C::max_outputs_for(n) * out_ch);
            const std::size_t  made = c.process(*m, catenae::span<const float>(in.data() + at * in_ch, n * in_ch),
                                                catenae::span<float>(part));
            out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(made * out_ch));
        }
        return out;
    }

    std::vector<float> ramp_input(std::size_t values) {
        std::vector<float> in(values);
        for (std::size_t i = 0; i < values; ++i) {
            in[i] = static_cast<float>(i % 97) * 0.01f - 0.4f + 0.001f * static_cast<float>(i % 13);
        }
        return in;
    }

    /// The wrapped form's output is the bare form's after `delay` frames of silence, bit for
    /// bit, and n frames for n, in each pattern of calls.
    template <typename Inner>
    ::testing::AssertionResult reproduces_at_its_delay(const Inner& inner, std::size_t frames) {
        constexpr std::size_t ch       = Inner::output_port::values_per_frame;
        const auto            wrapped  = chain(steady<Inner>{inner});
        const std::size_t     delay    = steady<Inner>::added_latency_frames;
        const auto            in       = ramp_input(frames * Inner::input_port::values_per_frame);
        const auto            straight = run_pattern(chain(inner), in, pattern{frames, frames});
        for (const pattern calls :
             {pattern{37, 64}, pattern{1, 1}, pattern{64, 64}, pattern{5, 128}, pattern{63, 1000}}) {
            const auto out = run_pattern(wrapped, in, calls);
            if (out.size() != frames * ch) {
                return ::testing::AssertionFailure()
                       << "calls of " << calls.first << " then " << calls.then << ": " << out.size() / ch << " frames";
            }
            for (std::size_t i = 0; i < out.size(); ++i) {
                const std::size_t frame = i / ch;
                if (frame >= delay && i - delay * ch >= straight.size()) {
                    break; // beyond what the straight run wrote
                }
                const float owed = frame < delay ? 0.0f : straight[i - delay * ch];
                if (!catenae::detail::same_bits(out[i], owed)) {
                    return ::testing::AssertionFailure()
                           << "calls of " << calls.first << " then " << calls.then << ": value " << i << " differs";
                }
            }
        }
        return ::testing::AssertionSuccess();
    }

    // Verifies CAT-STEADY-001.
    TEST(steady, IsFrameSynchronousAroundAnyOneToOne) {
        using wrapped = steady<nest_96_128>;
        static_assert(wrapped::input_group == 1 && wrapped::output_quantum == 1
                          && same_terms(wrapped::max_shortfall, ratio{0, 1})
                          && same_terms(wrapped::max_lead, ratio{0, 1}),
                      "frame by frame, never behind or ahead");
        static_assert(catenae::stage_traits<wrapped>::is_frame_synchronous, "the traits derive it");
        static_assert(decltype(chain(wrapped{k_96_128}))::is_frame_synchronous, "and so does a chain of it");
        static_assert(!nest_96_128::is_frame_synchronous, "the bare form is not");
        static_assert(std::is_same<decltype(steady{block_stage<1, 8>{}}), steady<block_stage<1, 8>>>::value,
                      "steady{c} wraps c");
        EXPECT_TRUE(catenae_test::counts_are_exact(wrapped{k_96_128}));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(wrapped{k_96_128}, true));
    }

    // Verifies CAT-STEADY-002.
    TEST(steady, PrimesByTheShortfall) {
        static_assert(steady<block_stage<1, 128>>::added_latency_frames == 127, "a 128-frame block: 127");
        static_assert(steady<nest_96_128>::added_latency_frames == 191, "96 before 128: 191");
        static_assert(steady<nest_480_512>::added_latency_frames == 959, "480 before 512: 959");
        static_assert(steady<nest_7_5>::added_latency_frames == 10, "7 before 5: 10");
        static_assert(steady<trails>::added_latency_frames == 1, "a chain that trails by a frame: 1");
        static_assert(steady<leads>::added_latency_frames == 0, "a chain that only leads: none");
        SUCCEED();
    }

    // Verifies CAT-STEADY-002: block sizes that do not nest.
    TEST(steady, ReproducesNonNestingBlocksAtItsDelay) {
        EXPECT_TRUE(reproduces_at_its_delay(k_96_128, 3000));
        EXPECT_TRUE(reproduces_at_its_delay(k_480_512, 6000));
        EXPECT_TRUE(reproduces_at_its_delay(k_7_5, 1000));
        EXPECT_TRUE(reproduces_at_its_delay(block_stage<2, 128>{}, 1500));
    }

    // Verifies CAT-STEADY-002: a chain that leads and one that trails.
    TEST(steady, ReproducesLeadingAndTrailingChainsAtItsDelay) {
        EXPECT_TRUE(reproduces_at_its_delay(k_leads, 1001));
        EXPECT_TRUE(reproduces_at_its_delay(k_trails, 1001));
    }

    // Verifies CAT-STAGE-009, CAT-STAGE-010, CAT-STAGE-011 and CAT-STAGE-012 for steady<>.
    TEST(steady, PassesTheGenericTests) {
        const steady<block_stage<2, 24>> blocks{block_stage<2, 24>{}};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(blocks));
        EXPECT_TRUE(catenae_test::counts_are_exact(blocks));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(blocks, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(blocks));
        const steady<trails> trailing{k_trails};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(trailing));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(trailing));
        const auto in_a_chain = steady<nest_7_5>{k_7_5} | catenae::gain<1>{0.5f, 0};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(in_a_chain));
        EXPECT_TRUE(catenae_test::counts_are_exact(in_a_chain));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(in_a_chain));
    }

    // Verifies CAT-STEADY-003.
    TEST(steady, LatencyAndTailsAddThePriming) {
        using wrapped_block = steady<block_stage<1, 128>>;
        static_assert(wrapped_block::has_exact_latency && same_terms(wrapped_block::exact_latency, ratio{127, 1}),
                      "the block's 0 and 127 frames of priming");
        static_assert(same_terms(trails::exact_latency, ratio{3, 2}), "the bare chain's");
        static_assert(same_terms(steady<trails>::exact_latency, ratio{5, 2}), "and the priming's frame");
        const wrapped_block w{block_stage<1, 128>{}};
        EXPECT_EQ(w.latency().nominal, 127.0f);
        EXPECT_EQ(w.latency().deviation, 0.0f);
        EXPECT_EQ(w.tail_frames(), 127u + 127u);
        EXPECT_EQ(w.practical_tail_frames(), 127u + 127u);
        EXPECT_EQ(w.flush_residue(), 0.0f);

        // Fed its whole tail of zeros, the wrapped form has written everything the bare form
        // writes when fed its own: nothing is left in the FIFO.
        const std::vector<float> in      = ramp_input(300);
        std::vector<float>       padded  = in;
        std::vector<float>       wpadded = in;
        padded.resize(in.size() + block_stage<1, 128>{}.tail_frames(), 0.0f);
        wpadded.resize(in.size() + w.tail_frames(), 0.0f);
        const auto bare = run_pattern(chain(block_stage<1, 128>{}), padded, pattern{64, 64});
        const auto out  = run_pattern(chain(w), wpadded, pattern{64, 64});
        ASSERT_EQ(out.size(), wpadded.size());
        ASSERT_GE(out.size(), 127u + bare.size());
        for (std::size_t i = 0; i < bare.size(); ++i) {
            ASSERT_TRUE(catenae::detail::same_bits(out[127 + i], bare[i])) << i;
        }

        // A chain that leads writes a frame ahead, which waits in the FIFO: its tail is one frame
        // though the chain's own is none. 301 frames make 302; fed one zero, all 302 are out.
        const steady<leads> l{k_leads};
        EXPECT_EQ(k_leads.tail_frames(), 0u);
        EXPECT_EQ(l.tail_frames(), 1u);
        const std::vector<float> odd     = ramp_input(301);
        std::vector<float>       odd_fed = odd;
        odd_fed.resize(302, 0.0f);
        const auto ahead = run_pattern(chain(k_leads), odd, pattern{301, 301});
        const auto all   = run_pattern(chain(l), odd_fed, pattern{64, 64});
        ASSERT_EQ(ahead.size(), 302u);
        ASSERT_EQ(all.size(), 302u);
        for (std::size_t i = 0; i < ahead.size(); ++i) {
            ASSERT_TRUE(catenae::detail::same_bits(all[i], ahead[i])) << i;
        }
    }

    /// A 1:1 stage that says it never trails and does: the FIFO, primed by nothing, runs short.
    struct trails_unannounced : block_stage<1, 8> {
        static constexpr ratio max_shortfall{0, 1};
    };
    /// A 1:1 stage that says it never leads, and gains a frame on every call: the FIFO runs over.
    struct gains_a_frame : block_stage<1, 1> {
        std::size_t run(state& st, workspace& ws, catenae::span<const float> in,
                        catenae::span<float> out) const noexcept {
            const std::size_t made = block_stage<1, 1>::run(st, ws, in, out);
            out[made]              = made > 0 ? out[made - 1] : 0.0f;
            return made + 1;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n + 1; }
    };

} // namespace

namespace catenae {
    template <>
    struct is_stage<trails_unannounced> : std::true_type {};
    template <>
    struct is_stage<gains_a_frame> : std::true_type {};
} // namespace catenae

namespace {

    // Verifies CAT-STEADY-004.
    TEST(steady, TheFifoNeverRunsShortOrOver) {
        // Every pattern the reproduction tests run passes the audit-level checks...
        CATENAE_EXPECT_NO_CHECK(EXPECT_TRUE(reproduces_at_its_delay(k_96_128, 1000)));
        CATENAE_EXPECT_NO_CHECK(EXPECT_TRUE(reproduces_at_its_delay(k_leads, 301)));
        CATENAE_EXPECT_NO_CHECK(EXPECT_TRUE(reproduces_at_its_delay(k_trails, 301)));
        // ...and a configuration whose numbers are wrong is caught there. The memory is made
        // outside the trapped call, which leaves by a jump.
        if constexpr (catenae::detail::k_audit_level) {
            const std::vector<float> in = ramp_input(128);
            std::vector<float>       out(128);

            const auto short_one = chain(steady<trails_unannounced>{trails_unannounced{}});
            auto       short_mem = std::make_unique<catenae::memory_of<decltype(short_one)>>();
            short_one.reset(short_mem->state);
            CATENAE_EXPECT_CHECK(catenae::check::fifo_short, static_cast<void>(short_one.process(
                                                                 *short_mem, catenae::span<const float>(in.data(), 4),
                                                                 catenae::span<float>(out.data(), 4))));

            const auto over_one = chain(steady<gains_a_frame>{gains_a_frame{}});
            auto       over_mem = std::make_unique<catenae::memory_of<decltype(over_one)>>();
            over_one.reset(over_mem->state);
            CATENAE_EXPECT_CHECK(catenae::check::fifo_over,
                                 static_cast<void>(over_one.process(*over_mem, catenae::span<const float>(in),
                                                                    catenae::span<float>(out))));
        }
    }

} // namespace
