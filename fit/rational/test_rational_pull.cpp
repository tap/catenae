/// @file test_rational_pull.cpp
/// @brief The rational skeleton's chains pulled through a handoff by fed_by (CF, PLAN.md 6.1).
// Copyright 2026 Timothy Place and the catenae contributors
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/fed_by.h"
#include "catenae/handoff.h"
#include "catenae/span.h"
#include "catenae_test/harness.h"
#include "rational/chain.h"

// A named namespace: a junction of the skeleton's own, a handoff that records each pull.
namespace rational_pull_fixture {

    /// A handoff that records the size of each pull, the frames it gave and how many were real.
    template <typename Handoff, std::size_t Ch>
    struct recording : Handoff {
        static std::vector<std::size_t>& pulls() {
            static std::vector<std::size_t> log;
            return log;
        }
        static std::vector<float>& given() {
            static std::vector<float> log;
            return log;
        }
        static std::size_t& real() {
            static std::size_t count = 0;
            return count;
        }
        [[nodiscard]] std::size_t pull(typename Handoff::state& st, catenae::span<float> out) const noexcept {
            const std::size_t got = Handoff::pull(st, out);
            pulls().push_back(out.size() / Ch);
            given().insert(given().end(), out.begin(), out.end());
            real() += got;
            return got;
        }
    };

} // namespace rational_pull_fixture

namespace catenae {
    template <typename Handoff, std::size_t Ch>
    struct is_junction<rational_pull_fixture::recording<Handoff, Ch>> : std::true_type {};
} // namespace catenae

namespace {

    namespace rat = catenae_fit::rational;
    using rational_pull_fixture::recording;

    using chain31 = rat::up2_ratio3over2<float, 2>;
    using chain16 = rat::down3_down2<float, 2>;
    using conv32  = rat::converter<float, 2, rat::ratio3over2>;

    // The largest pull a request can need: the request plus the chain's shortfall and lead, at its
    // input rate.
    static_assert(catenae::fed_by_workspace<conv32, 64>::largest_pull == 43, "ceil((64 + 1/2) x 2/3)");
    static_assert(catenae::fed_by_workspace<chain31, 64>::largest_pull == 22, "ceil(64 / 3)");
    static_assert(catenae::fed_by_state<conv32>::holdover_frames == 1, "one frame in writes at most 2");
    static_assert(catenae::fed_by_state<chain31>::holdover_frames == 2, "one frame in writes 3");
    // A handoff serves it when its target is at least the pull plus its push, less one.
    static_assert(catenae::handoff<float, 2, 128, 74, 32>::serves(43, 1), "74 + 1 >= 43 + 32");
    static_assert(!catenae::handoff<float, 2, 128, 73, 32>::serves(43, 1), "73 + 1 < 43 + 32");

    /// What a run of requests saw.
    struct run_record {
        std::vector<float>                    out;     ///< every request's frames, end to end
        std::vector<std::vector<std::size_t>> pulls;   ///< the pulls each request made
        std::vector<float>                    given;   ///< every frame the junction gave, end to end
        std::size_t                           real;    ///< how many of them were real
        std::size_t                           starved; ///< fed_by's count of silent frames
        catenae::handoff_report               report;  ///< the handoff's, at the end
        std::vector<float>                    tail;    ///< the end of the stream, drained by hand
    };

    /// The end of a pulled stream: what the holdover keeps, then the chain's flush on the state
    /// fed_by holds, one burst at a time.
    ///
    /// CF finding 6: fed_by has no end of stream. A chain behind it is flushed only by reaching
    /// into fed_by_state's members (its holdover, then its chain's state and fed_by_workspace's
    /// chain workspace), which is what this does; include/catenae/fed_by.h offers neither a flush
    /// nor a flush_remaining.
    template <typename C, std::size_t MaxRequest>
    std::vector<float> drain_by_hand(const C& c, catenae::fed_by_state<C>& st,
                                     catenae::fed_by_workspace<C, MaxRequest>& ws) {
        std::vector<float> tail(st.holdover.begin(), st.holdover.begin() + static_cast<std::ptrdiff_t>(st.held * 2));
        st.held = 0;
        std::vector<float> burst(C::max_outputs_for(1) * 2);
        while (c.flush_remaining(st.chain) > 0) {
            const std::size_t n = c.flush(st.chain, ws.chain, catenae::span<float>(burst));
            tail.insert(tail.end(), burst.begin(), burst.begin() + static_cast<std::ptrdiff_t>(n * 2));
        }
        return tail;
    }

    /// One clock: the producer pushes `Push` input frames every Push L / M output frames, and the
    /// consumer asks for each request's frames when that many output frames have passed (at a
    /// tie the producer first). Requests cycle through `sizes`, `count` of them.
    template <typename Link, std::size_t MaxRequest, typename C>
    run_record drive(const C& c, const std::vector<std::size_t>& sizes, std::size_t count) {
        constexpr std::size_t push   = Link::largest_push;
        constexpr std::size_t period = push * C::rate_ratio.up / C::rate_ratio.down;
        static_assert(period * C::rate_ratio.down == push * C::rate_ratio.up, "a push is whole output frames");
        Link::pulls().clear();
        Link::given().clear();
        Link::real() = 0;
        const Link link{};
        auto       link_state = std::make_unique<typename Link::state>();
        auto       st         = std::make_unique<catenae::fed_by_state<C>>();
        auto       ws         = std::make_unique<catenae::fed_by_workspace<C, MaxRequest>>();
        reset(c, *st);
        catenae_test::signal source(17);
        std::vector<float>   frames(push * 2);
        run_record           record{};
        std::size_t          next_push = period;
        std::size_t          due       = sizes[0];
        std::size_t          asked     = 0;
        while (asked < count) {
            if (next_push <= due) {
                for (float& v : frames) {
                    v = source.next<float>();
                }
                EXPECT_EQ(link.push(*link_state, catenae::span<const float>(frames)), push);
                next_push += period;
                continue;
            }
            const std::size_t  n = sizes[asked % sizes.size()];
            std::vector<float> part(n * 2);
            const std::size_t  before = Link::pulls().size();
            EXPECT_EQ(catenae::fed_by(link, *link_state, c, *st, *ws, catenae::span<float>(part)), n);
            record.out.insert(record.out.end(), part.begin(), part.end());
            record.pulls.emplace_back(Link::pulls().begin() + static_cast<std::ptrdiff_t>(before), Link::pulls().end());
            ++asked;
            due += sizes[asked % sizes.size()];
        }
        record.given   = Link::given();
        record.real    = Link::real();
        record.starved = st->starved;
        record.report  = link.report(*link_state);
        record.tail    = drain_by_hand(c, *st, *ws);
        return record;
    }

    /// The chain run straight over every frame the junction gave and then its tail of zeros, in
    /// one call, from reset.
    template <typename C>
    std::vector<float> straight(std::vector<float> in) {
        constexpr C c = rat::make<C>();
        in.resize(in.size() + c.practical_tail_frames() * 2, 0.0f);
        auto m = std::make_unique<catenae::memory_of<C>>();
        c.reset(m->state);
        std::vector<float> out(C::max_outputs_for(in.size() / 2) * 2);
        out.resize(c.process(*m, catenae::span<const float>(in), catenae::span<float>(out)) * 2);
        return out;
    }

    /// The requests' frames and then the drained tail: the whole stream the pull side wrote.
    std::vector<float> whole_stream(const run_record& rec) {
        std::vector<float> all = rec.out;
        all.insert(all.end(), rec.tail.begin(), rec.tail.end());
        return all;
    }

    // PLAN.md 6.1's case: a 3/2 chain at 64-frame requests behind a handoff. Each request is
    // filled exactly with one pull, of 43, 42, 43 frames in turn (one pre-advance: the stage writes
    // on a group's first frame); the handoff never runs dry once it pays out; and what the
    // requests delivered is what the chain writes straight over the frames it was given.
    TEST(rational_pull, ThreeOverTwoPullsVaryAndEachRequestIsExact) {
        using link_t              = recording<catenae::handoff<float, 2, 128, 80, 32>, 2>;
        constexpr conv32  c       = rat::make<conv32>();
        const run_record  rec     = drive<link_t, 64>(c, {64}, 48);
        const std::size_t cycle[] = {43, 42, 43};
        for (std::size_t i = 0; i < rec.pulls.size(); ++i) {
            ASSERT_EQ(rec.pulls[i].size(), 1u) << "request " << i;
            EXPECT_EQ(rec.pulls[i][0], cycle[i % 3]) << "request " << i;
        }
        EXPECT_EQ(rec.report.underruns, 0u);
        EXPECT_EQ(rec.report.overruns, 0u);
        EXPECT_EQ(rec.report.running, 1u);
        EXPECT_EQ(rec.starved, rec.given.size() / 2 - rec.real) << "the frames given as silence while it filled";
        EXPECT_GT(rec.starved, 0u);
        ASSERT_EQ(rec.out.size(), 48u * 64 * 2);
        const std::vector<float> all  = whole_stream(rec);
        const std::vector<float> owed = straight<conv32>(rec.given);
        ASSERT_EQ(all.size(), owed.size()) << "the requests and the drained tail are the padded stream";
        EXPECT_EQ(std::memcmp(all.data(), owed.data(), all.size() * sizeof(float)), 0);
    }

    // The named chain by 3 (up2 then 3/2) at requests of varying size: a request its holdover
    // covers makes no pull, the others one each, of sizes that vary; never a dry handoff; the
    // output is the straight run's.
    TEST(rational_pull, AHoldoverCoversSomeRequestsAndNoneUnderruns) {
        using link_t               = recording<catenae::handoff<float, 2, 64, 40, 16>, 2>;
        constexpr std::size_t most = catenae::fed_by_workspace<chain31, 64>::largest_pull;
        constexpr chain31     c    = rat::make<chain31>();
        const run_record      rec  = drive<link_t, 64>(c, {64, 1, 2, 30, 64, 5, 1, 17}, 64);
        std::size_t           none = 0;
        std::set<std::size_t> sizes;
        for (const auto& made : rec.pulls) {
            ASSERT_LE(made.size(), 1u);
            none += made.empty() ? 1u : 0u;
            for (const std::size_t n : made) {
                sizes.insert(n);
                EXPECT_LE(n, most);
            }
        }
        EXPECT_GT(none, 0u) << "a request the holdover covers pulls nothing";
        EXPECT_GE(sizes.size(), 4u) << "pulls of varying size";
        EXPECT_EQ(rec.report.underruns, 0u);
        EXPECT_EQ(rec.report.overruns, 0u);
        EXPECT_EQ(rec.starved, rec.given.size() / 2 - rec.real);
        const std::vector<float> all  = whole_stream(rec);
        const std::vector<float> owed = straight<chain31>(rec.given);
        ASSERT_EQ(all.size(), owed.size());
        EXPECT_EQ(std::memcmp(all.data(), owed.data(), all.size() * sizeof(float)), 0);
    }

    // CF finding 7: fed_by's largest pull is the request plus the chain's shortfall and lead at
    // its input rate (include/catenae/fed_by.h, detail::most_frames_in), but a chain whose stages
    // write a group on its first frame never needs the lead: it is ahead exactly when it writes.
    // Down by 6 at 64-frame requests the staging and the handoff's target are sized for 389
    // frames, and no state needs more than 384. Counted here from every position of the group.
    TEST(rational_pull, TheLargestPullCountsALeadTheChainNeverNeeds) {
        static_assert(catenae::fed_by_workspace<chain16, 64>::largest_pull == 389, "(64 + 5/6) x 6");
        constexpr chain16  c = rat::make<chain16>();
        auto               m = std::make_unique<catenae::memory_of<chain16>>();
        std::vector<float> one(2, 0.5f);
        std::vector<float> out(chain16::max_outputs_for(1) * 2);
        c.reset(m->state);
        std::size_t most = 0;
        for (std::size_t at = 0; at < 2 * chain16::input_group; ++at) {
            most = std::max(most, c.frames_needed(m->state, 64));
            static_cast<void>(c.process(*m, catenae::span<const float>(one), catenae::span<float>(out)));
        }
        EXPECT_EQ(most, 384u);
    }

} // namespace
