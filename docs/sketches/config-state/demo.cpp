// Copyright 2026 Timothy Place and the catenae contributors
// The sketch at work, on a host. Throwaway.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "sketch.h"

[[noreturn]] void catenae_check_failed(int code) noexcept {
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

// ---- D34 -----------------------------------------------------------------------------------
// The configuration: a constant. The type is the topology; the object is 4 bytes (the drive).
constexpr auto overdrive = up2<2>{} | up2<2>{} | map<2, soft_clip>{soft_clip{4.0f}} | down2<2>{} | down2<2>{};

static_assert(decltype(overdrive)::rate_ratio == (ratio{1, 1}));
static_assert(decltype(overdrive)::is_frame_synchronous);

// The memory: one zero-initialised object per instance. Two voices share one configuration.
static state_of<decltype(overdrive)> s_voice_a;
static state_of<decltype(overdrive)> s_voice_b;

// ---- D35 -----------------------------------------------------------------------------------
// A region that works on blocks of 128, as stft | gate | istft does.
// A stage used alone is a chain of one, which is what supplies the checked process().
constexpr auto region        = chain(blockwise<128, halve>{halve{}});
constexpr auto region_steady = chain(steady<blockwise<128, halve>>{blockwise<128, halve>{halve{}}});
static_assert(!decltype(region)::is_frame_synchronous);
static_assert(decltype(region_steady)::is_frame_synchronous);
static_assert(steady<blockwise<128, halve>>::added_latency_frames == 127);

constexpr auto up4 = up2<2>{} | up2<2>{};

// A low-pass at 0.1 of the sample rate, as a table a generator would write: no designer, no
// double and no libm are needed to use it.
constexpr auto k_lowpass_at_0_1 = lowpass_design<float>::from_table(
    {0.06745527388907191f, 0.13491054777814382f, 0.06745527388907191f, -1.1429805025399011f, 0.4128015980961886f},
    2.176250899482823f);

int main() {
    // D34: process fixed blocks. Both arguments are arrays, so every size is checked by the compiler.
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    in[0] = 0.25f;
    const std::size_t a = overdrive.process(s_voice_a, in, out);
    const std::size_t b = overdrive.process(s_voice_b, in, out);
    std::printf("D34  overdrive: %zu and %zu frames; configuration %zu bytes, state %zu bytes\n", a, b, sizeof(overdrive),
                sizeof(s_voice_a));

    // A parameter that changes at run time: the configuration is then an ordinary object, not a
    // constant. It is still only the parameters (8 bytes here); the histories stay in the state.
    static auto       s_drive_chain = overdrive;                    // a copy that may change
    constexpr std::size_t k_clip    = 2;                            // the map is the third stage
    s_drive_chain.stage<k_clip>().function().drive = 9.0f;          // set it, between calls
    const std::size_t c = s_drive_chain.process(s_voice_a, in, out); // the histories carry on
    std::printf("D34  drive is now %.1f: %zu frames from the same state\n",
                static_cast<double>(s_drive_chain.stage<k_clip>().function().drive), c);

    // ---- parameters that change at run time: a low-pass and a ramped gain -------------------
    // A fixed gain is just map<> with a multiplication.
    constexpr auto trim = chain(map<2, times>{times{0.5f}});
    static_cast<void>(trim);

    // A channel strip whose cutoff and level will change, so its configuration is a variable.
    // It starts from a design that is a table in ROM: no designer has run.
    static auto                         s_strip = lowpass<2>{k_lowpass_at_0_1} | gain<2>{1.0f, 96};
    static state_of<decltype(s_strip)>  s_strip_state;
    std::array<float, 64 * 2>           dc{};
    dc.fill(1.0f);                                   // a steady input makes the changes easy to read
    std::array<float, 64 * 2>           strip_out{};
    for (int call = 0; call < 8; ++call) {
        static_cast<void>(s_strip.process(s_strip_state, dc, strip_out));
    }
    std::printf("strip  settled at %.3f; latency %.3f frames (stored in the design)\n",
                static_cast<double>(strip_out[126]), static_cast<double>(s_strip.stage<0>().nominal_latency()));

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
        static_cast<void>(s_strip.process(s_strip_state, dc, strip_out));
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
    static_cast<void>(s_strip.process(s_strip_state, dc, strip_out));
    s_meter.post(strip_out[126]); // ... and at the bottom, a reading for whoever is watching

    // Control thread again:
    float shown = 0.0f;
    s_meter.take([&shown](const float& value) noexcept { shown = value; });
    std::printf("mail   design collected: %d; level collected: %d (now %.1f); nothing more waiting: %d; meter shows %.3f\n",
                retuned_now, level_now, static_cast<double>(s_strip.stage<1>().level()), !no_more, static_cast<double>(shown));

    // D35: the bare region bursts.
    static state_of<decltype(region)> s_region;
    std::array<float, 64>             mono{};
    std::array<float, 128>            burst{};
    std::printf("D35  bare region, 64 frames a call:");
    for (int call = 0; call < 4; ++call) {
        std::printf(" %zu", region.process(s_region, span<const float>(mono), span<float>(burst)));
    }
    std::printf("\n");
    // region.process(s_region, mono, mono_out);   // does not compile:
    //   "catenae: a fixed block needs a frame-synchronous chain; wrap it in steady<>"

    // steady<> gives n for n, 127 frames later.
    static state_of<decltype(region_steady)> s_steady;
    std::array<float, 64>                    mono_out{};
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

    // The pull side: a chain by 4 asked for 102 frames writes 104; fed_by hands over exactly 102.
    static handoff<2, 1024>              s_link;
    static fed_by_state<decltype(up4)>   s_from_link;
    std::array<float, 300 * 2>           source{};
    const std::size_t                    pushed = s_link.push(source);
    std::array<float, 102 * 2>           wanted{};
    const std::size_t                    needed = up4.frames_needed(s_from_link.chain, 102);
    const std::size_t                    bare   = up4.outputs_for(s_from_link.chain, needed);
    const std::size_t                    got    = fed_by(s_link, up4, s_from_link, span<float>(wanted));
    std::printf("D35  pull: pushed %zu; 102 frames need %zu in, which make %zu; fed_by delivered %zu and holds %zu\n", pushed,
                needed, bare, got, s_from_link.held);
    const std::size_t again = fed_by(s_link, up4, s_from_link, span<float>(wanted));
    std::printf("D35  pull again: delivered %zu, holds %zu, starved of %zu input frames\n", again, s_from_link.held,
                s_from_link.starved);
    return 0;
}
