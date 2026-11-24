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

int main() {
    // D34: process fixed blocks. Both arguments are arrays, so every size is checked by the compiler.
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    in[0] = 0.25f;
    const std::size_t a = overdrive.process(s_voice_a, in, out);
    const std::size_t b = overdrive.process(s_voice_b, in, out);
    std::printf("D34  overdrive: %zu and %zu frames; configuration %zu bytes, state %zu bytes\n", a, b, sizeof(overdrive),
                sizeof(s_voice_a));

    // A parameter change is a new configuration applied to the same state.
    auto driven = overdrive;                                     // a run-time copy, on the stack: 4 bytes
    driven      = up2<2>{} | up2<2>{} | map<2, soft_clip>{soft_clip{9.0f}} | down2<2>{} | down2<2>{};
    const std::size_t c = driven.process(s_voice_a, in, out);    // the histories carry on
    std::printf("D34  after a change of drive: %zu frames from the same state\n", c);

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
            if (mono_out[i] != 0.0f) { // NOLINT: an exact test for the sketch
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
