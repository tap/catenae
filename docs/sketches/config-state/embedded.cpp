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

// The same chain with a drive that can change at run time: the configuration is a variable.
static auto s_drive_chain = overdrive;
static state_of<decltype(overdrive)> s_driven;

void set_drive(float drive) noexcept {
    s_drive_chain.stage<2>().function().drive = drive;
}
void audio_callback_driven(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(s_drive_chain.process(s_driven, in, out));
}
