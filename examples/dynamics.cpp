/// @file dynamics.cpp
/// @brief The dynamics strip of examples/dynamics_tables.h in a callback, and what its two
/// choices buy: one level for the frame, and a limiter that looks ahead (guide: Dynamics).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/compressor.h"
#include "catenae/dynamics_design.h"
#include "catenae/expander.h"
#include "catenae/limiter.h"
#include "catenae/parallel.h"
#include "dynamics_tables.h"

namespace {

    using example::dynamics_strip;
    using example::dynamics_strip_t;

    // ANCHOR: callback
    // The memory: a voice's state and the thread's workspace, plain storage in .bss.
    CATENAE_CONSTINIT catenae::state_of<dynamics_strip_t> s_voice{};
    CATENAE_CONSTINIT catenae::workspace_of<dynamics_strip_t> s_work{};

    void reset_voice() noexcept {
        dynamics_strip.reset(s_voice);
    } // once, before the first callback

    void audio_callback(const std::array<float, 64 * 2>& in, std::array<float, 64 * 2>& out) noexcept {
        static_cast<void>(dynamics_strip.process(s_voice, s_work, in, out));
    }
    // ANCHOR_END: callback

    // ANCHOR: unlinked
    // Each channel its own compressor, as a pair of mono stages in parallel: each cuts by its own
    // level, so a loud left channel is cut more than a quiet right one.
    using unlinked_t = catenae::parallel<catenae::compressor<1>, catenae::compressor<1>>;
    // ANCHOR_END: unlinked

    // ANCHOR: fast
    // A limiter without look-ahead: the compressor at a ratio of infinity, its attack 48 frames
    // (1 ms at 48 kHz), the same ceiling. Made at run time, outside the audio path.
    const catenae::compressor_spec k_fast = {-1.0, std::numeric_limits<double>::infinity(), 0.0, 48.0, 2400.0, 0.0};
    // ANCHOR_END: fast

    constexpr double k_two_pi = 6.283185307179586;

    /// A stereo tone of `frames` frames: the left at `left` and the right at `right`, of
    /// amplitude, 0.01 cycles a frame.
    std::vector<float> tone(std::size_t frames, double left, double right) {
        std::vector<float> v(2 * frames);
        for (std::size_t f = 0; f < frames; ++f) {
            const double s = std::sin(k_two_pi * 0.01 * static_cast<double>(f));
            v[2 * f]       = static_cast<float>(left * s);
            v[2 * f + 1]   = static_cast<float>(right * s);
        }
        return v;
    }

    /// One run of `stage`, as a chain of one, from reset over all of `in`.
    template <typename Stage>
    std::vector<float> run(const Stage& stage, const std::vector<float>& in) {
        const auto c = catenae::chain(stage);
        using c_t    = std::remove_cv_t<decltype(c)>;
        std::vector<catenae::state_of<c_t>> st(1);
        c.reset(st[0]);
        std::vector<catenae::workspace_of<c_t>> ws(1);
        std::vector<float>                      out(in.size());
        static_cast<void>(c.process(st[0], ws[0], catenae::span<const float>(in), catenae::span<float>(out)));
        return out;
    }

    /// The largest magnitude of channel `c` from frame `from` on.
    double peak(const std::vector<float>& v, std::size_t c, std::size_t from) {
        double p = 0.0;
        for (std::size_t f = from; f < v.size() / 2; ++f) {
            p = std::fmax(p, std::fabs(static_cast<double>(v[2 * f + c])));
        }
        return p;
    }

    double db(double x) {
        return 20.0 * std::log10(x);
    }

    /// The balance between the channels after compression, dB: linked and unlinked.
    void balance(double& linked, double& unlinked) {
        const std::vector<float>     in = tone(48000, 1.0, 0.25); // left 0 dBFS, right -12 dBFS
        const catenae::compressor<2> both(example::bus_design);
        const unlinked_t each(catenae::compressor<1>(example::bus_design), catenae::compressor<1>(example::bus_design));
        const auto       a = run(both, in);
        const auto       b = run(each, in);
        linked             = db(peak(a, 0, 24000) / peak(a, 1, 24000));
        unlinked           = db(peak(b, 0, 24000) / peak(b, 1, 24000));
    }

    /// A burst at +6 dBFS after a quiet tone: how far above the ceiling the fast compressor lets
    /// it out, dB, and the limiter's largest output sample.
    void transient(double& fast_over, double& limited_peak) {
        std::vector<float> in = tone(4800, 0.1, 0.1);
        for (std::size_t f = 2400; f < 2880; ++f) {
            in[2 * f] *= 20.0f; // 0.1 to 2.0: +6 dBFS
            in[2 * f + 1] *= 20.0f;
        }
        const double ceiling = static_cast<double>(example::ceiling_design.numbers().ceiling);
        const auto   fast    = catenae::compressor_design<float>::make(k_fast);
        const auto   a       = run(catenae::compressor<2>(*fast), in);
        const auto   b       = run(catenae::limiter<2, 48>(example::ceiling_design), in);
        fast_over            = db(std::fmax(peak(a, 0, 0), peak(a, 1, 0)) / ceiling);
        limited_peak         = std::fmax(peak(b, 0, 0), peak(b, 1, 0));
    }

    /// The expander on a noise floor at -70 dB: what it cuts, dB.
    double gate_cut() {
        std::vector<float> in(2 * 48000);
        std::uint32_t      x = 0x9E3779B9u;
        for (float& v : in) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v = static_cast<float>(3.16e-4 * (static_cast<double>(x) / 2147483648.0 - 1.0)); // -70 dB
        }
        const auto out   = run(catenae::expander<2>(example::gate_design), in);
        double     e_in  = 0.0;
        double     e_out = 0.0;
        for (std::size_t i = in.size() / 2; i < in.size(); ++i) {
            e_in += static_cast<double>(in[i]) * static_cast<double>(in[i]);
            e_out += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        }
        return 10.0 * std::log10(e_in / e_out);
    }

} // namespace

int main() {
    reset_voice();
    std::array<float, 64 * 2> in{};
    std::array<float, 64 * 2> out{};
    audio_callback(in, out); // the strip runs in a callback like any chain
    double linked   = 0.0;
    double unlinked = 0.0;
    balance(linked, unlinked);
    double fast    = 0.0;
    double limited = 0.0;
    transient(fast, limited);
    const double cut = gate_cut();
    std::printf("left over right, 12.0 dB in: linked %.1f dB, each channel its own %.1f dB\n", linked, unlinked);
    const double ceiling = static_cast<double>(example::ceiling_design.numbers().ceiling);
    std::printf("a +6 dBFS burst: no look-ahead, %.1f dB over the ceiling; the limiter's peak %.6f, its ceiling %.6f\n",
                fast, limited, ceiling);
    std::printf("the expander on a -70 dB floor: %.1f dB cut\n", cut);
    const unsigned long latency = dynamics_strip_t::exact_latency.up;
    const unsigned long voice   = sizeof(catenae::state_of<dynamics_strip_t>);
    const unsigned long work    = sizeof(catenae::workspace_of<dynamics_strip_t>);
    std::printf("the strip: latency %lu frames; a voice %lu bytes; the workspace %lu bytes\n", latency, voice, work);
    const bool ok = std::fabs(linked - 12.0) < 0.05 && unlinked < 11.0 && fast > 3.0 && limited <= ceiling
                    && std::fabs(cut - 40.0) < 0.1;
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
