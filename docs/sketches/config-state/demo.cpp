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

// The memory. What a voice remembers is its state. What a call needs is the workspace: one
// for the thread, shared by both voices.
static state_of<decltype(overdrive)>     s_voice_a;
static state_of<decltype(overdrive)>     s_voice_b;
static workspace_of<decltype(overdrive)> s_overdrive_work;

// A state, a workspace and the pair of them are plain storage: no constructor runs for them at
// any standard, they can be reset in place, and they can be handed to C. A chain's
// configuration is trivially copyable too, so it can travel in a mailbox.
static_assert(is_plain<state_of<decltype(overdrive)>>);
static_assert(is_plain<workspace_of<decltype(overdrive)>>);
static_assert(is_plain<memory_of<decltype(overdrive)>>);
static_assert(std::is_trivially_copyable_v<std::remove_const_t<decltype(overdrive)>>);
// Up before down, with every frame of a pair arriving together: nothing trails or leads.
static_assert(decltype(overdrive)::max_shortfall == (ratio{0, 1}) && decltype(overdrive)::max_lead == (ratio{0, 1}));

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

// Down, then up: 1:1, and NOT n for n. Its input group is a pair. down2 writes on the first
// frame of a pair (the convention of the decimators catenae carries), so the chain never
// trails what it is owed and can be a whole frame ahead: steady<> then adds no latency, and
// needs room in its FIFO for that frame.
constexpr auto down_up = down2<1>{} | up2<1>{};
static_assert(decltype(down_up)::rate_ratio == (ratio{1, 1}));
static_assert(decltype(down_up)::input_group == 2);
static_assert(!decltype(down_up)::is_frame_synchronous);
static_assert(decltype(down_up)::max_shortfall == (ratio{0, 1}) && decltype(down_up)::max_lead == (ratio{1, 1}));
static_assert(steady<decltype(down_up)>::added_latency_frames == 0);
// The other convention, writing on the last frame of a pair: it trails by a frame and never leads.
constexpr auto late_up = down2_late<1>{} | up2<1>{};
static_assert(decltype(late_up)::max_shortfall == (ratio{1, 1}) && decltype(late_up)::max_lead == (ratio{0, 1}));
static_assert(steady<decltype(late_up)>::added_latency_frames == 1);

// A low-pass at 0.1 of the sample rate, as a table a generator would write: no designer, no
// double and no libm are needed to use it. (An unstable table here would not compile.)
constexpr auto k_lowpass_at_0_1 = lowpass_design<float>::from_table(
    {0.06745527388907191f, 0.13491054777814382f, 0.06745527388907191f, -1.1429805025399011f, 0.4128015980961886f},
    2.176250899482823f);

// ---- helpers for the checks below ------------------------------------------------------------

/// Runs steady{inner} on a ramp, in one call of `first` frames and then calls of `call`, and
/// counts the frames that are not what the inner configuration writes in one straight run,
/// delayed by the stated latency. (For a pass-through inner that is the ramp itself.)
template <typename Inner>
std::size_t wrong_frames(const Inner& inner, std::size_t first, std::size_t call, std::size_t calls) noexcept {
    static constexpr std::size_t k_most  = 1024;
    static constexpr std::size_t k_total = 40000;
    const auto                   wrapped = chain(steady{inner});
    const auto                   bare    = chain(inner);
    static memory_of<decltype(wrapped)> s_memory;
    static memory_of<decltype(bare)>    s_bare;
    static std::array<float, k_total>   s_ramp;
    static std::array<float, k_total + k_most> s_straight;
    wrapped.reset(s_memory.state);
    bare.reset(s_bare.state);
    const std::size_t total = first + call * calls;
    for (std::size_t i = 0; i < total; ++i) {
        s_ramp[i] = static_cast<float>(i + 1);
    }
    s_straight.fill(0.0f);
    const std::size_t straight = bare.process(s_bare, span<const float>(s_ramp.data(), total), span<float>(s_straight));
    const std::size_t latency  = steady<Inner>::added_latency_frames;
    std::array<float, k_most> out{};
    std::size_t               wrong = 0;
    std::size_t               at    = 0;
    for (std::size_t k = 0; k <= calls; ++k) {
        const std::size_t n = k == 0 ? first : call;
        static_cast<void>(wrapped.process(s_memory, span<const float>(s_ramp.data() + at, n), span<float>(out.data(), n)));
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t position = at + i;
            float             expected = 0.0f; // the priming, before the inner output arrives
            bool              known    = true;
            if (position >= latency) {
                known    = position - latency < straight;
                expected = known ? s_straight[position - latency] : 0.0f;
            }
            wrong += !known || out[i] < expected || out[i] > expected ? 1U : 0U;
        }
        at += n;
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

// A junction that counts the pulls made on it, to show that a request is at most one pull.
// Its target, 256, serves pulls of up to 175 frames beside pushes of 64: 256 >= 175 + 64 - 1.
struct counted_link {
    using link_t = handoff<2, 1024, 256, 64>;
    using state  = link_t::state;
    static inline std::size_t pulls   = 0;
    static inline std::size_t largest = 0;
    static constexpr bool serves(std::size_t largest_pull, std::size_t quantum) noexcept {
        return link_t::serves(largest_pull, quantum);
    }
    [[nodiscard]] std::size_t pull(state& st, span<float> out) const noexcept {
        ++pulls;
        largest = std::max(largest, out.size() / 2);
        return link_t{}.pull(st, out);
    }
};

// One run of a handoff on a clock of frames: the producer pushes Push frames every Push ticks
// from tick `producer_start`, the consumer pulls Pull frames every Pull ticks from tick
// `consumer_start`. From tick `late_from` the producer misses `late_pushes` pushes and then
// delivers them all at once (a thread that was held up, not frames that were lost).
struct link_run {
    std::size_t fill_at_start; // what the first real pull saw
    std::size_t least;         // the least and the most any later pull saw, while running
    std::size_t most;
    std::size_t fill_at_restart; // what the first real pull after an underrun saw
    std::uint32_t underruns;
    std::uint32_t overruns;
};
template <std::size_t Capacity, std::size_t Target, std::size_t Push, std::size_t Pull>
link_run run_link(std::size_t producer_start, std::size_t consumer_start, std::size_t late_from, std::size_t late_pushes) noexcept {
    constexpr handoff<1, Capacity, Target, Push> link{};
    static state_of<decltype(link)>              s_state;
    link.reset(s_state);
    std::array<float, Push> block{};
    std::array<float, Pull> pulled{};
    link_run    result{0, Capacity, 0, 0, 0, 0};
    bool        started = false;
    bool        was_running = false;
    std::size_t owed    = 0; // pushes the producer has missed and not yet delivered
    for (std::size_t tick = 0; tick < 40000; ++tick) {
        if (tick >= producer_start && (tick - producer_start) % Push == 0) {
            const std::size_t number = (tick - producer_start) / Push;
            if (late_pushes != 0 && number >= late_from && number < late_from + late_pushes) {
                ++owed;
            }
            else {
                for (std::size_t burst = 0; burst <= owed; ++burst) {
                    static_cast<void>(link.push(s_state, block));
                }
                owed = 0;
            }
        }
        if (tick >= consumer_start && (tick - consumer_start) % Pull == 0) {
            const handoff_report before = link.report(s_state);
            const std::size_t    real   = link.pull(s_state, pulled);
            const bool           running = real != 0;
            if (running) {
                const std::size_t saw = real + link.report(s_state).fill_frames; // after any trim
                if (!started) {
                    result.fill_at_start = saw;
                    started              = true;
                }
                else if (!was_running) {
                    result.fill_at_restart = saw;
                }
                else {
                    result.least = std::min(result.least, saw);
                    result.most  = std::max(result.most, saw);
                }
            }
            was_running = running && link.report(s_state).running;
            static_cast<void>(before);
        }
    }
    const handoff_report report = link.report(s_state);
    result.underruns            = report.underruns;
    result.overruns             = report.overruns;
    return result;
}

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
    std::printf("reset  over garbage and over zeros, bit-equal output: overdrive %d, strip %d, steady region %d, steady down|up %d, steady late-down|up %d\n",
                reset_is_complete(overdrive), reset_is_complete(lowpass<2>{k_lowpass_at_0_1} | gain<2>{0.5f, 96}),
                reset_is_complete(region_steady), reset_is_complete(chain(steady{down_up})), reset_is_complete(chain(steady{late_up})));

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
    std::printf("\n");

    // A stage may run AHEAD of what it is owed as well as behind. down2 writes on the first
    // frame of a pair, so after one odd call its output is a frame ahead, and steady<>'s FIFO
    // has to have room for that frame beside a full burst. "One odd call, then full blocks" is
    // the case that finds a FIFO one frame short.
    std::printf("D35  steady over a chain that leads or trails, frames wrong against one straight run delayed by the stated latency:\n");
    std::printf("      down2 | up2 (leads by 1):       latency %zu, wrong %zu (1 then 64s), %zu (3s), %zu (64s)\n",
                steady<decltype(down_up)>::added_latency_frames, wrong_frames(down_up, 1, 64, 40), wrong_frames(down_up, 3, 3, 400),
                wrong_frames(down_up, 64, 64, 40));
    std::printf("      down2_late | up2 (trails by 1): latency %zu, wrong %zu (1 then 64s), %zu (3s), %zu (64s)\n",
                steady<decltype(late_up)>::added_latency_frames, wrong_frames(late_up, 1, 64, 40), wrong_frames(late_up, 3, 3, 400),
                wrong_frames(late_up, 64, 64, 40));
    constexpr auto down_block_up = down2<1>{} | blockwise<96, same>{same{}} | up2<1>{};
    constexpr auto late_block_up = down2_late<1>{} | blockwise<5, same>{same{}} | up2<1>{} | blockwise<4, same>{same{}};
    std::printf("      down2 | block 96 | up2:         latency %zu, wrong %zu (1 then 64s), %zu (37s)\n",
                steady<decltype(down_block_up)>::added_latency_frames, wrong_frames(down_block_up, 1, 64, 100),
                wrong_frames(down_block_up, 37, 37, 300));
    std::printf("      down2_late | block 5 | up2 | block 4: latency %zu, wrong %zu (1 then 64s), %zu (7s)\n",
                steady<decltype(late_block_up)>::added_latency_frames, wrong_frames(late_block_up, 1, 64, 100),
                wrong_frames(late_block_up, 7, 7, 900));

    // Two block stages whose sizes do not nest. The largest burst is not the worst backlog. The
    // second stage is fed in whole blocks of the first, so it is never more than its own size
    // less their common divisor behind: 96 + 128 - 32 - 1, and that is exact.
    constexpr auto b96_128  = blockwise<96, same>{same{}} | blockwise<128, same>{same{}};
    constexpr auto b480_512 = blockwise<480, same>{same{}} | blockwise<512, same>{same{}};
    constexpr auto b7_5     = blockwise<7, same>{same{}} | blockwise<5, same>{same{}};
    constexpr auto b128_64  = blockwise<128, same>{same{}} | blockwise<64, same>{same{}};
    std::printf("D35  steady over two block stages, frames wrong against the input delayed by the stated latency:\n");
    std::printf("       96 | 128: latency %zu, wrong %zu (37-frame calls), %zu (64)\n", steady<decltype(b96_128)>::added_latency_frames,
                wrong_frames(b96_128, 37, 37, 400), wrong_frames(b96_128, 64, 64, 400));
    std::printf("      480 | 512: latency %zu, wrong %zu (33-frame calls), %zu (64)\n", steady<decltype(b480_512)>::added_latency_frames,
                wrong_frames(b480_512, 33, 33, 800), wrong_frames(b480_512, 64, 64, 400));
    std::printf("        7 | 5:   latency %zu, wrong %zu (1-frame calls), %zu (64)\n", steady<decltype(b7_5)>::added_latency_frames,
                wrong_frames(b7_5, 1, 1, 4000), wrong_frames(b7_5, 64, 64, 100));
    std::printf("      128 | 64:  latency %zu, wrong %zu (48-frame calls)\n", steady<decltype(b128_64)>::added_latency_frames,
                wrong_frames(b128_64, 48, 48, 400));

    // ---- a junction with a fill policy ------------------------------------------------------
    // handoff<channels, capacity, target, push>: silence until the ring holds its target; on
    // that pull the oldest frames above the target are discarded and the ring starts paying
    // out. So a pull that starts, or starts again, sees exactly the target, however early the
    // producer began and however much it delivers at once after being held up.
    static_assert(handoff<1, 1024, 128, 64>::serves(64, 64));   // 128 >= 64 + 64 - 64
    static_assert(handoff<1, 2048, 512, 480>::serves(64, 32));  // 512 >= 64 + 480 - 32
    static_assert(!handoff<1, 2048, 480, 480>::serves(64, 32)); // 480 is not
    static_assert(is_plain<state_of<handoff<2, 1024, 128, 64>>>);
    std::printf("link  target 128, 64-frame pushes and pulls; fill the first real pull sees, then the range later pulls see:\n");
    for (const std::size_t early : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{100}}) {
        const link_run r = run_link<1024, 128, 64, 64>(0, early * 64, 0, 0);
        std::printf("        producer %3zu periods early: %zu, then %zu to %zu; %u underruns, %u overruns while it waited\n", early,
                    r.fill_at_start, r.least, r.most, r.underruns, r.overruns);
    }
    {
        const link_run r = run_link<1024, 128, 64, 64>(5 * 64, 0, 0, 0);
        std::printf("        consumer   5 periods early: %zu, then %zu to %zu; %u underruns\n", r.fill_at_start, r.least, r.most, r.underruns);
    }
    {
        const link_run r = run_link<1024, 128, 64, 64>(0, 0, 50, 5);
        std::printf("        a producer held up for 5 periods that then delivers all 5: %u underrun, and the next real pull sees %zu\n",
                    r.underruns, r.fill_at_restart);
    }
    {
        const link_run enough = run_link<2048, 512, 480, 64>(0, 7, 0, 0);
        const link_run short_ = run_link<2048, 480, 480, 64>(0, 7, 0, 0);
        std::printf("link  480-frame pushes, 64-frame pulls: target 512 (64 + 480 - 32) sees %zu to %zu, %u underruns; target 480: %u underruns\n",
                    enough.least, enough.most, enough.underruns, short_.underruns);
    }

    // ---- the pull side: exactly the frames asked for, in at most one pull on the junction ----
    // A chain by 4 asked for 102 frames writes 104; fed_by hands over exactly 102 and holds 2.
    constexpr counted_link::link_t               feed{};
    static state_of<counted_link>                s_link;
    static fed_by_state<decltype(up4)>           s_from_link;
    static fed_by_workspace<decltype(up4), 700>  s_from_link_work; // requests of up to 700 frames: pulls of up to 175
    static_assert(fed_by_workspace<decltype(up4), 700>::k_max_pull == 175);
    static_assert(is_plain<fed_by_state<decltype(up4)>> && is_plain<fed_by_workspace<decltype(up4), 700>>);
    feed.reset(s_link);
    reset(up4, s_from_link);
    std::array<float, 64 * 2> source{};
    std::size_t               pushed = 0;
    for (int push = 0; push < 5; ++push) {
        pushed += feed.push(s_link, source);
    }
    std::array<float, 102 * 2> wanted{};
    const std::size_t          needed = up4.frames_needed(s_from_link.chain, 102);
    const std::size_t          bare   = up4.outputs_for(s_from_link.chain, needed);
    const std::size_t          got    = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(wanted));
    std::printf("D35  pull: pushed %zu (the ring starts from its target, 256); 102 frames need %zu in, which make %zu; fed_by delivered %zu and holds %zu, in %zu pull of %zu frames\n",
                pushed, needed, bare, got, s_from_link.held, counted_link::pulls, counted_link::largest);
    // A request the holdover covers makes no pull at all.
    std::array<float, 2 * 2> two{};
    counted_link::pulls = 0;
    const std::size_t small = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(two));
    std::printf("D35  pull of %zu frames, which the holdover covers: %zu pulls\n", small, counted_link::pulls);
    // A request larger than the chain's block is still one pull.
    std::array<float, 700 * 2> large{};
    counted_link::pulls   = 0;
    counted_link::largest = 0;
    const std::size_t big = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(large));
    std::printf("D35  pull of %zu frames (the chain's block is 64): %zu pull of %zu frames; holds %zu\n", big, counted_link::pulls,
                counted_link::largest, s_from_link.held);
    const std::size_t again = fed_by(counted_link{}, s_link, up4, s_from_link, s_from_link_work, span<float>(large));
    std::printf("D35  pull of %zu more with %zu frames left in the ring: delivered %zu, and starved counts %zu input frames of silence\n",
                again, static_cast<std::size_t>(256 - 26 - 175), again, s_from_link.starved);
    return 0;
}
