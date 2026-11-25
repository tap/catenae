// Copyright 2026 Timothy Place and the catenae contributors
// The sketch at work, on a host. Throwaway.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sketch.h"

[[noreturn]] void catenae::check_failed(int code) noexcept {
    std::printf("check %d failed\n", code);
    std::abort();
}

using namespace catenae;

struct soft_clip {
    float drive;
    float operator()(float x) const noexcept { return std::tanh(drive * x); }
};
struct halve {
    float operator()(float x) const noexcept { return 0.5f * x; }
};
struct same {
    float operator()(float x) const noexcept { return x; }
};

// ---- D34 -----------------------------------------------------------------------------------
// The configuration: a constant. The type is the topology; the object is 4 bytes (the drive).
constexpr auto overdrive = up2<2>{} | up2<2>{} | map<2, soft_clip>{soft_clip{4.0f}} | down2<2>{} | down2<2>{};

static_assert(decltype(overdrive)::rate_ratio == (ratio{1, 1}));
static_assert(decltype(overdrive)::input_group == 1); // up before down: every n comes back as n
static_assert(decltype(overdrive)::is_frame_synchronous);

// The memory. What a voice remembers is its state: 44 bytes each. What a call needs is the
// workspace: one for the thread, shared by both voices.
static state_of<decltype(overdrive)>     s_voice_a;
static state_of<decltype(overdrive)>     s_voice_b;
static workspace_of<decltype(overdrive)> s_overdrive_work;

// A state is plain storage: it can be reset in place, and handed to C.
static_assert(std::is_trivially_copyable_v<state_of<decltype(overdrive)>>);
static_assert(std::is_standard_layout_v<state_of<decltype(overdrive)>>);

// | flattens whichever side a chain is on.
constexpr auto up4   = up2<2>{} | up2<2>{};
constexpr auto down4 = down2<2>{} | down2<2>{};
static_assert(std::is_same_v<decltype(up4 | map<2, soft_clip>{soft_clip{4.0f}} | down4), std::remove_const_t<decltype(overdrive)>>);

// ---- D35 -----------------------------------------------------------------------------------
// A region that works on blocks of 128, as stft | gate | istft does.
// A stage used alone is a chain of one, which is what supplies the checked process().
using region_t               = blockwise<128, halve>;
constexpr auto region        = chain(region_t{halve{}});
constexpr auto region_steady = chain(steady{region_t{halve{}}});
static_assert(decltype(region)::input_group == 128);
static_assert(!decltype(region)::is_frame_synchronous);
static_assert(decltype(region_steady)::is_frame_synchronous);
static_assert(steady<region_t>::added_latency_frames == 127);

// Down, then up: 1:1, and NOT n for n. Its input group is a pair.
constexpr auto down_up = down2<1>{} | up2<1>{};
static_assert(decltype(down_up)::rate_ratio == (ratio{1, 1}));
static_assert(decltype(down_up)::input_group == 2);
static_assert(!decltype(down_up)::is_frame_synchronous);
static_assert(steady<decltype(down_up)>::added_latency_frames == 1);

// A low-pass at 0.1 of the sample rate, as a table a generator would write: no designer, no
// double and no libm are needed to use it. (An unstable table here would not compile.)
constexpr auto k_lowpass_at_0_1 = lowpass_design<float>::from_table(
    {0.06745527388907191f, 0.13491054777814382f, 0.06745527388907191f, -1.1429805025399011f, 0.4128015980961886f},
    2.176250899482823f);

// ---- helpers for the checks below ------------------------------------------------------------

/// Runs steady{inner} on a ramp in calls of `call` frames and counts the frames that are not
/// the ramp delayed by the stated latency.
template <typename Inner>
std::size_t wrong_frames(const Inner& inner, std::size_t call, std::size_t total) noexcept {
    static constexpr std::size_t k_most = 1024;
    const auto                   wrapped = chain(steady{inner});
    static memory_of<decltype(wrapped)> s_memory;
    wrapped.reset(s_memory.state);
    const std::size_t        latency = steady<Inner>::added_latency_frames;
    std::array<float, k_most> in{};
    std::array<float, k_most> out{};
    std::size_t               wrong = 0;
    for (std::size_t at = 0; at < total; at += call) {
        for (std::size_t i = 0; i < call; ++i) {
            in[i] = static_cast<float>(at + i + 1);
        }
        static_cast<void>(wrapped.process(s_memory, span<const float>(in.data(), call), span<float>(out.data(), call)));
        for (std::size_t i = 0; i < call; ++i) {
            const std::size_t position = at + i;
            const float       expected = position < latency ? 0.0f : static_cast<float>(position - latency + 1);
            wrong += (out[i] < expected || out[i] > expected) ? 1U : 0U;
        }
    }
    return wrong;
}

/// The harness test every stage and chain gets: reset over garbage and reset over zeros must
/// behave alike, bit for bit. The workspace is left as garbage both times.
template <typename Chain>
bool reset_is_complete(const Chain& chain) noexcept {
    static memory_of<Chain> s_one;
    static memory_of<Chain> s_two;
    std::memset(static_cast<void*>(&s_one), 0xA5, sizeof s_one);
    std::memset(static_cast<void*>(&s_two), 0, sizeof s_two);
    std::memset(static_cast<void*>(&s_two.workspace), 0x5A, sizeof s_two.workspace);
    chain.reset(s_one.state);
    chain.reset(s_two.state);
    constexpr std::size_t                      k_frames = 256;
    std::array<float, k_frames * 2>            in{};
    std::array<float, k_frames * 2 * 4>        out_one{};
    std::array<float, k_frames * 2 * 4>        out_two{};
    const std::size_t                          width = Chain::input_port::width;
    for (std::size_t i = 0; i < k_frames * width; ++i) {
        in[i] = static_cast<float>(i % 17) * 0.05f - 0.4f;
    }
    bool alike = true;
    for (int call = 0; call < 3; ++call) {
        const std::size_t one = chain.process(s_one, span<const float>(in.data(), k_frames * width), span<float>(out_one));
        const std::size_t two = chain.process(s_two, span<const float>(in.data(), k_frames * width), span<float>(out_two));
        alike = alike && one == two && std::memcmp(out_one.data(), out_two.data(), sizeof out_one) == 0;
    }
    return alike;
}

// A junction that counts the pulls made on it, to show that a request is one pull.
struct counted_link {
    using link_t = handoff<2, 1024, 64>;
    using state  = link_t::state;
    static inline std::size_t pulls   = 0;
    static inline std::size_t largest = 0;
    [[nodiscard]] std::size_t pull(state& st, span<float> out) const noexcept {
        ++pulls;
        largest = std::max(largest, out.size() / 2);
        return link_t{}.pull(st, out);
    }
};

int main() {
    // ---- D34: configuration, state, workspace ----------------------------------------------
    // Both arguments are arrays, so every size is checked by the compiler.
    overdrive.reset(s_voice_a); // once, before the first call
    overdrive.reset(s_voice_b);
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    in[0] = 0.25f;
    const std::size_t a = overdrive.process(s_voice_a, s_overdrive_work, in, out);
    const std::size_t b = overdrive.process(s_voice_b, s_overdrive_work, in, out);
    std::printf("D34  overdrive: %zu and %zu frames; configuration %zu bytes, state %zu bytes a voice, workspace %zu bytes a thread\n",
                a, b, sizeof(overdrive), sizeof(s_voice_a), sizeof(s_overdrive_work));

    // A parameter that changes at run time: the configuration is then an ordinary object, not a
    // constant. It is still only the parameters; the histories stay in the state.
    static auto           s_drive_chain = overdrive;                  // a copy that may change
    constexpr std::size_t k_clip        = 2;                          // the map is the third stage
    s_drive_chain.stage<k_clip>().function().drive = 9.0f;            // set it, between calls
    const std::size_t c = s_drive_chain.process(s_voice_a, s_overdrive_work, in, out); // the histories carry on
    std::printf("D34  drive is now %.1f: %zu frames from the same state\n",
                static_cast<double>(s_drive_chain.stage<k_clip>().function().drive), c);

    // ---- parameters that change at run time: a low-pass and a ramped gain -------------------
    // A fixed gain is just map<> with a multiplication.
    constexpr auto trim = chain(map<2, times>{times{0.5f}});
    static_cast<void>(trim);

    // A channel strip whose cutoff and level will change, so its configuration is a variable.
    // It starts from a design that is a table in ROM: no designer has run. There is one of it,
    // so its state and workspace are kept together.
    static auto                        s_strip = lowpass<2>{k_lowpass_at_0_1} | gain<2>{1.0f, 96};
    static memory_of<decltype(s_strip)> s_strip_memory;
    s_strip.reset(s_strip_memory.state); // the gain starts at its level, 1.0: no fade-in
    std::array<float, 64 * 2> dc{};
    dc.fill(1.0f); // a steady input makes the changes easy to read
    std::array<float, 64 * 2> strip_out{};
    static_cast<void>(s_strip.process(s_strip_memory, dc, strip_out));
    const float first_frame = strip_out[0];
    for (int call = 0; call < 7; ++call) {
        static_cast<void>(s_strip.process(s_strip_memory, dc, strip_out));
    }
    std::printf("strip  first frame %.3f (the filter rising, the gain already at 1.0); settled at %.3f; latency %.3f frames (stored in the design)\n",
                static_cast<double>(first_frame), static_cast<double>(strip_out[126]),
                static_cast<double>(s_strip.stage<0>().nominal_latency()));

    // A new cutoff: design it (this can fail, and it is the part that uses double and tan),
    // then hand the result to the filter (this cannot fail).
    const auto retuned = lowpass_design<float>::make(0.02);
    if (retuned) {
        s_strip.stage<0>().set_design(*retuned);
    }
    const auto impossible = lowpass_design<float>::make(0.7);
    std::printf("strip  cutoff 0.02 %s; cutoff 0.7 %s\n", retuned ? "accepted" : "refused",
                impossible ? "accepted" : "refused: cutoff_out_of_range");

    // A new level: the gain ramps to it over its 96 frames, from wherever it is.
    s_strip.stage<1>().set_level(0.25f);
    std::printf("strip  level 1.0 -> 0.25, output at the end of each 64-frame call:");
    for (int call = 0; call < 3; ++call) {
        static_cast<void>(s_strip.process(s_strip_memory, dc, strip_out));
        std::printf(" %.3f", static_cast<double>(strip_out[126]));
    }
    std::printf("\n");

    // ---- the same changes from another thread: through mailboxes --------------------------
    // The control thread never touches the chain. It posts; the audio thread collects at the top
    // of a call. (One thread plays both parts here; mailbox_stress.cpp uses two.)
    static mailbox<lowpass_design<float>> s_new_design;
    static mailbox<float>                 s_new_level;
    static mailbox<float>                 s_meter; // and the other way: a reading for the user interface

    // Control thread:
    const auto wider = lowpass_design<float>::make(0.2);
    if (wider) {
        s_new_design.post(*wider);
    }
    s_new_level.post(0.9f);
    s_new_level.post(1.0f); // posted before the audio thread looked: the newest wins

    // Audio thread, at the top of its callback:
    const bool retuned_now = s_new_design.take([](const lowpass_design<float>& d) noexcept { s_strip.stage<0>().set_design(d); });
    const bool level_now   = s_new_level.take([](const float& level) noexcept { s_strip.stage<1>().set_level(level); });
    const bool no_more     = s_new_level.take([](const float& /*unused*/) noexcept {});
    static_cast<void>(s_strip.process(s_strip_memory, dc, strip_out));
    s_meter.post(strip_out[126]); // ... and at the bottom, a reading for whoever is watching

    // Control thread again:
    float shown = 0.0f;
    s_meter.take([&shown](const float& value) noexcept { shown = value; });
    std::printf("mail   design collected: %d; level collected: %d (now %.1f); nothing more waiting: %d; meter shows %.3f\n",
                retuned_now, level_now, static_cast<double>(s_strip.stage<1>().level()), !no_more, static_cast<double>(shown));

    // ---- reset: the configuration puts the state at rest, whatever the storage held ---------
    std::printf("reset  over garbage and over zeros, bit-equal output: overdrive %d, strip %d, steady region %d, steady down|up %d\n",
                reset_is_complete(overdrive), reset_is_complete(lowpass<2>{k_lowpass_at_0_1} | gain<2>{0.5f, 96}),
                reset_is_complete(region_steady), reset_is_complete(chain(steady{down_up})));

    // ---- D35: bursts, input groups and steady<> ------------------------------------------------
    // The bare region bursts.
    static memory_of<decltype(region)> s_region;
    region.reset(s_region.state);
    std::array<float, 64>  mono{};
    std::array<float, 128> burst{};
    std::printf("D35  bare region, 64 frames a call:");
    for (int call = 0; call < 4; ++call) {
        std::printf(" %zu", region.process(s_region, span<const float>(mono), span<float>(burst)));
    }
    std::printf("\n");
    // region.process(s_region, mono, mono_out);   // with two std::array<float, 64>: does not compile:
    //   "catenae: a fixed block must be a whole number of the chain's input groups; wrap the chain in steady<>"

    // A host whose block is the hop, aligned to it, needs no steady<>: the fixed entry compiles.
    static memory_of<decltype(region)> s_region_aligned;
    region.reset(s_region_aligned.state);
    std::array<float, 128> hop_in{};
    std::array<float, 128> hop_out{};
    std::printf("D35  bare region, 128 frames a call (its input group), fixed blocks:");
    for (int call = 0; call < 3; ++call) {
        std::printf(" %zu", region.process(s_region_aligned, hop_in, hop_out));
    }
    std::printf("\n");

    // steady<> gives n for n, 127 frames later.
    static memory_of<decltype(region_steady)> s_steady;
    region_steady.reset(s_steady.state);
    std::array<float, 64> mono_out{};
    std::printf("D35  steady region, 64 frames a call:");
    std::size_t where = 0;
    for (std::size_t call = 0; call < 4; ++call) {
        mono.fill(0.0f);
        if (call == 0) {
            mono[0] = 1.0f; // an impulse at frame 0
        }
        const std::size_t made = region_steady.process(s_steady, mono, mono_out);
        std::printf(" %zu", made);
        for (std::size_t i = 0; i < 64; ++i) {
            if (mono_out[i] > 0.0f) {
                where = call * 64 + i;
            }
        }
    }
    std::printf("; the impulse comes out at frame %zu\n", where);

    // Down then up is 1:1 and not n for n. In whole pairs it is exact, so an even fixed block
    // compiles; an odd one does not (must_not_compile.cpp). For any block at all: steady<>.
    static memory_of<decltype(down_up)> s_down_up;
    down_up.reset(s_down_up.state);
    std::array<float, 64> pairs_in{};
    std::array<float, 64> pairs_out{};
    std::array<float, 3>  odd_in{};
    std::array<float, 4>  odd_out{};
    std::printf("D35  down2 | up2: fixed 64-frame blocks give %zu %zu;", down_up.process(s_down_up, pairs_in, pairs_out),
                down_up.process(s_down_up, pairs_in, pairs_out));
    std::printf(" 3-frame calls through the checked entry give");
    for (int call = 0; call < 4; ++call) {
        std::printf(" %zu", down_up.process(s_down_up, span<const float>(odd_in), span<float>(odd_out)));
    }
    constexpr auto                              down_up_steady = chain(steady{down_up});
    static memory_of<decltype(down_up_steady)> s_down_up_steady;
    down_up_steady.reset(s_down_up_steady.state);
    std::array<float, 3> odd_same{};
    std::printf("; inside steady<> they give");
    for (int call = 0; call < 4; ++call) {
        std::printf(" %zu", down_up_steady.process(s_down_up_steady, odd_in, odd_same));
    }
    std::printf(" (1 frame of added latency)\n");

    // Two block stages whose sizes do not nest. The largest burst is not the worst backlog;
    // the sum of the stages' shortfalls bounds it.
    constexpr auto b96_128  = blockwise<96, same>{same{}} | blockwise<128, same>{same{}};
    constexpr auto b480_512 = blockwise<480, same>{same{}} | blockwise<512, same>{same{}};
    constexpr auto b7_5     = blockwise<7, same>{same{}} | blockwise<5, same>{same{}};
    constexpr auto b128_64  = blockwise<128, same>{same{}} | blockwise<64, same>{same{}};
    std::printf("D35  steady over two block stages, frames wrong against the input delayed by the stated latency:\n");
    std::printf("       96 | 128: latency %zu, wrong %zu (37-frame calls), %zu (64)\n", steady<decltype(b96_128)>::added_latency_frames,
                wrong_frames(b96_128, 37, 37 * 400), wrong_frames(b96_128, 64, 64 * 400));
    std::printf("      480 | 512: latency %zu, wrong %zu (33-frame calls), %zu (64)\n", steady<decltype(b480_512)>::added_latency_frames,
                wrong_frames(b480_512, 33, 33 * 800), wrong_frames(b480_512, 64, 64 * 400));
    std::printf("        7 | 5:   latency %zu, wrong %zu (1-frame calls), %zu (64)\n", steady<decltype(b7_5)>::added_latency_frames,
                wrong_frames(b7_5, 1, 4000), wrong_frames(b7_5, 64, 64 * 100));
    std::printf("      128 | 64:  latency %zu, wrong %zu (48-frame calls)\n", steady<decltype(b128_64)>::added_latency_frames,
                wrong_frames(b128_64, 48, 48 * 400));

    // ---- a junction with a fill policy ------------------------------------------------------
    // handoff<channels, capacity, target>: silence until the ring holds its target, then real
    // frames. The fill a pull sees is the target whichever thread ran first; the delay counted
    // in frames differs by the one block that the leading side is ahead.
    constexpr handoff<1, 1024, 128>      relay{};
    static state_of<decltype(relay)>     s_first;
    static state_of<decltype(relay)>     s_second;
    std::array<float, 64>                block{};
    std::array<float, 64>                pulled{};
    for (int order = 0; order < 2; ++order) {
        auto& st = order == 0 ? s_first : s_second;
        relay.reset(st);
        std::size_t pushed_frames = 0;
        std::size_t delay         = 0;
        std::size_t fill_seen     = 0;
        bool        found         = false;
        for (std::size_t period = 0; period < 40; ++period) {
            const bool late = order == 0 && (period == 20 || period == 21 || period == 22); // the producer misses three periods
            const auto push = [&]() noexcept {
                if (!late) {
                    for (std::size_t i = 0; i < 64; ++i) {
                        block[i] = static_cast<float>(++pushed_frames);
                    }
                    static_cast<void>(relay.push(st, block));
                }
            };
            if (order == 0) {
                push(); // the producer runs first in each period
            }
            fill_seen = relay.report(st).fill_frames;
            static_cast<void>(relay.pull(st, pulled));
            if (!found && pulled[0] > 0.0f) {
                delay = period * 64 + 1 - static_cast<std::size_t>(pulled[0]);
                found = true;
            }
            if (order == 1) {
                push(); // the consumer runs first in each period
            }
            if (period == 19) {
                std::printf("link  %s first: fill a pull sees %zu, delay %zu frames", order == 0 ? "producer" : "consumer", fill_seen,
                            delay);
            }
        }
        const handoff_report report = relay.report(st);
        if (order == 0) {
            std::printf("; then the producer misses three periods: %u underrun, and the fill a pull sees is back at %zu\n",
                        report.underruns, fill_seen);
        }
        else {
            std::printf("; %u underruns\n", report.underruns);
        }
    }

    // ---- the pull side: exactly the frames asked for, in one pull on the junction ------------
    // A chain by 4 asked for 102 frames writes 104; fed_by hands over exactly 102 and holds 2.
    constexpr counted_link::link_t                      feed{};
    static state_of<counted_link>                       s_link;
    static fed_by_state<decltype(up4)>                  s_from_link;
    static fed_by_workspace<decltype(up4), 1024>        s_from_link_work; // requests of up to 1024 frames
    feed.reset(s_link);
    reset(up4, s_from_link);
    std::array<float, 300 * 2> source{};
    const std::size_t          pushed = feed.push(s_link, source);
    std::array<float, 102 * 2> wanted{};
    const std::size_t          needed = up4.frames_needed(s_from_link.chain, 102);
    const std::size_t          bare   = up4.outputs_for(s_from_link.chain, needed);
    const std::size_t          got    = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(wanted));
    std::printf("D35  pull: pushed %zu; 102 frames need %zu in, which make %zu; fed_by delivered %zu and holds %zu, in %zu pull of %zu frames\n",
                pushed, needed, bare, got, s_from_link.held, counted_link::pulls, counted_link::largest);
    // A request larger than the chain's block is still one pull.
    std::array<float, 700 * 2> large{};
    counted_link::pulls   = 0;
    counted_link::largest = 0;
    const std::size_t big = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(large));
    std::printf("D35  pull of %zu frames (the chain's block is 64): %zu pull of %zu frames; holds %zu\n", big, counted_link::pulls,
                counted_link::largest, s_from_link.held);
    const std::size_t again = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(large));
    std::printf("D35  pull of %zu more with %zu frames left in the ring: delivered %zu, and starved counts %zu input frames of silence\n",
                again, static_cast<std::size_t>(300 - 26 - 175), again, s_from_link.starved);
    return 0;
}
