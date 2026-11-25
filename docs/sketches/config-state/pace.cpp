// Copyright 2026 Timothy Place and the catenae contributors
// The numbers a chain publishes about how its output arrives, checked against what the real
// stages do: input_group, max_shortfall and max_lead by counting, steady<> by comparing its
// output with one straight run delayed by its stated latency, and the pull side by comparing
// with one straight run. Audit checks are on. Throwaway; run by verify.sh.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sketch.h"

[[noreturn]] void catenae::check_failed(int code) noexcept {
    std::printf("check %d failed\n", code);
    std::abort();
}

using namespace catenae;

struct same {
    float operator()(float x) const noexcept { return x; }
};
template <std::size_t Hop>
using block = blockwise<Hop, same>;

static unsigned s_seed = 12345;
static std::size_t pick(std::size_t most) noexcept { // 1..most
    s_seed = s_seed * 1664525U + 1013904223U;
    return 1 + (s_seed >> 8) % most;
}

static int s_failures = 0;
static void fail(const char* name, const char* what) noexcept {
    std::printf("FAILED  %s: %s\n", name, what);
    ++s_failures;
}

constexpr std::size_t k_frames = 6000;
static std::array<float, k_frames>     s_in;
static std::array<float, k_frames * 4> s_straight;
static std::array<float, k_frames * 4> s_pieces;

/// Counts: over every N up to k_frames, how far the output trails and leads N L/M; and the
/// smallest n whose multiples always come back exact. Against what the chain publishes.
template <typename Chain>
void counts(const char* name, const Chain& c) noexcept {
    static memory_of<Chain> s_memory;
    c.reset(s_memory.state);
    const ratio r = Chain::rate_ratio;
    std::size_t out = 0;
    ratio       trail{0, 1};
    ratio       lead{0, 1};
    static std::array<std::size_t, 2048> s_made;
    for (std::size_t n = 1; n <= 2047; ++n) {
        out += c.process(s_memory, span<const float>(s_in.data(), 1), span<float>(s_pieces));
        s_made[n] = out;
        // out against n * up / down, in units of 1/down
        const std::size_t owed = n * r.up;
        const std::size_t have = out * r.down;
        if (have < owed && (owed - have) * trail.down > trail.up * r.down) {
            trail = ratio{owed - have, r.down};
        }
        if (have > owed && (have - owed) * lead.down > lead.up * r.down) {
            lead = ratio{have - owed, r.down};
        }
    }
    std::size_t group = 1;
    for (;; ++group) {
        bool exact = true;
        for (std::size_t n = group; n <= 2047 && exact; n += group) {
            exact = s_made[n] * r.down == n * r.up;
        }
        if (exact) {
            break;
        }
    }
    const ratio s = Chain::max_shortfall;
    const ratio l = Chain::max_lead;
    const bool  s_ok = trail.up * s.down <= s.up * trail.down;
    const bool  l_ok = lead.up * l.down <= l.up * lead.down;
    std::printf("%-44s group %3zu (published %3zu)  trails %4zu/%zu (published %4zu/%zu)  leads %zu/%zu (published %zu/%zu)\n", name, group,
                Chain::input_group, trail.up, trail.down, s.up, s.down, lead.up, lead.down, l.up, l.down);
    if (!s_ok) {
        fail(name, "max_shortfall is too small");
    }
    if (!l_ok) {
        fail(name, "max_lead is too small");
    }
    if (Chain::input_group % group != 0) {
        fail(name, "input_group is not a multiple of the true group");
    }
}

/// steady<> over a 1:1 chain: every frame is the straight run's, delayed by the stated latency.
template <typename Inner>
void steadied(const char* name, const Inner& inner) noexcept {
    const auto wrapped = chain(steady{inner});
    const auto bare    = chain(inner);
    static memory_of<decltype(wrapped)> s_memory;
    static memory_of<decltype(bare)>    s_bare;
    bare.reset(s_bare.state);
    const std::size_t straight = bare.process(s_bare, span<const float>(s_in), span<float>(s_straight));
    const std::size_t latency  = steady<Inner>::added_latency_frames;
    std::size_t       wrong    = 0;
    for (int pattern = 0; pattern < 9; ++pattern) {
        static constexpr std::size_t sizes[] = {1, 2, 3, 7, 37, 63, 64, 65, 0};
        wrapped.reset(s_memory.state);
        std::size_t at = 0;
        bool        first = true;
        while (at < k_frames - 200) {
            std::size_t n = sizes[pattern] == 0 ? pick(150) : sizes[pattern];
            if (first && pattern % 2 == 0) {
                n = 1; // one odd call, then the pattern
            }
            first = false;
            static_cast<void>(wrapped.process(s_memory, span<const float>(s_in.data() + at, n), span<float>(s_pieces.data(), n)));
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t position = at + i;
                float             expected = 0.0f;
                bool              known    = true;
                if (position >= latency) {
                    known    = position - latency < straight;
                    expected = known ? s_straight[position - latency] : 0.0f;
                }
                wrong += !known || std::memcmp(&s_pieces[i], &expected, sizeof expected) != 0 ? 1U : 0U;
            }
            at += n;
        }
    }
    std::printf("%-44s steady<>: latency %4zu, %zu frames wrong over 9 call patterns\n", name, latency, wrong);
    if (wrong != 0) {
        fail(name, "steady<> returned wrong audio");
    }
}

/// A source that never runs dry: a ramp, with nothing to say about targets.
struct ramp_source {
    struct state {
        std::size_t next;
        std::size_t pulls;
        std::size_t largest;
    };
    static constexpr bool serves(std::size_t /*largest_pull*/, std::size_t /*quantum*/) noexcept { return true; }
    [[nodiscard]] std::size_t pull(state& st, span<float> out) const noexcept {
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = s_in[st.next + i];
        }
        st.next += out.size();
        st.pulls += 1;
        st.largest = std::max(st.largest, out.size());
        return out.size();
    }
};

/// The pull side: random requests give exactly what was asked, bit-equal to one straight run,
/// in at most one pull each, none larger than the staging.
template <typename Chain>
void pulled(const char* name, const Chain& c) noexcept {
    constexpr std::size_t k_most = 300;
    static memory_of<Chain>                  s_bare;
    static fed_by_state<Chain>               s_state;
    static fed_by_workspace<Chain, k_most>   s_work;
    static ramp_source::state                s_source;
    c.reset(s_bare.state);
    const std::size_t straight = c.process(s_bare, span<const float>(s_in.data(), k_frames / 2), span<float>(s_straight));
    reset(c, s_state);
    s_source = ramp_source::state{0, 0, 0};
    std::size_t done     = 0;
    std::size_t wrong    = 0;
    std::size_t requests = 0;
    while (done + k_most < straight && s_source.next + fed_by_workspace<Chain, k_most>::k_max_pull < k_frames / 2) {
        const std::size_t want   = pick(k_most);
        const std::size_t before = s_source.pulls;
        const std::size_t got    = fed_by(ramp_source{}, s_source, c, s_state, s_work, span<float>(s_pieces.data(), want));
        wrong += got == want && std::memcmp(s_pieces.data(), s_straight.data() + done, want * sizeof(float)) == 0 ? 0U : 1U;
        wrong += s_source.pulls - before > 1 ? 1U : 0U;
        done += want;
        ++requests;
    }
    std::printf("%-44s pull side: %4zu requests, %zu wrong; largest pull %zu of a staging of %zu\n", name, requests, wrong,
                s_source.largest, fed_by_workspace<Chain, k_most>::k_max_pull);
    if (wrong != 0 || s_source.largest > fed_by_workspace<Chain, k_most>::k_max_pull) {
        fail(name, "the pull side");
    }
}

template <typename Chain>
void all(const char* name, const Chain& c) noexcept {
    counts(name, c);
    pulled(name, c);
    if constexpr (Chain::rate_ratio == (ratio{1, 1})) {
        steadied(name, c);
    }
}

int main() {
    for (std::size_t i = 0; i < k_frames; ++i) {
        s_in[i] = static_cast<float>(i + 1);
    }
    constexpr up2<1>        up{};
    constexpr down2<1>      down{};      // leads
    constexpr down2_late<1> late{};      // trails
    constexpr map<1, same>  through{same{}};
    all("down2 | up2", down | up);
    all("down2_late | up2", late | up);
    all("up2 | down2", up | down);
    all("up2 | down2_late", up | late);
    all("down2 | down2 | up2 | up2", down | down | up | up);
    all("down2_late | down2 | up2 | up2", late | down | up | up);
    all("down2 | down2_late | up2 | up2", down | late | up | up);
    all("down2 | up2 | down2_late | up2", down | up | late | up);
    all("down2 | block 5 | up2", down | block<5>{same{}} | up);
    all("down2_late | block 96 | up2", late | block<96>{same{}} | up);
    all("block 3 | down2 | up2", block<3>{same{}} | down | up);
    all("down2 | up2 | block 3", down | up | block<3>{same{}});
    all("up2 | block 5 | down2", up | block<5>{same{}} | down);
    all("up2 | block 6 | down2_late", up | block<6>{same{}} | late);
    all("block 96 | block 128", block<96>{same{}} | block<128>{same{}});
    all("block 7 | block 5", block<7>{same{}} | block<5>{same{}});
    all("block 12 | block 8 | block 12", block<12>{same{}} | block<8>{same{}} | block<12>{same{}});
    all("down2 | block 4 | up2 | block 6", down | block<4>{same{}} | up | block<6>{same{}});
    all("a chain inside a chain: (down2 | up2), block 3", chain(down | up, block<3>{same{}}));
    all("a chain inside a chain: block 6, (block 4 | map)", chain(block<6>{same{}}, block<4>{same{}} | through));
    all("steady{down2 | up2} | block 5", steady{down | up} | block<5>{same{}});
    all("up2 | up2", up | up);
    all("down2 | down2", down | down);
    all("down2_late | down2", late | down);
    all("down2 | down2_late", down | late);
    all("down2 | block 3", down | block<3>{same{}});
    all("block 5 | down2", block<5>{same{}} | down);
    all("block 5 | down2_late | up2 | up2", block<5>{same{}} | late | up | up);
    all("up2 | block 3 | up2", up | block<3>{same{}} | up);
    all("down2 | down2 | down2 | up2", down | down | down | up);
    std::printf("%d failures\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
