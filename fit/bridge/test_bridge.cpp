/// @file test_bridge.cpp
/// @brief CF: bridge's 44.1 -> 48 kHz machine as one catenae stage, with its table design.
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge/bridge.h"
#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/expected.h"
#include "catenae/gain.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/table_view.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

namespace {

    namespace br = catenae_fit::bridge;

    constexpr std::size_t k_taps = 8;
    using design_t               = br::design<float, k_taps>;
    using table_t                = design_t::table_type;
    using mono_t                 = br::bridge_to_48k<1, k_taps>;
    using stereo_t               = br::bridge_to_48k<2, k_taps>;
    using strip_t                = catenae::chain<stereo_t, catenae::gain<2>>;

    /// A spec 8 taps can meet: Kaiser's estimate reaches 44.5 dB at a 10 kHz passband.
    constexpr br::spec k_spec{10000.0, 40.0};

    /// A ROM table the compiler computes, with no libm: a triangle centred on the prototype's
    /// middle, so linear phase like make()'s, each branch normalised to 1.
    constexpr table_t make_rom() noexcept {
        table_t                    rom{};
        constexpr double           centre     = static_cast<double>(br::phases * k_taps - 1) / 2.0;
        constexpr double           half_width = 1.5 * static_cast<double>(br::phases);
        std::array<double, k_taps> row{};
        for (std::size_t p = 0; p < design_t::stored_rows; ++p) {
            double sum = 0.0;
            for (std::size_t k = 0; k < k_taps; ++k) {
                const double j      = static_cast<double>(k * br::phases + p);
                const double d      = j > centre ? j - centre : centre - j;
                row[k_taps - 1 - k] = d < half_width ? 1.0 - d / half_width : 0.0;
                sum += row[k_taps - 1 - k];
            }
            for (std::size_t t = 0; t < k_taps; ++t) {
                rom[p * k_taps + t] = static_cast<float>(row[t] / sum);
            }
        }
        return rom;
    }
    constexpr table_t k_rom = make_rom();

    /// The prototype h[k L + p] a stored table stands for, its mirrored half rebuilt from the
    /// symmetry h[j] = h[L T - 1 - j].
    std::vector<float> prototype_of(catenae::span<const float> table) {
        std::vector<float> h(br::phases * k_taps);
        for (std::size_t p = 0; p < br::phases; ++p) {
            for (std::size_t k = 0; k < k_taps; ++k) {
                h[k * br::phases + p] = p < design_t::stored_rows ? table[p * k_taps + (k_taps - 1 - k)]
                                                                  : table[(br::phases - 1 - p) * k_taps + k];
            }
        }
        return h;
    }

    /// upfirdn(h, x, up = 160, down = 147) from x[<0] = 0, by its definition: output n is
    /// sum_i x[i] h[n M - i L] over the input frames whose term falls inside h, oldest first.
    template <typename A>
    std::vector<A> upfirdn(const std::vector<float>& h, const std::vector<float>& x, std::size_t channels,
                           std::size_t outputs) {
        const std::size_t length = h.size();
        std::vector<A>    y(outputs * channels);
        for (std::size_t n = 0; n < outputs; ++n) {
            const std::size_t at    = n * br::decimation;
            const std::size_t first = at + 1 > length ? (at + 1 - length + br::phases - 1) / br::phases : 0;
            for (std::size_t c = 0; c < channels; ++c) {
                A acc{};
                for (std::size_t i = first; i * br::phases <= at; ++i) {
                    if constexpr (std::is_same<A, float>::value) {
                        acc += h[at - i * br::phases] * x[i * channels + c];
                    }
                    else {
                        acc += static_cast<A>(h[at - i * br::phases]) * static_cast<A>(x[i * channels + c]);
                    }
                }
                y[n * channels + c] = acc;
            }
        }
        return y;
    }

    /// Input of `frames` frames of `channels`, from the harness's signal.
    std::vector<float> noise(std::size_t frames, std::size_t channels, std::uint32_t seed) {
        catenae_test::signal s(seed);
        std::vector<float>   x(frames * channels);
        for (float& v : x) {
            v = s.next<float>();
        }
        return x;
    }

    /// The storage a design is made into: vectors that outlive the test's stages.
    struct made_table {
        std::vector<float>  table   = std::vector<float>(design_t::table_size());
        std::vector<double> scratch = std::vector<double>(design_t::workspace_size());
    };

    /// What a stage writes for `in`, fed in calls of the sizes given.
    template <typename Stage>
    std::vector<float> run_in(const Stage& stage, const std::vector<float>& in, const std::vector<std::size_t>& sizes) {
        catenae_test::driver<Stage> d(stage, 0);
        std::vector<float>          out;
        std::size_t                 at = 0;
        for (const std::size_t n : sizes) {
            d.run(in, at, n, out);
            at += n;
        }
        return out;
    }

    bool bit_equal(const std::vector<float>& a, const std::vector<float>& b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }

    TEST(fit_bridge, TheTableDesignsSizesAreConstants) {
        static_assert(design_t::table_size() == 80 * k_taps, "the stored half: 80 rows of Taps");
        static_assert(design_t::workspace_size() == 160 * k_taps, "the whole prototype, in double");
        static_assert(sizeof(table_t) == design_t::table_size() * sizeof(float), "a ROM table is the storage");
        // D54: the table is large, the configuration a view of it.
        static_assert(sizeof(table_t) > catenae::max_configuration_bytes, "2,560 bytes of table");
        static_assert(sizeof(stereo_t) == sizeof(const float*), "the configuration is one pointer");
        static_assert(sizeof(strip_t) <= 4 * sizeof(const float*), "and the strip's is a few words");
        static_assert(std::is_trivially_copyable<stereo_t>::value && std::is_trivially_copyable<design_t>::value,
                      "a view is trivially copyable");
        static_assert(!std::is_aggregate<design_t>::value && !std::is_default_constructible<design_t>::value,
                      "a design is a class: only make() and from_table() produce one (D37)");
        static_assert(catenae::stage_traits<stereo_t>::opted_in && catenae::stage_traits<strip_t>::opted_in,
                      "stage_traits accepts it, and the chain it joins");
        SUCCEED();
    }

    TEST(fit_bridge, MakeRejectsWhatItCannotMeetAndWritesNothing) {
        made_table m;
        std::fill(m.table.begin(), m.table.end(), 7.0f);
        std::vector<float>  short_table(design_t::table_size() - 1);
        std::vector<double> short_scratch(design_t::workspace_size() - 1);
        const double        nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(design_t::make(k_spec, short_table, m.scratch).error(), catenae::status::size_mismatch);
        EXPECT_EQ(design_t::make(k_spec, m.table, short_scratch).error(), catenae::status::size_mismatch);
        EXPECT_EQ(design_t::make(br::spec{nan, 40.0}, m.table, m.scratch).error(), catenae::status::invalid_argument);
        EXPECT_EQ(design_t::make(br::spec{10000.0, -1.0}, m.table, m.scratch).error(),
                  catenae::status::invalid_argument);
        EXPECT_EQ(design_t::make(br::spec{0.0, 40.0}, m.table, m.scratch).error(), catenae::status::invalid_argument);
        EXPECT_EQ(design_t::make(br::spec{24000.0, 40.0}, m.table, m.scratch).error(), catenae::status::out_of_range);
        // bridge's economy spec needs its 38 taps; 8 cannot reach 70 dB at an 18 kHz passband.
        EXPECT_EQ(design_t::make(br::spec{18000.0, 70.0}, m.table, m.scratch).error(), catenae::status::out_of_range);
        for (const float v : m.table) {
            ASSERT_EQ(v, 7.0f);
        }
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        EXPECT_EQ(made->table().data(), m.table.data());
        for (std::size_t p = 0; p < br::phases; ++p) {
            double sum = 0.0;
            for (const float c : made->row(p)) {
                sum += static_cast<double>(c);
            }
            EXPECT_NEAR(sum, 1.0, 1e-6) << p;
        }
    }

    TEST(fit_bridge, AViewTakingConstructorRefusesATemporary) {
        static_assert(std::is_constructible<stereo_t, const table_t&>::value, "a table that outlives the stage");
        static_assert(!std::is_constructible<stereo_t, table_t>::value, "a temporary table: refused");
        static_assert(!std::is_constructible<stereo_t, const table_t&&>::value, "an expiring one: refused");
        // A design is itself a view of the caller's storage, so a temporary design is safe.
        static_assert(std::is_constructible<stereo_t, design_t>::value, "a design is copied, not viewed");
        // make() writes through a span of the caller's storage, which a temporary cannot give;
        // so does bind().
        static_assert(!std::is_constructible<catenae::span<float>, table_t>::value, "no span over a temporary");
        static_assert(std::is_same<decltype(design_t::bind(std::declval<table_t&>())), design_t>::value, "storage");
        // CF finding F7, made: span<const T> accepts a temporary array, as std::span does, so
        // the stage takes a catenae::table_view, which does not: one constructor, no deleted twin.
        static_assert(std::is_constructible<catenae::span<const float>, table_t>::value, "span<const T> would dangle");
        static_assert(!std::is_constructible<design_t::view_type, table_t>::value, "a table_view does not");
        // CF finding F6, made: catenae::expected's accessors on a temporary give a copy
        // (CAT-EXPECTED-006), so a stage that viewed its design (rather than copying a view)
        // would refuse `stage{*design::make(...)}` by deleting its rvalue overload.
        using made_t = catenae::expected<design_t, catenae::status>;
        static_assert(std::is_same<decltype(*std::declval<made_t>()), design_t>::value, "a copy, from a temporary");
        static_assert(std::is_same<decltype(std::declval<made_t>().value()), design_t>::value, "the same");
        static_assert(std::is_same<decltype(*std::declval<made_t&>()), design_t&>::value, "a reference, from one kept");
        SUCCEED();
    }

    TEST(fit_bridge, AConfigurationThatHoldsAViewSaysSo) {
        // CF finding F8, made: D44 says a posted value owns what it refers to. A strip with a
        // bridge stage is a pointer to a table, trivially copyable; the design says it holds a
        // view, the stage and the chain compose it, and mailbox<strip_t> does not compile
        // (CAT-MAILBOX-004, pinned by tests/compile_fail/mailbox_holds_view.cpp). A design is
        // replaced through 6.3's acknowledgement instead.
        static_assert(std::is_trivially_copyable<strip_t>::value, "a view is trivially copyable");
        static_assert(catenae::holds_view<design_t>::value && catenae::holds_view<stereo_t>::value, "it says so");
        static_assert(catenae::holds_view<strip_t>::value, "and so does its chain");
        static_assert(!catenae::holds_view<catenae::chain<catenae::gain<2>>>::value, "a gain's does not");
        SUCCEED();
    }

    TEST(fit_bridge, OutputIsUpfirdnSampleForSample) {
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const stereo_t    stage{*made};
        const std::size_t frames  = 1500;
        const auto        x       = noise(frames, 2, 21);
        const std::size_t outputs = stereo_t::max_outputs_for(frames); // from reset: exactly this many
        const auto        h       = prototype_of(made->table());
        const auto        y       = upfirdn<float>(h, x, 2, outputs);
        const auto        y_f64   = upfirdn<double>(h, x, 2, outputs);
        // In one call, frame by frame, and at block sizes that do and do not divide the group.
        EXPECT_TRUE(bit_equal(run_in(stage, x, {frames}), y));
        EXPECT_TRUE(bit_equal(run_in(stage, x, std::vector<std::size_t>(frames, 1)), y));
        for (const std::size_t block : {7u, 64u, 146u, 147u, 148u, 480u}) {
            std::vector<std::size_t> sizes(frames / block, block);
            sizes.push_back(frames % block);
            EXPECT_TRUE(bit_equal(run_in(stage, x, sizes), y)) << block;
        }
        for (std::uint32_t seed = 30; seed < 34; ++seed) {
            EXPECT_TRUE(bit_equal(run_in(stage, x, catenae_test::chunks(frames, 300, seed)), y)) << seed;
        }
        // Through a chain's process, in its 64-frame blocks, at calls of 100.
        const auto                          strip = catenae::chain(stage);
        catenae::memory_of<decltype(strip)> memory{};
        strip.reset(memory.state);
        std::vector<float> through(outputs * 2);
        std::size_t        made_frames = 0;
        for (std::size_t at = 0; at < frames; at += 100) {
            const std::size_t n = std::min<std::size_t>(100, frames - at);
            made_frames +=
                strip.process(memory, catenae::span<const float>(x.data() + at * 2, n * 2),
                              catenae::span<float>(through.data() + made_frames * 2, (outputs - made_frames) * 2));
        }
        EXPECT_EQ(made_frames, outputs);
        EXPECT_TRUE(bit_equal(through, y));
        // Against the same sum in double: the float accumulation's error, nothing more.
        double worst = 0.0;
        for (std::size_t i = 0; i < y.size(); ++i) {
            worst = std::max(worst, std::fabs(static_cast<double>(y[i]) - y_f64[i]));
        }
        EXPECT_LT(worst, 1e-6);
    }

    TEST(fit_bridge, AnImpulseReadsThePrototypeEveryMthPoint) {
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const mono_t       stage{*made};
        std::vector<float> x(20, 0.0f);
        x[0]           = 1.0f;
        const auto out = run_in(stage, x, {20});
        const auto h   = prototype_of(made->table());
        ASSERT_EQ(out.size(), mono_t::max_outputs_for(20));
        for (std::size_t n = 0; n < out.size(); ++n) {
            const float expected = n * br::decimation < h.size() ? h[n * br::decimation] : 0.0f;
            EXPECT_EQ(std::memcmp(&out[n], &expected, sizeof expected), 0) << n;
        }
    }

    TEST(fit_bridge, CountsAreExactFromEveryPositionACallCanLeave) {
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const mono_t          stage{*made};
        const auto            x = noise(147 + 200, 1, 22);
        mono_t::state         st{};
        mono_t::workspace     ws{};
        std::vector<float>    out(mono_t::max_outputs_for(200));
        std::array<bool, 160> seen{};
        catenae::stage_traits<mono_t>::reset(stage, st);
        for (std::size_t from = 0; from < 147; ++from) {
            seen[st.position] = true;
            for (std::size_t n = 0; n <= 200; ++n) {
                mono_t::state     copy = st;
                const std::size_t wrote =
                    stage.run(copy, ws, catenae::span<const float>(x.data() + from, n), catenae::span<float>(out));
                ASSERT_EQ(wrote, mono_t::outputs_for(st, n)) << "position " << st.position << ", n " << n;
                ASSERT_LE(wrote, mono_t::max_outputs_for(n));
            }
            for (std::size_t k = 0; k <= mono_t::outputs_for(st, 200); ++k) {
                const std::size_t need = mono_t::frames_needed(st, k);
                ASSERT_GE(mono_t::outputs_for(st, need), k);
                ASSERT_TRUE(need == 0 || mono_t::outputs_for(st, need - 1) < k) << k;
            }
            static_cast<void>(
                stage.run(st, ws, catenae::span<const float>(x.data() + from, 1), catenae::span<float>(out)));
        }
        std::size_t positions = 0;
        for (const bool s : seen) {
            positions += s ? 1u : 0u;
        }
        EXPECT_EQ(positions, 147u); // every position a call can stop at: the other 13 are mid-burst
        EXPECT_EQ(st.position, 0u); // and 147 frames make a superblock
    }

    TEST(fit_bridge, ArrivalNumbersAreTheMachinesByCounting) {
        using traits = catenae::stage_traits<stereo_t>;
        static_assert(traits::rate_ratio == (catenae::ratio{160, 147}) && traits::input_group == 147
                          && traits::max_shortfall == (catenae::ratio{0, 1})
                          && traits::max_lead == (catenae::ratio{146, 147}) && traits::output_quantum == 1
                          && !traits::is_frame_synchronous && !traits::is_in_place && !traits::rests_at_zero,
                      "an output is written when the first frame it needs arrives (PLAN.md 12)");
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const stereo_t stage{*made};
        // Frame by frame from reset, O(N) = ceil(160 N / 147): never behind, at most 146/147 ahead.
        const auto counts = catenae_test::counts_by(stage, 1, 3 * 147);
        for (std::size_t n = 0; n < counts.size(); ++n) {
            ASSERT_EQ(counts[n], (n * 160 + 146) / 147) << n;
        }
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(stage, true));
        EXPECT_TRUE(catenae_test::reset_ignores_storage(stage));
        EXPECT_TRUE(catenae_test::counts_are_exact(stage));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(stage));
    }

    TEST(fit_bridge, LatencyIsThePrototypesCentre) {
        static_assert(catenae::stage_traits<stereo_t>::has_exact_latency && stereo_t::exact_latency.up == 1279
                          && stereo_t::exact_latency.down == 294,
                      "(160 x 8 - 1) / (2 x 147) output frames, a constant of the type");
        using six_t = br::bridge_to_48k<2, 6>;
        static_assert(six_t::exact_latency.up == 137 && six_t::exact_latency.down == 42,
                      "959/294, reduced: the tap count is the type's");
        static_assert(strip_t::has_exact_latency && strip_t::exact_latency == (catenae::ratio{1279, 294}),
                      "and a gain after it adds none");
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const stereo_t stage{*made};
        EXPECT_FLOAT_EQ(stage.latency().nominal, 1279.0f / 294.0f);
        EXPECT_FLOAT_EQ(stage.latency().deviation, 0.0f);
        // The prototype is symmetric about (L T - 1) / 2, which is what makes the delay exact.
        const auto h = prototype_of(made->table());
        for (std::size_t j = 0; j < h.size(); ++j) {
            ASSERT_EQ(std::memcmp(&h[j], &h[h.size() - 1 - j], sizeof(float)), 0) << j;
        }
    }

    TEST(fit_bridge, AfterTapsLessOneZerosTheOutputIsSilence) {
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const stereo_t stage{*made};
        EXPECT_EQ(stage.tail_frames(), k_taps - 1);
        EXPECT_EQ(stage.practical_tail_frames(), k_taps - 1);
        EXPECT_FLOAT_EQ(stage.flush_residue(), 0.0f);
        auto x = noise(300, 2, 23);
        x.resize((300 + k_taps - 1 + 200) * 2, 0.0f);
        std::vector<std::size_t> sizes = {300};
        sizes.insert(sizes.end(), k_taps - 1, 1u); // the tail, a frame at a time
        sizes.push_back(200);
        const auto        out  = run_in(stage, x, sizes);
        const std::size_t tail = stereo_t::max_outputs_for(300 + k_taps - 1);
        ASSERT_EQ(out.size(), stereo_t::max_outputs_for(300 + k_taps - 1 + 200) * 2);
        // The last frame the tail writes still holds the last real frame; everything after is 0.
        EXPECT_TRUE(out[(tail - 1) * 2] < 0.0f || out[(tail - 1) * 2] > 0.0f);
        for (std::size_t i = tail * 2; i < out.size(); ++i) {
            ASSERT_EQ(out[i], 0.0f) << i;
        }
    }

    TEST(fit_bridge, InAChainItProcessesAndFlushes) {
        made_table m;
        const auto made = design_t::make(k_spec, m.table, m.scratch);
        ASSERT_TRUE(made.has_value());
        const auto strip = stereo_t{*made} | catenae::gain<2>{0.5f, 0};
        static_assert(std::is_same<std::remove_const_t<decltype(strip)>, strip_t>::value, "bridge | gain");
        static_assert(strip_t::input_group == 147 && strip_t::max_lead == (catenae::ratio{146, 147}),
                      "the chain composes the stage's numbers");
        catenae::memory_of<strip_t> memory{};
        strip.reset(memory.state);
        const std::size_t         frames = 1000;
        const auto                x      = noise(frames, 2, 24);
        std::vector<float>        out;
        std::array<float, 70 * 2> block{}; // max_outputs_for(64)
        for (std::size_t at = 0; at < frames; at += 64) {
            const std::size_t n     = std::min<std::size_t>(64, frames - at);
            const std::size_t wrote = strip.process(memory, catenae::span<const float>(x.data() + at * 2, n * 2),
                                                    catenae::span<float>(block));
            out.insert(out.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(wrote * 2));
        }
        EXPECT_EQ(strip.flush_remaining(memory.state), k_taps - 1);
        std::array<float, 3 * 2> piece{}; // room for a burst (2 frames) and one more
        std::size_t              calls = 0;
        while (strip.flush_remaining(memory.state) > 0) {
            const std::size_t wrote = strip.flush(memory, catenae::span<float>(piece));
            out.insert(out.end(), piece.begin(), piece.begin() + static_cast<std::ptrdiff_t>(wrote * 2));
            ++calls;
        }
        EXPECT_GT(calls, 1u); // resumable: the tail did not fit one call
        // What zero-padding the input by the tail writes, at the gain's half.
        auto padded = x;
        padded.resize((frames + k_taps - 1) * 2, 0.0f);
        auto y = upfirdn<float>(prototype_of(made->table()), padded, 2, stereo_t::max_outputs_for(frames + k_taps - 1));
        for (float& v : y) {
            v *= 0.5f;
        }
        EXPECT_TRUE(bit_equal(out, y));
        // A host whose block is the group gets the compile-time entry: 147 frames in, 160 out.
        std::array<float, 147 * 2> fixed_in{};
        std::array<float, 160 * 2> fixed_out{};
        strip.reset(memory.state);
        for (int call = 0; call < 3; ++call) {
            EXPECT_EQ(strip.process(memory, fixed_in, fixed_out), 160u);
        }
    }

    // The ROM path: a table the compiler computed, so the configuration is a constant. A
    // designer may still run later and the stage take its design between calls (PLAN.md 9.2).
    CATENAE_CONSTINIT auto s_rom_strip = stereo_t{k_rom} | catenae::gain<2>{1.0f, 0};
    CATENAE_CONSTINIT catenae::memory_of<strip_t> s_rom_memory{};
    CATENAE_CONSTINIT std::array<float, design_t::table_size()> s_rom_retable{};
    CATENAE_CONSTINIT std::array<double, design_t::workspace_size()> s_rom_scratch{};

    TEST(fit_bridge, ARomTableConstantInitialisesItsChain) {
        static_assert(std::is_same<decltype(s_rom_strip), strip_t>::value, "the same strip type");
        s_rom_strip.reset(s_rom_memory.state);
        const auto         x = noise(400, 2, 25);
        std::vector<float> out(stereo_t::max_outputs_for(400) * 2);
        EXPECT_EQ(s_rom_strip.process(s_rom_memory, catenae::span<const float>(x), catenae::span<float>(out)),
                  stereo_t::max_outputs_for(400));
        EXPECT_TRUE(bit_equal(out, upfirdn<float>(prototype_of(catenae::span<const float>(k_rom)), x, 2,
                                                  stereo_t::max_outputs_for(400))));
        // A table made at run time, taken between calls: the ROM one was the placeholder.
        const auto made = design_t::make(k_spec, s_rom_retable, s_rom_scratch);
        ASSERT_TRUE(made.has_value());
        s_rom_strip.stage<0>().set_design(*made);
        s_rom_strip.reset(s_rom_memory.state);
        EXPECT_EQ(s_rom_strip.process(s_rom_memory, catenae::span<const float>(x), catenae::span<float>(out)),
                  stereo_t::max_outputs_for(400));
        EXPECT_TRUE(bit_equal(out, upfirdn<float>(prototype_of(made->table()), x, 2, stereo_t::max_outputs_for(400))));
    }

    TEST(fit_bridge, ABadRomTableMetAtRunTimeReachesTheHandler) {
        auto bad = std::make_unique<table_t>(k_rom);
        (*bad)[3] += 0.5f; // row 0 no longer sums to 1
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(design_t::from_table(*bad)));
        *bad      = k_rom;
        (*bad)[9] = std::numeric_limits<float>::quiet_NaN();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(stereo_t{*bad}));
        CATENAE_EXPECT_NO_CHECK(static_cast<void>(design_t::from_table(k_rom)));
    }

    // The make() path. The table's storage, the designer's workspace and the strip's memory are
    // plain storage, constant-initialised in .bss; and so is the configuration, bound to the
    // table's storage before make() fills it (CF finding F2, made: design::bind).
    CATENAE_CONSTINIT table_t s_made_table{};
    CATENAE_CONSTINIT std::array<double, design_t::workspace_size()> s_made_scratch{};
    CATENAE_CONSTINIT catenae::memory_of<strip_t> s_made_memory{};
    CATENAE_CONSTINIT auto s_made_strip = stereo_t{design_t::bind(s_made_table)} | catenae::gain<2>{1.0f, 0};

    TEST(fit_bridge, AMadeTableLeavesNothingToRunTimeButMake) {
        static_assert(std::is_same<decltype(s_made_strip), strip_t>::value, "the same strip type");
        EXPECT_EQ(s_made_strip.stage<0>().design().table().data(), s_made_table.data());
        const auto         x = noise(400, 2, 26);
        std::vector<float> out(stereo_t::max_outputs_for(400) * 2, 1.0f);
        // Bound and not yet made: an all-zero table, a filter that writes silence.
        s_made_strip.reset(s_made_memory.state);
        EXPECT_EQ(s_made_strip.process(s_made_memory, catenae::span<const float>(x), catenae::span<float>(out)),
                  stereo_t::max_outputs_for(400));
        for (const float v : out) {
            ASSERT_EQ(v, 0.0f);
        }
        // make() fills the bound storage in place; the configuration is not touched.
        const auto made = design_t::make(k_spec, s_made_table, s_made_scratch);
        ASSERT_TRUE(made.has_value());
        EXPECT_EQ(made->table().data(), s_made_table.data());
        s_made_strip.reset(s_made_memory.state);
        EXPECT_EQ(s_made_strip.process(s_made_memory, catenae::span<const float>(x), catenae::span<float>(out)),
                  stereo_t::max_outputs_for(400));
        EXPECT_TRUE(bit_equal(out, upfirdn<float>(prototype_of(made->table()), x, 2, stereo_t::max_outputs_for(400))));
    }

} // namespace
