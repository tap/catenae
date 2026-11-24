// Copyright 2026 Timothy Place and the catenae contributors
//
// A THROWAWAY SKETCH for decisions D34 and D35 of PLAN.md. It exists to show what the code
// would look like and to measure where the memory lands. It is not the library: the rate
// changers are toys (a linear-interpolating up2, a pair-averaging down2), there is one sample
// type, and nothing here is a contract.
#ifndef CATENAE_SKETCHES_CONFIG_STATE_SKETCH_H
#define CATENAE_SKETCHES_CONFIG_STATE_SKETCH_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <utility>

// The integrator defines this (PLAN.md 3.3). It must not return.
[[noreturn]] void catenae_check_failed(int code) noexcept;

namespace catenae {

    inline void expects(bool ok, int code) noexcept {
        if (!ok) {
            catenae_check_failed(code);
        }
    }

    // ---- the small vocabulary -----------------------------------------------------------

    struct ratio {
        std::size_t up;
        std::size_t down;
    };
    constexpr bool operator==(ratio a, ratio b) noexcept {
        return a.up * b.down == b.up * a.down;
    }
    constexpr ratio operator*(ratio a, ratio b) noexcept {
        const std::size_t u = a.up * b.up;
        const std::size_t d = a.down * b.down;
        const std::size_t g = std::gcd(u, d);
        return ratio{u / g, d / g};
    }

    template <typename S, std::size_t Ch>
    struct port {
        using value_type = S;
        static constexpr std::size_t width = Ch;
    };

    template <typename T>
    class span {
      public:
        constexpr span(T* first, std::size_t count) noexcept
            : m_data(first)
            , m_size(count) {}
        template <std::size_t N>
        constexpr span(std::array<std::remove_const_t<T>, N>& a) noexcept // NOLINT: converting, as std::span
            : m_data(a.data())
            , m_size(N) {}
        template <std::size_t N>
        constexpr span(const std::array<std::remove_const_t<T>, N>& a) noexcept // NOLINT
            : m_data(a.data())
            , m_size(N) {}
        template <typename U, std::enable_if_t<std::is_same_v<const U, T>, int> = 0>
        constexpr span(span<U> other) noexcept // NOLINT: span<float> to span<const float>
            : m_data(other.data())
            , m_size(other.size()) {}

        constexpr T*          data() const noexcept { return m_data; }
        constexpr std::size_t size() const noexcept { return m_size; }
        constexpr T*          begin() const noexcept { return m_data; }
        constexpr T*          end() const noexcept { return m_data + m_size; }
        constexpr T&          operator[](std::size_t i) const noexcept { return m_data[i]; }
        constexpr span        first(std::size_t count) const noexcept { return span(m_data, count); }
        constexpr span        subspan(std::size_t offset) const noexcept { return span(m_data + offset, m_size - offset); }

      private:
        T*          m_data;
        std::size_t m_size;
    };

    // A type says it is a stage by specialising this (D10).
    template <typename T>
    struct is_stage : std::false_type {};

    // =====================================================================================
    // D34. A stage is a CONFIGURATION: a small constant value. What it remembers between
    // calls is a separate STATE, which is valid when every byte of it is zero.
    // =====================================================================================

    /// out = f(in), a sample at a time.
    template <std::size_t Ch, typename F>
    class map {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio rate_ratio{1, 1};
        static constexpr bool  is_frame_synchronous = true;

        struct state {};

        explicit constexpr map(F f) noexcept
            : m_f(f) {}

        constexpr const F& function() const noexcept { return m_f; }
        constexpr F&       function() noexcept { return m_f; }

        std::size_t run(state& /*unused*/, span<const float> in, span<float> out) const noexcept {
            std::transform(in.begin(), in.end(), out.begin(), m_f);
            return in.size() / Ch;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        static constexpr std::size_t outputs_for(const state& /*unused*/, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state& /*unused*/, std::size_t k) noexcept { return k; }

      private:
        F m_f;
    };
    template <std::size_t Ch, typename F>
    struct is_stage<map<Ch, F>> : std::true_type {};

    /// A fixed gain is nothing more than map<> with a multiplication: no class of its own.
    struct times {
        float factor;
        constexpr float operator()(float x) const noexcept { return x * factor; }
    };

    // ---- a parameter that changes while the stream runs: gain -----------------------------

    /// A gain whose level can change at run time without a click: it ramps to a new level
    /// over ramp_frames. The level and the ramp length are the configuration. Where the ramp
    /// has got to is state.
    template <std::size_t Ch>
    class gain {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio rate_ratio{1, 1};
        static constexpr bool  is_frame_synchronous = true;

        struct state {
            float         current;   // the level being applied now
            float         goal;      // the level the ramp is heading for
            float         step;      // per frame, while a ramp runs
            std::uint32_t remaining; // frames of ramp left
            std::uint32_t started;   // 0 until the first call, which starts at the level with no fade-in
        };

        constexpr gain(float level, std::uint32_t ramp_frames) noexcept
            : m_level(level)
            , m_ramp_frames(ramp_frames) {}

        constexpr float level() const noexcept { return m_level; }
        /// Takes effect from the next run(), as a ramp. Not synchronised: call it on the thread
        /// that runs the chain, between calls.
        constexpr void set_level(float level) noexcept { m_level = level; }

        std::size_t run(state& st, span<const float> in, span<float> out) const noexcept {
            if (st.started == 0) {
                st.current = m_level;
                st.goal    = m_level;
                st.started = 1;
            }
            if (st.goal < m_level || st.goal > m_level) { // the level was changed: head for it
                st.goal      = m_level;
                st.remaining = m_ramp_frames;
                if (m_ramp_frames == 0) {
                    st.current = m_level;
                }
                else {
                    st.step = (m_level - st.current) / static_cast<float>(m_ramp_frames);
                }
            }
            const std::size_t frames = in.size() / Ch;
            for (std::size_t i = 0; i < frames; ++i) {
                if (st.remaining > 0) {
                    st.current += st.step;
                    --st.remaining;
                    if (st.remaining == 0) {
                        st.current = st.goal; // land exactly, whatever the rounding did
                    }
                }
                for (std::size_t c = 0; c < Ch; ++c) {
                    out[i * Ch + c] = in[i * Ch + c] * st.current;
                }
            }
            return frames;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        static constexpr std::size_t outputs_for(const state& /*unused*/, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state& /*unused*/, std::size_t k) noexcept { return k; }

      private:
        float         m_level;
        std::uint32_t m_ramp_frames;
    };
    template <std::size_t Ch>
    struct is_stage<gain<Ch>> : std::true_type {};

    // ---- a parameter that can be wrong: a low-pass filter and its design (D37) --------------

    enum class [[nodiscard]] status { cutoff_out_of_range };

    template <typename E>
    struct unexpected {
        E value;
    };

    /// A value or the reason there is none, after std::expected. No heap, no exceptions.
    template <typename T, typename E>
    class [[nodiscard]] expected {
      public:
        constexpr expected(const T& value) noexcept // NOLINT: converting, as std::expected
            : m_value(value)
            , m_has(true) {}
        constexpr expected(unexpected<E> error) noexcept // NOLINT
            : m_error(error.value)
            , m_has(false) {}

        constexpr bool     has_value() const noexcept { return m_has; }
        constexpr explicit operator bool() const noexcept { return m_has; }
        constexpr const T& operator*() const noexcept {
            expects(m_has, 3);
            return m_value;
        }
        constexpr E error() const noexcept {
            expects(!m_has, 4);
            return m_error;
        }

      private:
        union {
            T m_value;
            E m_error;
        };
        bool m_has;
    };

    /// A second-order Butterworth low-pass, designed for sample format S. This is the part
    /// that can fail and the part that uses double and libm, so it is kept apart from the
    /// stage: make() is called at start-up or on a control thread, never in the audio path.
    /// A design holds its coefficients already in S, and its latency as a stored number.
    template <typename S>
    class lowpass_design {
      public:
        /// cutoff in cycles per sample. Fails outside the range the format can hold.
        static expected<lowpass_design, status> make(double cutoff) noexcept {
            constexpr double lowest = 1.0e-4; // below this a float design's poles reach the unit circle
            if (!(cutoff >= lowest && cutoff <= 0.5 - lowest)) {
                return unexpected<status>{status::cutoff_out_of_range};
            }
            constexpr double pi    = 3.14159265358979323846;
            constexpr double sqrt2 = 1.41421356237309504880;
            const double     k     = std::tan(pi * cutoff);
            const double     norm  = 1.0 / (1.0 + sqrt2 * k + k * k);
            const double     b0    = k * k * norm;
            const double     b1    = 2.0 * b0;
            const double     b2    = b0;
            const double     a1    = 2.0 * (k * k - 1.0) * norm;
            const double     a2    = (1.0 - sqrt2 * k + k * k) * norm;
            const double     delay = (b1 + 2.0 * b2) / (b0 + b1 + b2) - (a1 + 2.0 * a2) / (1.0 + a1 + a2);
            return lowpass_design({static_cast<S>(b0), static_cast<S>(b1), static_cast<S>(b2), static_cast<S>(a1),
                                   static_cast<S>(a2)},
                                  static_cast<float>(delay));
        }

        /// For a table that a generator wrote: the same value with no designer in the image.
        static constexpr lowpass_design from_table(const std::array<S, 5>& coefficients, float latency_frames) noexcept {
            return lowpass_design(coefficients, latency_frames);
        }

        constexpr const std::array<S, 5>& coefficients() const noexcept { return m_coefficients; } // b0 b1 b2 a1 a2
        constexpr float                   nominal_latency() const noexcept { return m_latency; }   // group delay at DC

      private:
        constexpr lowpass_design(const std::array<S, 5>& coefficients, float latency_frames) noexcept
            : m_coefficients(coefficients)
            , m_latency(latency_frames) {}

        std::array<S, 5> m_coefficients;
        float            m_latency;
    };

    /// The filter. It is built from a design, so it cannot fail, and it never sees a double.
    template <std::size_t Ch>
    class lowpass {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio rate_ratio{1, 1};
        static constexpr bool  is_frame_synchronous = true;

        struct state {
            std::array<std::array<float, 2>, Ch> z; // transposed direct form II, per channel
        };

        explicit constexpr lowpass(const lowpass_design<float>& design) noexcept
            : m_design(design) {}

        constexpr const lowpass_design<float>& design() const noexcept { return m_design; }
        /// A new cutoff is a new design. This cannot fail: the design was checked when it was
        /// made. The filter's history carries on under the new coefficients.
        constexpr void  set_design(const lowpass_design<float>& design) noexcept { m_design = design; }
        constexpr float nominal_latency() const noexcept { return m_design.nominal_latency(); }

        std::size_t run(state& st, span<const float> in, span<float> out) const noexcept {
            const std::array<float, 5>& k      = m_design.coefficients();
            const std::size_t           frames = in.size() / Ch;
            for (std::size_t i = 0; i < frames; ++i) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    const float x   = in[i * Ch + c];
                    const float y   = k[0] * x + st.z[c][0];
                    st.z[c][0]      = k[1] * x - k[3] * y + st.z[c][1];
                    st.z[c][1]      = k[2] * x - k[4] * y;
                    out[i * Ch + c] = y;
                }
            }
            return frames;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        static constexpr std::size_t outputs_for(const state& /*unused*/, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state& /*unused*/, std::size_t k) noexcept { return k; }

      private:
        lowpass_design<float> m_design; // the configuration: five coefficients and a latency
    };
    template <std::size_t Ch>
    struct is_stage<lowpass<Ch>> : std::true_type {};

    /// Twice the rate, by linear interpolation (a toy). It remembers one frame.
    template <std::size_t Ch>
    class up2 {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio rate_ratio{2, 1};
        static constexpr bool  is_frame_synchronous = true;

        struct state {
            std::array<float, Ch> previous; // zero: silence before the stream, which is right
        };

        constexpr up2() noexcept = default;

        std::size_t run(state& st, span<const float> in, span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            for (std::size_t i = 0; i < frames; ++i) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    const float x           = in[i * Ch + c];
                    out[(2 * i) * Ch + c]   = 0.5f * (st.previous[c] + x);
                    out[(2 * i + 1) * Ch + c] = x;
                    st.previous[c]          = x;
                }
            }
            return 2 * frames;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return 2 * n; }
        static constexpr std::size_t outputs_for(const state& /*unused*/, std::size_t n) noexcept { return 2 * n; }
        static constexpr std::size_t frames_needed(const state& /*unused*/, std::size_t k) noexcept { return (k + 1) / 2; }
    };
    template <std::size_t Ch>
    struct is_stage<up2<Ch>> : std::true_type {};

    /// Half the rate, by averaging pairs (a toy). It remembers a held frame and a phase.
    template <std::size_t Ch>
    class down2 {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio rate_ratio{1, 2};
        static constexpr bool  is_frame_synchronous = true; // for calls of a whole number of pairs

        struct state {
            std::array<float, Ch> held;
            std::size_t           have; // 0 or 1 frames waiting for their pair
        };

        constexpr down2() noexcept = default;

        std::size_t run(state& st, span<const float> in, span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            std::size_t       made   = 0;
            for (std::size_t i = 0; i < frames; ++i) {
                if (st.have == 0) {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        st.held[c] = in[i * Ch + c];
                    }
                    st.have = 1;
                }
                else {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[made * Ch + c] = 0.5f * (st.held[c] + in[i * Ch + c]);
                    }
                    st.have = 0;
                    ++made;
                }
            }
            return made;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return (n + 1) / 2; }
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept { return (st.have + n) / 2; }
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            return k == 0 ? 0 : 2 * k - st.have;
        }
    };
    template <std::size_t Ch>
    struct is_stage<down2<Ch>> : std::true_type {};

    /// Works on whole blocks of Hop frames: nothing comes out until a block is in, then Hop
    /// frames come out at once. This is how stft | spectral stage | istft behaves per call,
    /// and it stands in for that region here (mono, f applied to each sample of the block).
    template <std::size_t Hop, typename F>
    class blockwise {
      public:
        using input_port  = port<float, 1>;
        using output_port = port<float, 1>;
        static constexpr ratio rate_ratio{1, 1};
        static constexpr bool  is_frame_synchronous = false; // 1:1 in the long run only (D35)

        struct state {
            std::array<float, Hop> block;
            std::size_t            have;
        };

        explicit constexpr blockwise(F f) noexcept
            : m_f(f) {}

        std::size_t run(state& st, span<const float> in, span<float> out) const noexcept {
            std::size_t made = 0;
            for (std::size_t i = 0; i < in.size(); ++i) {
                st.block[st.have] = in[i];
                ++st.have;
                if (st.have == Hop) {
                    std::transform(st.block.begin(), st.block.end(), out.begin() + made, m_f);
                    made += Hop;
                    st.have = 0;
                }
            }
            return made;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return ((n + Hop - 1) / Hop) * Hop; }
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
            return ((st.have + n) / Hop) * Hop;
        }
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            return k == 0 ? 0 : ((k + Hop - 1) / Hop) * Hop - st.have;
        }

      private:
        F m_f;
    };
    template <std::size_t Hop, typename F>
    struct is_stage<blockwise<Hop, F>> : std::true_type {};

    // ---- the chain ----------------------------------------------------------------------

    inline constexpr std::size_t k_block = 64; // frames of the chain's input worked on at a time

    template <typename... Stages>
    class chain {
        static constexpr std::size_t k_count = sizeof...(Stages);
        template <std::size_t I>
        using stage_t = std::tuple_element_t<I, std::tuple<Stages...>>;

        // The most frames stage I can write while the chain works on one block.
        template <std::size_t I>
        static constexpr std::size_t bound() noexcept {
            if constexpr (I == 0) {
                return stage_t<0>::max_outputs_for(k_block);
            }
            else {
                return stage_t<I>::max_outputs_for(bound<I - 1>());
            }
        }

        // One joint: the output port of stage I is the input port of stage I + 1.
        template <std::size_t I>
        static constexpr bool joint() noexcept {
            static_assert(std::is_same_v<typename stage_t<I>::output_port, typename stage_t<I + 1>::input_port>,
                          "catenae: one stage's output port is not the next stage's input port");
            return true;
        }
        template <std::size_t... I>
        static constexpr bool joints(std::index_sequence<I...> /*unused*/) noexcept {
            return (joint<I>() && ...);
        }
        static constexpr bool k_joints_ok = joints(std::make_index_sequence<k_count - 1>{});

        template <std::size_t I>
        using scratch_t = std::array<typename stage_t<I>::output_port::value_type, bound<I>() * stage_t<I>::output_port::width>;
        template <typename Seq>
        struct scratch_of;
        template <std::size_t... I>
        struct scratch_of<std::index_sequence<I...>> {
            using type = std::tuple<scratch_t<I>...>;
        };

      public:
        using input_port  = typename stage_t<0>::input_port;
        using output_port = typename stage_t<k_count - 1>::output_port;
        using sample_in   = typename input_port::value_type;
        using sample_out  = typename output_port::value_type;

        static constexpr ratio rate_ratio           = (Stages::rate_ratio * ...);
        static constexpr bool  is_frame_synchronous = (Stages::is_frame_synchronous && ...);

        /// Everything the chain remembers and all of its scratch. All zero is its reset value,
        /// so a static one is in .bss: no flash, no start-up code.
        struct state {
            std::tuple<typename Stages::state...>                                 stages;
            typename scratch_of<std::make_index_sequence<k_count - 1>>::type scratch;
        };

        explicit constexpr chain(Stages... stages) noexcept
            : m_stages(stages...) {}

        constexpr const std::tuple<Stages...>& stages() const noexcept { return m_stages; }

        /// One stage's configuration, by its position in the chain.
        template <std::size_t I>
        constexpr const stage_t<I>& stage() const noexcept {
            return std::get<I>(m_stages);
        }
        template <std::size_t I>
        constexpr stage_t<I>& stage() noexcept {
            return std::get<I>(m_stages);
        }

        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return fold_max<0>(n); }
        constexpr std::size_t outputs_for(const state& st, std::size_t n) const noexcept { return fold_out<0>(st, n); }
        constexpr std::size_t frames_needed(const state& st, std::size_t k) const noexcept {
            return fold_need<k_count - 1>(st, k);
        }

        /// The trusted form: what an enclosing chain or an adapter calls.
        std::size_t run(state& st, span<const sample_in> in, span<sample_out> out) const noexcept {
            const std::size_t width  = input_port::width;
            const std::size_t frames = in.size() / width;
            std::size_t       done   = 0;
            std::size_t       made   = 0;
            while (done < frames) {
                const std::size_t n = std::min(k_block, frames - done);
                made += step<0>(st, span<const sample_in>(in.data() + done * width, n * width),
                                out.subspan(made * output_port::width));
                done += n;
            }
            return made;
        }

        /// The caller's entry: the one run-time check, then run().
        [[nodiscard]] std::size_t process(state& st, span<const sample_in> in, span<sample_out> out) const noexcept {
            expects(in.size() % input_port::width == 0, 1);
            expects(out.size() >= outputs_for(st, in.size() / input_port::width) * output_port::width, 2);
            return run(st, in, out);
        }

        /// The caller's entry for fixed blocks: checked by the compiler, nothing at run time.
        template <std::size_t N, std::size_t M>
        [[nodiscard]] std::size_t process(state& st, const std::array<sample_in, N>& in,
                                          std::array<sample_out, M>& out) const noexcept {
            static_assert(is_frame_synchronous,
                          "catenae: a fixed block needs a frame-synchronous chain; wrap it in steady<>");
            static_assert(N % input_port::width == 0, "catenae: the input is not a whole number of frames");
            static_assert(M >= max_outputs_for(N / input_port::width) * output_port::width,
                          "catenae: the output block is too small for this input block");
            return run(st, span<const sample_in>(in), span<sample_out>(out));
        }

      private:
        template <std::size_t I>
        std::size_t step(state& st, span<const typename stage_t<I>::input_port::value_type> in,
                         span<sample_out> out) const noexcept {
            const auto& stage = std::get<I>(m_stages);
            auto&       mine  = std::get<I>(st.stages);
            if constexpr (I + 1 == k_count) {
                return stage.run(mine, in, out);
            }
            else {
                auto&             buffer = std::get<I>(st.scratch);
                const std::size_t made   = stage.run(mine, in, span<typename scratch_t<I>::value_type>(buffer));
                return step<I + 1>(
                    st, span<const typename scratch_t<I>::value_type>(buffer.data(), made * stage_t<I>::output_port::width),
                    out);
            }
        }
        template <std::size_t I>
        static constexpr std::size_t fold_max(std::size_t n) noexcept {
            if constexpr (I + 1 == k_count) {
                return stage_t<I>::max_outputs_for(n);
            }
            else {
                return fold_max<I + 1>(stage_t<I>::max_outputs_for(n));
            }
        }
        template <std::size_t I>
        constexpr std::size_t fold_out(const state& st, std::size_t n) const noexcept {
            const std::size_t made = std::get<I>(m_stages).outputs_for(std::get<I>(st.stages), n);
            if constexpr (I + 1 == k_count) {
                return made;
            }
            else {
                return fold_out<I + 1>(st, made);
            }
        }
        template <std::size_t I>
        constexpr std::size_t fold_need(const state& st, std::size_t k) const noexcept {
            const std::size_t n = std::get<I>(m_stages).frames_needed(std::get<I>(st.stages), k);
            if constexpr (I == 0) {
                return n;
            }
            else {
                return fold_need<I - 1>(st, n);
            }
        }

        std::tuple<Stages...> m_stages; // the configurations: this is all a chain object holds
    };
    template <typename... Stages>
    struct is_stage<chain<Stages...>> : std::true_type {};

    /// The memory of a chain or a stage.
    template <typename Config>
    using state_of = typename Config::state;

    // a | b | c composes CONFIGURATIONS, which are a few bytes each. No history moves.
    template <typename A, typename B, std::enable_if_t<is_stage<A>::value && is_stage<B>::value, int> = 0>
    constexpr chain<A, B> operator|(A a, B b) noexcept {
        return chain<A, B>(a, b);
    }
    template <typename... As, typename B, std::enable_if_t<is_stage<B>::value, int> = 0>
    constexpr chain<As..., B> operator|(const chain<As...>& a, B b) noexcept {
        return std::apply([&b](const As&... as) { return chain<As..., B>(as..., b); }, a.stages());
    }

    // =====================================================================================
    // D35. A chain whose ratio is 1:1 need not return n frames for n. steady<> makes it so.
    // =====================================================================================

    /// Wraps a 1:1 configuration that writes in bursts and presents a frame-synchronous one:
    /// n frames out for every n in. The price is a FIFO and (largest burst - 1) frames more
    /// latency, both stated.
    template <typename Inner>
    class steady {
        static_assert(Inner::rate_ratio == (ratio{1, 1}), "catenae: steady<> is for a 1:1 configuration");
        using sample = typename Inner::output_port::value_type;
        static constexpr std::size_t k_width    = Inner::output_port::width;
        static constexpr std::size_t k_burst    = Inner::max_outputs_for(1);
        static constexpr std::size_t k_capacity = (k_burst - 1) + Inner::max_outputs_for(k_block);

      public:
        using input_port  = typename Inner::input_port;
        using output_port = typename Inner::output_port;
        static constexpr ratio       rate_ratio           = Inner::rate_ratio;
        static constexpr bool        is_frame_synchronous = true;
        static constexpr std::size_t added_latency_frames = k_burst - 1;

        struct state {
            state_of<Inner>                                               inner;
            std::array<sample, Inner::max_outputs_for(k_block) * k_width> burst; // what one inner call wrote
            std::array<sample, k_capacity * k_width>                      fifo;  // zeros at reset: the added latency
            std::size_t                                                   read;
            std::ptrdiff_t                                                surplus; // frames held beyond (k_burst - 1)
        };

        explicit constexpr steady(Inner inner) noexcept
            : m_inner(inner) {}

        std::size_t run(state& st, span<const typename input_port::value_type> in, span<sample> out) const noexcept {
            const std::size_t frames = in.size() / input_port::width;
            std::size_t       done   = 0;
            while (done < frames) {
                const std::size_t n    = std::min(k_block, frames - done);
                const std::size_t made = m_inner.run(
                    st.inner, span<const typename input_port::value_type>(in.data() + done * input_port::width,
                                                                          n * input_port::width),
                    span<sample>(st.burst));
                const std::size_t held  = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(k_burst - 1) + st.surplus);
                const std::size_t write = (st.read + held) % k_capacity;
                for (std::size_t i = 0; i < made * k_width; ++i) {
                    st.fifo[(write * k_width + i) % (k_capacity * k_width)] = st.burst[i];
                }
                for (std::size_t i = 0; i < n * k_width; ++i) {
                    out[done * k_width + i] = st.fifo[(st.read * k_width + i) % (k_capacity * k_width)];
                }
                st.read = (st.read + n) % k_capacity;
                st.surplus += static_cast<std::ptrdiff_t>(made) - static_cast<std::ptrdiff_t>(n);
                done += n;
            }
            return frames;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
        static constexpr std::size_t outputs_for(const state& /*unused*/, std::size_t n) noexcept { return n; }
        static constexpr std::size_t frames_needed(const state& /*unused*/, std::size_t k) noexcept { return k; }

      private:
        Inner m_inner;
    };
    template <typename Inner>
    struct is_stage<steady<Inner>> : std::true_type {};

    // ---- a junction, and the pull side that delivers exactly what was asked ---------------

    /// A single-producer, single-consumer ring of frames (shared clock, two threads).
    template <std::size_t Ch, std::size_t Capacity>
    class handoff {
        static_assert((Capacity & (Capacity - 1)) == 0, "catenae: a handoff's capacity is a power of two");

      public:
        using sink_port   = port<float, Ch>;
        using source_port = port<float, Ch>;

        [[nodiscard]] std::size_t push(span<const float> in) noexcept {
            const std::uint32_t head   = m_head.load(std::memory_order_relaxed);
            const std::uint32_t tail   = m_tail.load(std::memory_order_acquire);
            const std::size_t   room   = Capacity - static_cast<std::size_t>(head - tail);
            const std::size_t   frames = std::min(in.size() / Ch, room);
            for (std::size_t i = 0; i < frames * Ch; ++i) {
                m_ring[((static_cast<std::size_t>(head) % Capacity) * Ch + i) % (Capacity * Ch)] = in[i];
            }
            m_head.store(head + static_cast<std::uint32_t>(frames), std::memory_order_release);
            return frames;
        }
        /// Always fills out; what it could not supply is silence. Returns the real frames.
        [[nodiscard]] std::size_t pull(span<float> out) noexcept {
            const std::uint32_t tail   = m_tail.load(std::memory_order_relaxed);
            const std::uint32_t head   = m_head.load(std::memory_order_acquire);
            const std::size_t   frames = std::min(out.size() / Ch, static_cast<std::size_t>(head - tail));
            for (std::size_t i = 0; i < frames * Ch; ++i) {
                out[i] = m_ring[((static_cast<std::size_t>(tail) % Capacity) * Ch + i) % (Capacity * Ch)];
            }
            std::fill(out.begin() + frames * Ch, out.end(), 0.0f);
            m_tail.store(tail + static_cast<std::uint32_t>(frames), std::memory_order_release);
            return frames;
        }

      private:
        std::array<float, Capacity * Ch> m_ring{};
        std::atomic<std::uint32_t>       m_head{0};
        std::atomic<std::uint32_t>       m_tail{0};
    };

    /// What the pull side remembers: the chain's state, the frames in transit, and the
    /// holdover that lets it deliver exactly the frames asked for. Valid when all zero.
    template <typename Chain>
    struct fed_by_memory {
        static constexpr std::size_t k_in_width  = Chain::input_port::width;
        static constexpr std::size_t k_out_width = Chain::output_port::width;
        static constexpr std::size_t k_overshoot = Chain::max_outputs_for(1) - 1; // frames, at most

        state_of<Chain>                                                                          chain;
        std::array<typename Chain::input_port::value_type, k_block * k_in_width>                   pulled;
        std::array<typename Chain::output_port::value_type, Chain::max_outputs_for(k_block) * k_out_width> made;
        std::array<typename Chain::output_port::value_type, (k_overshoot + 1) * k_out_width>     holdover;
        std::size_t                                                                              held;    // frames in holdover
        std::size_t                                                                              starved; // input frames that were silence
    };
    template <typename Chain>
    using fed_by_state = fed_by_memory<std::remove_cv_t<Chain>>;

    /// Fills `out` with exactly out.size() / width frames of the chain's output, pulling what
    /// the chain needs from the junction. This is where SampleRateTap's pull(out, k) lands.
    template <typename Junction, typename Chain>
    std::size_t fed_by(Junction& junction, const Chain& chain, fed_by_memory<Chain>& st,
                       span<typename Chain::output_port::value_type> out) noexcept {
        using state_t          = fed_by_memory<Chain>;
        const std::size_t want = out.size() / state_t::k_out_width;
        std::size_t       done = std::min(st.held, want);
        std::copy(st.holdover.begin(), st.holdover.begin() + static_cast<std::ptrdiff_t>(done * state_t::k_out_width),
                  out.begin());
        std::copy(st.holdover.begin() + static_cast<std::ptrdiff_t>(done * state_t::k_out_width),
                  st.holdover.begin() + static_cast<std::ptrdiff_t>(st.held * state_t::k_out_width), st.holdover.begin());
        st.held -= done;
        while (done < want) {
            const std::size_t n    = std::min(chain.frames_needed(st.chain, want - done), k_block);
            const std::size_t real = junction.pull(span<typename Chain::input_port::value_type>(st.pulled.data(), n * state_t::k_in_width));
            st.starved += n - real;
            const std::size_t made = chain.run(
                st.chain, span<const typename Chain::input_port::value_type>(st.pulled.data(), n * state_t::k_in_width),
                span<typename Chain::output_port::value_type>(st.made));
            const std::size_t take = std::min(made, want - done);
            std::copy(st.made.begin(), st.made.begin() + static_cast<std::ptrdiff_t>(take * state_t::k_out_width),
                      out.begin() + static_cast<std::ptrdiff_t>(done * state_t::k_out_width));
            st.held = made - take; // the overshoot waits for the next call
            std::copy(st.made.begin() + static_cast<std::ptrdiff_t>(take * state_t::k_out_width),
                      st.made.begin() + static_cast<std::ptrdiff_t>(made * state_t::k_out_width), st.holdover.begin());
            done += take;
        }
        return want;
    }

} // namespace catenae

#endif // CATENAE_SKETCHES_CONFIG_STATE_SKETCH_H
