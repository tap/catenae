// Copyright 2026 Timothy Place and the catenae contributors
// Where the memory lands on a Cortex-M33. Throwaway; built by measure.sh.
#include <cmath>

#include "sketch.h"

[[noreturn]] void catenae::check_failed(int /*code*/) noexcept {
    for (;;) {
    }
}

using namespace catenae;

struct soft_clip {
    float drive;
    float operator()(float x) const noexcept { return std::tanh(drive * x); }
};

// Two voices of one overdrive: one configuration (a constant), a state each, and one workspace
// for the thread that runs them.
constexpr auto overdrive = up2<2>{} | up2<2>{} | map<2, soft_clip>{soft_clip{4.0f}} | down2<2>{} | down2<2>{};
CATENAE_SKETCH_CONSTINIT static state_of<decltype(overdrive)>     s_voice_a;
CATENAE_SKETCH_CONSTINIT static state_of<decltype(overdrive)>     s_voice_b;
CATENAE_SKETCH_CONSTINIT static workspace_of<decltype(overdrive)> s_overdrive_work;

// The audio callback: a fixed block, so no run-time size check.
void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(overdrive.process(s_voice_a, s_overdrive_work, in, out));
}
void second_voice_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(overdrive.process(s_voice_b, s_overdrive_work, in, out));
}

// A channel strip whose cutoff and level change at run time. Its configuration is a variable,
// started from a design that is a table in ROM. There is one of it, so its state and its
// workspace are kept together.
constexpr auto k_lowpass_at_0_1 = lowpass_design<float>::from_table(
    {0.06745527388907191f, 0.13491054777814382f, 0.06745527388907191f, -1.1429805025399011f, 0.4128015980961886f},
    2.176250899482823f);

CATENAE_SKETCH_CONSTINIT static auto                         s_strip = lowpass<2>{k_lowpass_at_0_1} | gain<2>{1.0f, 96};
CATENAE_SKETCH_CONSTINIT static memory_of<decltype(s_strip)> s_strip_memory;

void strip_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    static_cast<void>(s_strip.process(s_strip_memory, in, out));
}
void set_level(float level) noexcept {
    s_strip.stage<1>().set_level(level);
}
void set_design(const lowpass_design<float>& design) noexcept {
    s_strip.stage<0>().set_design(design);
}

// Once, at start-up, before the first callback: each configuration puts its states at rest.
void reset_voices() noexcept {
    overdrive.reset(s_voice_a);
    overdrive.reset(s_voice_b);
}
void reset_strip() noexcept {
    s_strip.reset(s_strip_memory.state);
}

// The same strip driven from another thread or an interrupt: nothing but posts on that side.
CATENAE_SKETCH_CONSTINIT static mailbox<lowpass_design<float>> s_new_design;
CATENAE_SKETCH_CONSTINIT static mailbox<float>                 s_new_level;

void post_level(float level) noexcept {
    s_new_level.post(level);
}
void post_design(const lowpass_design<float>& design) noexcept {
    s_new_design.post(design);
}
void strip_callback_with_mail(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
    s_new_design.take([](const lowpass_design<float>& d) noexcept { s_strip.stage<0>().set_design(d); });
    s_new_level.take([](const float& level) noexcept { s_strip.stage<1>().set_level(level); });
    static_cast<void>(s_strip.process(s_strip_memory, in, out));
}

// A junction: a stereo ring of 1024 frames that fills to 128 before it pays out, fed 64 at a time.
constexpr handoff<2, 1024, 128, 64> relay{};
CATENAE_SKETCH_CONSTINIT static state_of<decltype(relay)> s_relay;

void reset_relay() noexcept {
    relay.reset(s_relay);
}
std::size_t relay_push(const std::array<float, 64 * 2>& in) noexcept {
    return relay.push(s_relay, in);
}
std::size_t relay_pull(std::array<float, 64 * 2>& out) noexcept {
    return relay.pull(s_relay, out);
}

#if defined(SIZES)
// For measure.sh: the size of each part as the size of a symbol it can read with nm.
char size_of_overdrive_state[sizeof(state_of<decltype(overdrive)>)];
char size_of_overdrive_workspace[sizeof(workspace_of<decltype(overdrive)>)];
char size_of_strip_configuration[sizeof(s_strip)];
char size_of_strip_state[sizeof(state_of<decltype(s_strip)>)];
char size_of_strip_workspace[sizeof(workspace_of<decltype(s_strip)>)];
char size_of_mailbox_of_a_design[sizeof(s_new_design)];
char size_of_mailbox_of_a_float[sizeof(s_new_level)];
char size_of_relay_state[sizeof(state_of<decltype(relay)>)];
#endif
