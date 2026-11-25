/// @file racy_level.cpp
/// @brief What one would naturally write to change a setting from another thread, and what the
/// thread sanitizer says about it (guide: Threads and clocks). Built and run only where the build
/// is under the thread sanitizer, and passes only when the sanitizer reports the race.
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstdio>
#include <thread>

namespace {

    // ANCHOR: racy
    // The settings the audio thread uses, and a flag that says they changed. `volatile` makes the
    // compiler read the flag every time; it orders nothing, and it is not atomic.
    struct settings {
        float level;
        float pan;
    };
    settings      s_settings{1.0f, 0.0f};
    volatile bool s_changed = false;

    // Control thread: write the settings, then raise the flag.
    void change(float level, float pan) noexcept {
        s_settings.level = level;
        s_settings.pan   = pan;
        s_changed        = true;
    }

    // Audio thread, at the top of its callback: copy the settings if they changed.
    settings s_in_use{1.0f, 0.0f};
    void     audio_callback() noexcept {
        if (s_changed) {
            s_in_use  = s_settings;
            s_changed = false;
        }
    }
    // ANCHOR_END: racy

} // namespace

int main() {
    std::thread control([] {
        for (int i = 1; i <= 10000; ++i) {
            change(static_cast<float>(i) / 10000.0f, 0.5f);
        }
    });
    for (int i = 0; i < 10000; ++i) {
        audio_callback();
    }
    control.join();
    std::printf("level in use: %.4f\n", static_cast<double>(s_in_use.level));
    return 0;
}
