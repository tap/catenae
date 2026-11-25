// Copyright 2026 Timothy Place and the catenae contributors
// A main() so that measure.sh can link two whole images and compare them: one whose designs
// are all tables, and one that can also design a filter at run time. Throwaway.
#include <array>
#include <cstddef>

void reset_voices() noexcept;
void reset_strip() noexcept;
void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept;
void strip_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept;
bool set_cutoff(double cutoff) noexcept;

static std::array<float, 64 * 2> s_in;
static std::array<float, 64 * 2> s_out;
volatile float                   g_cutoff = 0.02f;

int main() {
    reset_voices();
    reset_strip();
#if defined(WITH_DESIGNER)
    static_cast<void>(set_cutoff(static_cast<double>(g_cutoff)));
#endif
    audio_callback(s_in, s_out);
    strip_callback(s_in, s_out);
    return static_cast<int>(s_out[0]);
}
