// Copyright 2026 Timothy Place and the catenae contributors
// Where the memory lands on a Cortex-M33. Throwaway; built by measure.sh.
#include <cmath>

#include "sketch.h"

[[noreturn]] void catenae_check_failed(int /*code*/) noexcept {
    for (;;) {
    }
}

using namespace catenae;

struct soft_clip {
    float drive;
    float operator()(float x) const noexcept { return std::tanh(drive * x); }
};

constexpr auto overdrive = up2<2>{} | up2<2>{} | map<2, soft_clip>{soft_clip{4.0f}} | down2<2>{} | down2<2>{};
static state_of<decltype(overdrive)> s_overdrive;

// The audio callback: a fixed block, so no run-time size check.
void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(overdrive.process(s_overdrive, in, out));
}

// A channel strip whose cutoff and level change at run time. Its configuration is a variable,
// started from a design that is a table in ROM.
constexpr auto k_lowpass_at_0_1 = lowpass_design<float>::from_table(
    {0.06745527388907191f, 0.13491054777814382f, 0.06745527388907191f, -1.1429805025399011f, 0.4128015980961886f},
    2.176250899482823f);

static auto                        s_strip = lowpass<2>{k_lowpass_at_0_1} | gain<2>{1.0f, 96};
static state_of<decltype(s_strip)> s_strip_state;

void strip_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(s_strip.process(s_strip_state, in, out));
}
void set_level(float level) noexcept {
    s_strip.stage<1>().set_level(level);
}
void set_design(const lowpass_design<float>& design) noexcept {
    s_strip.stage<0>().set_design(design);
}
