/// @file spectral_gate.h
/// @brief A spectral gate, a stage written outside the library over the STFT's spectra, and
/// PLAN.md 9.5's region around it: bare at its hop and inside `steady` (guide: The transform
/// domain).
// Copyright 2026 Timothy Place and the catenae contributors
//
// The gate is the noise gate applied bin by bin, as in the denoising of Zolzer (ed.), DAFX:
// Digital Audio Effects, 2nd ed., Wiley, 2011, chapter 7 (time-frequency processing): a bin
// whose power reaches a threshold passes, one below it is attenuated to a floor. Its gain moves
// between the two at a fixed rate per frame, the attack and release of a gate's ballistics, so
// that a bin near the threshold does not switch on and off from frame to frame.
#ifndef CATENAE_EXAMPLES_SPECTRAL_GATE_H
#define CATENAE_EXAMPLES_SPECTRAL_GATE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/steady.h"
#include "catenae/stft.h"
#include "catenae/window.h"

namespace example {

    /// 10^(db / 20) for a whole number of dB, as a constant: ten to a whole power times one of
    /// the twentieths of a decade, each the double nearest; within a few ulps of the exact value.
    constexpr double amplitude_of_db(int db) noexcept {
        constexpr std::array<double, 20> twentieths = {1.0,
                                                       1.1220184543019633,
                                                       1.2589254117941673,
                                                       1.4125375446227544,
                                                       1.5848931924611134,
                                                       1.7782794100389228,
                                                       1.9952623149688795,
                                                       2.2387211385683394,
                                                       2.51188643150958,
                                                       2.8183829312644537,
                                                       3.1622776601683795,
                                                       3.5481338923357546,
                                                       3.9810717055349727,
                                                       4.466835921509631,
                                                       5.011872336272723,
                                                       5.623413251903491,
                                                       6.3095734448019325,
                                                       7.0794578438413795,
                                                       7.943282347242815,
                                                       8.912509381337456};
        int                              decades    = db / 20;
        int                              rest       = db % 20;
        if (rest < 0) {
            rest += 20;
            --decades;
        }
        double a = twentieths[static_cast<std::size_t>(rest)];
        for (; decades > 0; --decades) {
            a *= 10.0;
        }
        for (; decades < 0; ++decades) {
            a /= 10.0;
        }
        return a;
    }

    // ANCHOR: design
    /// A spectral gate's numbers, which its configuration holds.
    struct gate_design {
        float open_power;   ///< a bin whose |X|^2 is at least this opens
        float floor;        ///< a closed bin's gain, from 0 to 1
        float attack_step;  ///< per frame a bin is open, its openness rises by this: 1 / attack frames
        float release_step; ///< per frame it is closed, its openness falls by this: 1 / release frames
    };

    /// The gate for a periodic Hann STFT of `n` samples: a bin opens at `threshold_db` below a
    /// full-scale sine's at its centre (n/4, the forward unscaled and the window's sum n/2), a
    /// closed one passes `floor_db`, and a bin opens over `attack` frames and closes over
    /// `release`, at least one each.
    constexpr gate_design hann_gate(std::size_t n, int threshold_db, int floor_db, std::uint32_t attack,
                                    std::uint32_t release) noexcept {
        const double threshold = static_cast<double>(n) / 4.0 * amplitude_of_db(threshold_db);
        return {static_cast<float>(threshold * threshold), static_cast<float>(amplitude_of_db(floor_db)),
                1.0f / static_cast<float>(attack < 1u ? 1u : attack),
                1.0f / static_cast<float>(release < 1u ? 1u : release)};
    }
    // ANCHOR_END: design

    // ANCHOR: card
    /// @brief Each bin of each channel passed when its power reaches a threshold and attenuated to
    /// a floor when it does not, the gain moving between the two over frames.
    ///
    /// | Contract   |                                                                         |
    /// |------------|-------------------------------------------------------------------------|
    /// | Ports      | `spectrum<float, Ch, Bins>` in and out                                  |
    /// | Ratio      | 1:1, in place, input group 1 (frame-synchronous: no trail, no lead)     |
    /// | Configured | by a `gate_design`: the threshold power, the floor, the two steps       |
    /// | State      | each bin's openness o, 0 to 1; at rest all zero, every bin closed       |
    /// | Workspace  | none                                                                    |
    /// | Gain       | per frame, o rises by the attack step if |X|^2 reaches the threshold and |
    /// |            | falls by the release step if not, clamped to 0 and 1; X times floor +   |
    /// |            | (1 - floor) o, its new value                                            |
    /// | Latency    | exactly 0 frames                                                        |
    /// | Tail       | none: a zero bin comes out zero at any gain                             |
    /// | Formats    | `float`                                                                 |
    /// | Limits     | a NaN bin comes out NaN and counts as below the threshold; channels and |
    /// |            | bins are gated each alone, so a tone's bins near the threshold may part |
    /// | Method     | the noise gate per bin (Zolzer, DAFX, 2nd ed., chapter 7)               |
    /// | Rationale  | D25, D34, D35, D49                                                      |
    // ANCHOR_END: card
    // ANCHOR: stage
    template <std::size_t Bins, std::size_t Ch>
    class spectral_gate {
      public:
        // A spectrum in and the same out: frame by frame, each frame Ch channels of Bins bins.
        using input_port  = catenae::spectrum<float, Ch, Bins>;
        using output_port = catenae::spectrum<float, Ch, Bins>;
        static constexpr catenae::ratio rate_ratio{1, 1};
        static constexpr std::size_t    input_group = 1;
        static constexpr catenae::ratio max_shortfall{0, 1};
        static constexpr catenae::ratio max_lead{0, 1};
        static constexpr std::size_t    output_quantum    = 1;
        static constexpr bool           is_in_place       = true;
        static constexpr bool           has_exact_latency = true;
        static constexpr catenae::ratio exact_latency{0, 1};

        // What one instance remembers: how open each bin is. All zero is every bin closed.
        struct state {
            std::array<float, Ch * Bins> openness;
        };
        static constexpr bool rests_at_zero = true;
        using workspace                     = catenae::no_workspace;

        explicit constexpr spectral_gate(const gate_design& design) noexcept
            : m_design(design) {}

        constexpr const gate_design& design() const noexcept { return m_design; }

        std::size_t run(state& st, workspace&, catenae::span<const catenae::complex<float>> in,
                        catenae::span<catenae::complex<float>> out) const noexcept {
            const std::size_t frames = in.size() / (Ch * Bins);
            const float       range  = 1.0f - m_design.floor;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t b = 0; b < Ch * Bins; ++b) {
                    const catenae::complex<float> x = in[frame * Ch * Bins + b]; // read first: out may be in
                    float&                        o = st.openness[b];
                    o = x.re * x.re + x.im * x.im >= m_design.open_power ? std::min(1.0f, o + m_design.attack_step)
                                                                         : std::max(0.0f, o - m_design.release_step);
                    const float gain           = m_design.floor + range * o;
                    out[frame * Ch * Bins + b] = {x.re * gain, x.im * gain};
                }
            }
            return frames;
        }

        // The counts: n in, n out, from any state.
        static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }

        catenae::stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
        std::size_t            tail_frames() const noexcept { return 0; }
        std::size_t            practical_tail_frames() const noexcept { return 0; }
        float                  flush_residue() const noexcept { return 0.0f; }

      private:
        gate_design m_design;
    };
    // ANCHOR_END: stage

} // namespace example

namespace catenae {
    template <std::size_t Bins, std::size_t Ch>
    struct is_stage<example::spectral_gate<Bins, Ch>> : std::true_type {};
} // namespace catenae

namespace example {

    // ANCHOR: region
    /// A bin opens 40 dB below a full-scale sine's and is 20 dB down closed; it opens over 2
    /// frames and closes over 8.
    inline constexpr gate_design gate_512 = hann_gate(512, -40, -20, 2, 8);

    /// Mono. 512-sample Hann frames every 128 samples: 257 bins of complex<float> per frame.
    inline constexpr auto gate = catenae::stft<512, 128, 1>{catenae::window::hann} | spectral_gate<257, 1>{gate_512}
                                 | catenae::istft<512, 128, 1>{catenae::window::hann};
    using gate_t = std::remove_cv_t<decltype(gate)>;

    //   stft:          port<float, 1>           ->  spectrum<float, 1, 257>
    //   spectral_gate: spectrum<float, 1, 257>  ->  spectrum<float, 1, 257>
    //   istft:         spectrum<float, 1, 257>  ->  port<float, 1>
    static_assert(gate_t::rate_ratio == (catenae::ratio{1, 1})); // 1/128 then 128/1
    static_assert(gate_t::input_group == 128);                   // exact in whole hops
    static_assert(!gate_t::is_frame_synchronous);                // 64 frames in give 0 or 128 out
    static_assert(gate_t::exact_latency == (catenae::ratio{384, 1}));

    /// For an audio callback, which must return n frames for n: the same region inside steady.
    inline constexpr auto denoise = catenae::chain(catenae::steady{gate}); // 127 frames later, and it says so
    using denoise_t               = std::remove_cv_t<decltype(denoise)>;
    static_assert(denoise_t::is_frame_synchronous);
    static_assert(denoise_t::exact_latency == (catenae::ratio{511, 1}));
    // ANCHOR_END: region

} // namespace example

#endif // CATENAE_EXAMPLES_SPECTRAL_GATE_H
