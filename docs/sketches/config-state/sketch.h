// Copyright 2026 Timothy Place and the catenae contributors
//
// A THROWAWAY SKETCH for PLAN.md. It exists to show what the code would look like and to
// measure where the memory lands. It is not the library: the rate changers are toys (a
// linear-interpolating up2, a two-tap down2), there is one sample type and one layout, and
// nothing here is a contract. README.md lists what it covers and what it does not.
#ifndef CATENAE_SKETCHES_CONFIG_STATE_SKETCH_H
#define CATENAE_SKETCHES_CONFIG_STATE_SKETCH_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <type_traits>
#include <utility>

// Fails the build if a static object's initialiser is not a constant: no start-up code, by
// the compiler's word. (C++20's constinit, spelled the way each compiler offers it at C++17.)
#if defined(__clang__)
#define CATENAE_SKETCH_CONSTINIT [[clang::require_constant_initialization]]
#elif defined(__GNUC__)
#define CATENAE_SKETCH_CONSTINIT __constinit
#else
#define CATENAE_SKETCH_CONSTINIT
#endif

namespace catenae {

    // The integrator defines this (PLAN.md 3.4), as `void catenae::check_failed(int) noexcept`.
    // It must not return. It is declared inside the namespace so that, in the library, it
    // carries the inline namespace's configuration tag like every other symbol.
    [[noreturn]] void check_failed(int code) noexcept;

    /// A boundary check: on in every build. constexpr, so that a checked accessor can be used
    /// in a constant expression, where a failure is a compile error.
    constexpr void expects(bool ok, int code) noexcept {
        if (!ok) {
            check_failed(code);
        }
    }
    /// An audit check, on the library's own promises: only with CATENAE_SKETCH_AUDIT.
#if defined(CATENAE_SKETCH_AUDIT)
    constexpr void audits(bool ok, int code) noexcept {
        if (!ok) {
            check_failed(code);
        }
    }
#else
    constexpr void audits(bool /*ok*/, int /*code*/) noexcept {}
#endif

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
    constexpr ratio operator+(ratio a, ratio b) noexcept {
        const std::size_t u = a.up * b.down + b.up * a.down;
        const std::size_t d = a.down * b.down;
        const std::size_t g = std::gcd(u, d);
        return ratio{u / g, d / g};
    }
    constexpr ratio operator/(ratio a, ratio b) noexcept {
        return a * ratio{b.down, b.up};
    }
    /// The smallest whole number not below r.
    constexpr std::size_t ceiling(ratio r) noexcept {
        return (r.up + r.down - 1) / r.down;
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
    // calls is a separate STATE: plain storage, with no constructor and no initialisers, so a
    // static one is in .bss. The configuration RESETS the state before its first use; storage
    // that is all zero means "not yet reset", not "at rest". A stage says how its state comes
    // to rest, one way or the other: it writes reset(state&) const noexcept, or it declares
    // rests_at_zero = true and the state is zeroed in place. What a stage needs only while a
    // call runs is a WORKSPACE, which carries nothing between calls and so is shared by every
    // state run on one thread (most stages have none); it needs no reset.
    //
    // D35. A stage also publishes how its output arrives, counted from the reset state. With
    // N frames in so far and O(N) frames out, its output is "owed" N L/M:
    //   input_group     the smallest n for which O(k n) is exactly k n L/M for every k: 1 for a
    //                   stage that keeps pace frame by frame, M for a decimator, the hop for a
    //                   stage that works in blocks;
    //   max_shortfall   the most output frames by which O(N) trails N L/M, over every N;
    //   max_lead        the most by which O(N) runs ahead of N L/M, over every N. Both are
    //                   ratios, since half an output frame is a real answer for a decimator;
    //   output_quantum  its output count is always a whole multiple of this: 1, unless it
    //                   writes whole blocks.
    // A stage that writes a group's output when the group's LAST frame arrives only trails; one
    // that writes it when the FIRST arrives only leads (the decimators catenae carries do this).
    // When frames reach a stage in whole multiples of q, it trails or leads by at most its
    // number scaled by (group - gcd(q, group)) / (group - 1): a stage of 128 fed in 96s is
    // never more than 96 behind. A leaf stage must honour that law; a chain is composed
    // exactly, carrying q from stage to stage.
    // =====================================================================================

    /// A stage with no scratch says so with this.
    struct no_workspace {};

    /// Plain storage: what a state, a workspace and a junction's state all are. It has no
    /// constructor to run, so a static one costs no start-up code at C++17, 20 or 23.
    template <typename T>
    inline constexpr bool is_plain =
        std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && std::is_trivially_default_constructible_v<T>;

    namespace detail {
        // How a stage says its state comes to rest. Nothing is inferred from a missing member:
        // these two are read only to insist that a stage declares exactly one of them.
        template <typename Stage, typename = void>
        struct declares_rest_at_zero : std::false_type {};
        template <typename Stage>
        struct declares_rest_at_zero<Stage, std::void_t<decltype(Stage::rests_at_zero)>> : std::bool_constant<Stage::rests_at_zero> {};
        template <typename Stage, typename = void>
        struct writes_reset : std::false_type {};
        template <typename Stage>
        struct writes_reset<Stage, std::void_t<decltype(std::declval<const Stage&>().reset(
                                       std::declval<typename Stage::state&>()))>> : std::true_type {};
    } // namespace detail

    /// Puts a state at rest, in place: the stage's own reset(), or zeros for a stage that
    /// declares it rests at zero. No temporary the size of the state is ever built.
    template <typename Stage>
    void reset_state(const Stage& stage, typename Stage::state& st) noexcept {
        static_assert(is_plain<typename Stage::state>,
                      "catenae: a state is a plain aggregate (trivially copyable, standard layout, no constructor)");
        static_assert(detail::declares_rest_at_zero<Stage>::value != detail::writes_reset<Stage>::value,
                      "catenae: a stage writes reset(state&) const noexcept, or declares rests_at_zero = true: exactly one");
        static_assert(std::numeric_limits<float>::is_iec559, "catenae: zero bytes are a zero float");
        if constexpr (detail::writes_reset<Stage>::value) {
            static_assert(noexcept(stage.reset(st)), "catenae: reset(state&) const is noexcept");
            stage.reset(st);
        }
        else {
            static_cast<void>(stage);
            std::memset(&st, 0, sizeof st);
        }
    }

    /// out = f(in), a sample at a time.
    template <std::size_t Ch, typename F>
    class map {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio       rate_ratio{1, 1};
        static constexpr std::size_t input_group = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum = 1;
        static constexpr bool        rests_at_zero  = true;

        struct state {};
        using workspace = no_workspace;

        explicit constexpr map(F f) noexcept
            : m_f(f) {}

        constexpr const F& function() const noexcept { return m_f; }
        constexpr F&       function() noexcept { return m_f; }

        std::size_t run(state& /*unused*/, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
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
        static constexpr ratio       rate_ratio{1, 1};
        static constexpr std::size_t input_group = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum = 1;

        struct state {
            float         current;   // the level being applied now
            float         goal;      // the level the ramp is heading for
            float         step;      // per frame, while a ramp runs
            std::uint32_t remaining; // frames of ramp left
        };
        using workspace = no_workspace;

        constexpr gain(float level, std::uint32_t ramp_frames) noexcept
            : m_level(level)
            , m_ramp_frames(ramp_frames) {}

        constexpr float level() const noexcept { return m_level; }
        /// Takes effect from the next run(), as a ramp. Not synchronised: call it on the thread
        /// that runs the chain, between calls.
        constexpr void set_level(float level) noexcept { m_level = level; }

        /// At rest the gain is at its level, not at zero: so this stage writes its reset.
        void reset(state& st) const noexcept {
            st.current   = m_level;
            st.goal      = m_level;
            st.step      = 0.0f;
            st.remaining = 0;
        }

        std::size_t run(state& st, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
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

    /// A value or the reason there is none, after std::expected. No heap, no exceptions. T and
    /// E are trivially destructible (a design, a status), which is what lets it be constexpr.
    template <typename T, typename E>
    class [[nodiscard]] expected {
        static_assert(std::is_trivially_destructible_v<T> && std::is_trivially_destructible_v<E>,
                      "catenae: expected<T, E> holds trivially destructible types");

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
        /// What can be checked without libm is checked: the poles are inside the unit circle
        /// (|a2| < 1 and |a1| < 1 + a2). A bad table that initialises a constexpr design does
        /// not compile; one met at run time reaches the handler.
        static constexpr lowpass_design from_table(const std::array<S, 5>& coefficients, float latency_frames) noexcept {
            const S a1 = coefficients[3];
            const S a2 = coefficients[4];
            expects(a2 < S(1) && a2 > S(-1), 6);
            expects(a1 < S(1) + a2 && a1 > -(S(1) + a2), 6);
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
        static constexpr ratio       rate_ratio{1, 1};
        static constexpr std::size_t input_group = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum = 1;
        static constexpr bool        rests_at_zero  = true; // an empty history

        struct state {
            std::array<std::array<float, 2>, Ch> z; // transposed direct form II, per channel
        };
        using workspace = no_workspace;

        explicit constexpr lowpass(const lowpass_design<float>& design) noexcept
            : m_design(design) {}

        constexpr const lowpass_design<float>& design() const noexcept { return m_design; }
        /// A new cutoff is a new design. This cannot fail: the design was checked when it was
        /// made. The filter's history carries on under the new coefficients.
        constexpr void  set_design(const lowpass_design<float>& design) noexcept { m_design = design; }
        constexpr float nominal_latency() const noexcept { return m_design.nominal_latency(); }

        std::size_t run(state& st, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
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
        static constexpr ratio       rate_ratio{2, 1};
        static constexpr std::size_t input_group = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum = 2;
        static constexpr bool        rests_at_zero  = true; // silence before the stream

        struct state {
            std::array<float, Ch> previous;
        };
        using workspace = no_workspace;

        constexpr up2() noexcept = default;

        std::size_t run(state& st, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            for (std::size_t i = 0; i < frames; ++i) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    const float x             = in[i * Ch + c];
                    out[(2 * i) * Ch + c]     = 0.5f * (st.previous[c] + x);
                    out[(2 * i + 1) * Ch + c] = x;
                    st.previous[c]            = x;
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

    /// Half the rate (a toy: each output is the mean of a frame and the one before it). It
    /// writes an output when the FIRST frame of a pair arrives, as the decimators catenae
    /// carries do: one frame in from rest gives one frame out. So it never trails, and with
    /// one frame of a pair taken it is half an output frame ahead.
    template <std::size_t Ch>
    class down2 {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio       rate_ratio{1, 2};
        static constexpr std::size_t input_group = 2;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{1, 2};
        static constexpr std::size_t output_quantum = 1;
        static constexpr bool        rests_at_zero  = true; // silence before the stream, a pair about to start

        struct state {
            std::array<float, Ch> previous;
            std::size_t           taken; // 0 or 1 frames of the present pair already in
        };
        using workspace = no_workspace;

        constexpr down2() noexcept = default;

        std::size_t run(state& st, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
            const std::size_t frames = in.size() / Ch;
            std::size_t       made   = 0;
            for (std::size_t i = 0; i < frames; ++i) {
                if (st.taken == 0) {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[made * Ch + c] = 0.5f * (st.previous[c] + in[i * Ch + c]);
                    }
                    ++made;
                }
                for (std::size_t c = 0; c < Ch; ++c) {
                    st.previous[c] = in[i * Ch + c];
                }
                st.taken = 1 - st.taken;
            }
            return made;
        }
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return (n + 1) / 2; }
        static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept { return (n + 1 - st.taken) / 2; }
        static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
            return k == 0 ? 0 : 2 * k - 1 + st.taken;
        }
    };
    template <std::size_t Ch>
    struct is_stage<down2<Ch>> : std::true_type {};

    /// The other convention, kept to test the composition against both: it writes a pair's
    /// output when its LAST frame arrives, so it never leads and can be half a frame behind.
    template <std::size_t Ch>
    class down2_late {
      public:
        using input_port  = port<float, Ch>;
        using output_port = port<float, Ch>;
        static constexpr ratio       rate_ratio{1, 2};
        static constexpr std::size_t input_group = 2;
        static constexpr ratio       max_shortfall{1, 2};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum = 1;
        static constexpr bool        rests_at_zero  = true;

        struct state {
            std::array<float, Ch> held;
            std::size_t           have; // 0 or 1 frames waiting for their pair
        };
        using workspace = no_workspace;

        constexpr down2_late() noexcept = default;

        std::size_t run(state& st, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
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
    struct is_stage<down2_late<Ch>> : std::true_type {};

    /// Works on whole blocks of Hop frames: nothing comes out until a block is in, then Hop
    /// frames come out at once. This is how stft | spectral stage | istft behaves per call,
    /// and it stands in for that region here (mono, f applied to each sample of the block).
    /// Its input group is the hop, it can be Hop - 1 frames behind, and it only ever writes
    /// whole blocks.
    template <std::size_t Hop, typename F>
    class blockwise {
      public:
        using input_port  = port<float, 1>;
        using output_port = port<float, 1>;
        static constexpr ratio       rate_ratio{1, 1};
        static constexpr std::size_t input_group = Hop;
        static constexpr ratio       max_shortfall{Hop - 1, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum = Hop;
        static constexpr bool        rests_at_zero  = true; // an empty block

        struct state {
            std::array<float, Hop> block;
            std::size_t            have;
        };
        using workspace = no_workspace;

        explicit constexpr blockwise(F f) noexcept
            : m_f(f) {}

        std::size_t run(state& st, workspace& /*unused*/, span<const float> in, span<float> out) const noexcept {
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

    /// The memory of a chain or a stage: what one instance (one voice) remembers.
    template <typename Config>
    using state_of = typename Config::state;
    /// The scratch of a chain or a stage: one per thread, shared by every state of that type.
    /// Nothing in it needs initialising and nothing in it survives a call.
    template <typename Config>
    using workspace_of = typename Config::workspace;
    /// Both, for the common case of one instance.
    template <typename Config>
    struct memory {
        state_of<Config>     state;
        workspace_of<Config> workspace;
    };
    template <typename Config>
    using memory_of = memory<std::remove_cv_t<Config>>;

    template <typename... Stages>
    class chain;

    namespace detail {

        // Things held one after another: a plain recursive aggregate. A chain keeps its
        // stages' configurations, states and workspaces this way (std::tuple is neither
        // trivially copyable nor standard layout, and its constructor zero-fills).
        template <typename... Ts>
        struct pack;
        template <>
        struct pack<> {};
        template <typename T>
        struct pack<T> {
            T first;
        };
        template <typename T, typename U, typename... Rest>
        struct pack<T, U, Rest...> {
            T                first;
            pack<U, Rest...> rest;
        };
        template <std::size_t I, typename T, typename... Rest>
        constexpr auto& item(pack<T, Rest...>& p) noexcept {
            if constexpr (I == 0) {
                return p.first;
            }
            else {
                return item<I - 1>(p.rest);
            }
        }
        template <std::size_t I, typename T, typename... Rest>
        constexpr const auto& item(const pack<T, Rest...>& p) noexcept {
            if constexpr (I == 0) {
                return p.first;
            }
            else {
                return item<I - 1>(p.rest);
            }
        }
        template <typename T>
        constexpr pack<T> packed(const T& only) noexcept {
            return pack<T>{only};
        }
        template <typename T, typename U, typename... Rest>
        constexpr pack<T, U, Rest...> packed(const T& head, const U& next, const Rest&... rest) noexcept {
            return pack<T, U, Rest...>{head, packed(next, rest...)};
        }
        template <std::size_t I, typename... Ts>
        struct pack_element;
        template <std::size_t I, typename T, typename... Rest>
        struct pack_element<I, T, Rest...> : pack_element<I - 1, Rest...> {};
        template <typename T, typename... Rest>
        struct pack_element<0, T, Rest...> {
            using type = T;
        };

        // One joint: a stage's output port is the next stage's input port. The chain derives
        // from joints<>, so every joint is checked when the chain's type is formed, whether or
        // not anything is ever called on it.
        template <std::size_t Index, typename OutputPort, typename InputPort>
        struct joint {
            static_assert(std::is_same_v<OutputPort, InputPort>,
                          "catenae: one stage's output port is not the next stage's input port");
        };
        template <std::size_t Index, typename... Stages>
        struct joints {};
        template <std::size_t Index, typename A, typename B, typename... Rest>
        struct joints<Index, A, B, Rest...>
            : joint<Index, typename A::output_port, typename B::input_port>
            , joints<Index + 1, B, Rest...> {};

        // The smallest n for which the frames arriving at every stage are a whole number of
        // that stage's input groups.
        template <typename... Stages>
        constexpr std::size_t composed_group() noexcept {
            const std::size_t groups[] = {Stages::input_group...};
            const ratio       ratios[] = {Stages::rate_ratio...};
            std::size_t       n        = 1;
            ratio             before{1, 1}; // the ratio from the chain's input to this stage's input
            for (std::size_t i = 0; i < sizeof...(Stages); ++i) {
                const std::size_t need = before.down * groups[i]; // n * before.up must be a multiple of this
                n                      = n * (need / std::gcd(n * before.up, need));
                before                 = before * ratios[i];
            }
            return n;
        }

        // How far a configuration's output can trail and lead what it is owed, and in what
        // multiples it is written, when frames reach it in whole multiples of q.
        struct pace {
            ratio       shortfall;
            ratio       lead;
            std::size_t quantum;
        };

        // A leaf stage: its published numbers, by the law stated above D35's list.
        template <typename Stage>
        struct arrival {
            static constexpr pace at(std::size_t q) noexcept {
                const std::size_t group = Stage::input_group;
                const ratio       rate  = Stage::rate_ratio;
                if (q % group == 0) { // whole groups in: exactly what is owed comes out
                    return pace{ratio{0, 1}, ratio{0, 1}, (q / group) * (group * rate.up / rate.down)};
                }
                const ratio scale{group - std::gcd(q, group), group - 1};
                return pace{Stage::max_shortfall * scale, Stage::max_lead * scale, Stage::output_quantum};
            }
        };

        // A chain: each stage's numbers at the quantum that reaches it, carried to the chain's
        // output rate and summed. Sufficient always, and exact for two block stages.
        template <typename Stage, typename... Rest>
        constexpr pace composed_pace(std::size_t q) noexcept {
            const pace mine = arrival<Stage>::at(q);
            if constexpr (sizeof...(Rest) == 0) {
                return mine;
            }
            else {
                const ratio after = (Rest::rate_ratio * ...); // from this stage's output to the chain's
                const pace  rest  = composed_pace<Rest...>(mine.quantum);
                return pace{mine.shortfall * after + rest.shortfall, mine.lead * after + rest.lead, rest.quantum};
            }
        }
        template <typename... Stages>
        struct arrival<chain<Stages...>> {
            static constexpr pace at(std::size_t q) noexcept { return composed_pace<Stages...>(q); }
        };

    } // namespace detail

    template <typename... Stages>
    class chain : private detail::joints<0, Stages...> {
        static_assert(sizeof...(Stages) > 0, "catenae: a chain has at least one stage");
        static_assert(((Stages::input_group != 1 ||
                        (Stages::max_shortfall == ratio{0, 1} && Stages::max_lead == ratio{0, 1})) &&
                       ...),
                      "catenae: a stage whose input group is 1 neither trails nor leads");
        static constexpr std::size_t k_count = sizeof...(Stages);
        template <std::size_t I>
        using stage_t = typename detail::pack_element<I, Stages...>::type;

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

        template <std::size_t I>
        using scratch_t = std::array<typename stage_t<I>::output_port::value_type, bound<I>() * stage_t<I>::output_port::width>;
        template <typename Seq>
        struct scratch_of;
        template <std::size_t... I>
        struct scratch_of<std::index_sequence<I...>> {
            using type = detail::pack<scratch_t<I>...>;
        };

      public:
        using input_port  = typename stage_t<0>::input_port;
        using output_port = typename stage_t<k_count - 1>::output_port;
        using sample_in   = typename input_port::value_type;
        using sample_out  = typename output_port::value_type;

        static constexpr ratio       rate_ratio  = (Stages::rate_ratio * ...);
        static constexpr std::size_t input_group = detail::composed_group<Stages...>();
        /// Derived, never declared: n frames in always write exactly n L/M, for any n.
        static constexpr bool is_frame_synchronous = input_group == 1;
        /// How far the chain's output can trail and lead what it is owed, counted from reset,
        /// and the multiples it is written in: composed from the stages' numbers.
        static constexpr ratio       max_shortfall  = detail::composed_pace<Stages...>(1).shortfall;
        static constexpr ratio       max_lead       = detail::composed_pace<Stages...>(1).lead;
        static constexpr std::size_t output_quantum = detail::composed_pace<Stages...>(1).quantum;

        /// What one instance remembers: its stages' histories and nothing else. Plain storage,
        /// so a static one is in .bss: no flash, no start-up code. reset() makes it ready.
        struct state {
            detail::pack<typename Stages::state...> stages;
            std::uint32_t                           ready; // k_ready once reset; always present, checked at the audit level
        };
        /// What a call needs and no instance keeps: the buffers between the stages, and the
        /// stages' own scratch. One per thread serves every state of this chain type. Plain
        /// storage too: nothing fills it, wherever it is.
        struct workspace {
            detail::pack<typename Stages::workspace...>                      stages;
            typename scratch_of<std::make_index_sequence<k_count - 1>>::type scratch;
        };

        explicit constexpr chain(const Stages&... stages) noexcept
            : m_stages(detail::packed(stages...)) {}

        /// One stage's configuration, by its position in the chain.
        template <std::size_t I>
        constexpr const stage_t<I>& stage() const noexcept {
            return detail::item<I>(m_stages);
        }
        template <std::size_t I>
        constexpr stage_t<I>& stage() noexcept {
            return detail::item<I>(m_stages);
        }
        /// One stage's part of a state, by the same position: where a meter's reading is read.
        template <std::size_t I>
        static constexpr const state_of<stage_t<I>>& stage_state(const state& st) noexcept {
            return detail::item<I>(st.stages);
        }
        template <std::size_t I>
        static constexpr state_of<stage_t<I>>& stage_state(state& st) noexcept {
            return detail::item<I>(st.stages);
        }

        /// Puts a state at rest: each stage resets its own part, in place. Before the first
        /// process(), and afterwards only between calls, on the thread that runs the chain.
        void reset(state& st) const noexcept {
            reset_each<0>(st);
            st.ready = k_ready;
        }
        static constexpr bool is_reset(const state& st) noexcept { return st.ready == k_ready; }

        /// Non-decreasing in n, as every stage's is: scratch and FIFOs are sized from it.
        static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return fold_max<0>(n); }
        constexpr std::size_t outputs_for(const state& st, std::size_t n) const noexcept { return fold_out<0>(st, n); }
        constexpr std::size_t frames_needed(const state& st, std::size_t k) const noexcept {
            return fold_need<k_count - 1>(st, k);
        }

        /// The trusted form: what an enclosing chain or an adapter calls.
        std::size_t run(state& st, workspace& ws, span<const sample_in> in, span<sample_out> out) const noexcept {
            static_assert(is_plain<workspace>, "catenae: a workspace is a plain aggregate (no constructor fills it)");
            const std::size_t width  = input_port::width;
            const std::size_t frames = in.size() / width;
            std::size_t       done   = 0;
            std::size_t       made   = 0;
            while (done < frames) {
                const std::size_t n = std::min(k_block, frames - done);
                made += step<0>(st, ws, span<const sample_in>(in.data() + done * width, n * width),
                                out.subspan(made * output_port::width));
                done += n;
            }
            return made;
        }

        /// The caller's entry: the one run-time check, then run().
        [[nodiscard]] std::size_t process(state& st, workspace& ws, span<const sample_in> in,
                                          span<sample_out> out) const noexcept {
            expects(in.size() % input_port::width == 0, 1);
            expects(out.size() >= outputs_for(st, in.size() / input_port::width) * output_port::width, 2);
            audits(is_reset(st), 10); // reset() was never called on this state
            return run(st, ws, in, out);
        }
        [[nodiscard]] std::size_t process(memory<chain>& both, span<const sample_in> in,
                                          span<sample_out> out) const noexcept {
            return process(both.state, both.workspace, in, out);
        }

        /// The caller's entry for fixed blocks: checked by the compiler, nothing at run time.
        /// A block that is a whole number of input groups is written as exactly N L/M frames
        /// as long as every call on this state is such a block.
        template <std::size_t N, std::size_t M>
        [[nodiscard]] std::size_t process(state& st, workspace& ws, const std::array<sample_in, N>& in,
                                          std::array<sample_out, M>& out) const noexcept {
            static_assert(N % input_port::width == 0, "catenae: the input is not a whole number of frames");
            static_assert((N / input_port::width) % input_group == 0,
                          "catenae: a fixed block must be a whole number of the chain's input groups; wrap the chain in steady<>");
            static_assert(M >= max_outputs_for(N / input_port::width) * output_port::width,
                          "catenae: the output block is too small for this input block");
            audits(is_reset(st), 10);
            return run(st, ws, span<const sample_in>(in), span<sample_out>(out));
        }
        template <std::size_t N, std::size_t M>
        [[nodiscard]] std::size_t process(memory<chain>& both, const std::array<sample_in, N>& in,
                                          std::array<sample_out, M>& out) const noexcept {
            return process(both.state, both.workspace, in, out);
        }

      private:
        static constexpr std::uint32_t k_ready = 0x72656164U;

        template <std::size_t I>
        void reset_each(state& st) const noexcept {
            reset_state(detail::item<I>(m_stages), detail::item<I>(st.stages));
            if constexpr (I + 1 < k_count) {
                reset_each<I + 1>(st);
            }
        }
        template <std::size_t I>
        std::size_t step(state& st, workspace& ws, span<const typename stage_t<I>::input_port::value_type> in,
                         span<sample_out> out) const noexcept {
            const auto& stage = detail::item<I>(m_stages);
            auto&       mine  = detail::item<I>(st.stages);
            auto&       work  = detail::item<I>(ws.stages);
            if constexpr (I + 1 == k_count) {
                return stage.run(mine, work, in, out);
            }
            else {
                auto&             buffer = detail::item<I>(ws.scratch);
                const std::size_t made   = stage.run(mine, work, in, span<typename scratch_t<I>::value_type>(buffer));
                audits(made <= bound<I>(), 7);
                return step<I + 1>(
                    st, ws,
                    span<const typename scratch_t<I>::value_type>(buffer.data(), made * stage_t<I>::output_port::width), out);
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
            const std::size_t made = detail::item<I>(m_stages).outputs_for(detail::item<I>(st.stages), n);
            if constexpr (I + 1 == k_count) {
                return made;
            }
            else {
                return fold_out<I + 1>(st, made);
            }
        }
        template <std::size_t I>
        constexpr std::size_t fold_need(const state& st, std::size_t k) const noexcept {
            const std::size_t n = detail::item<I>(m_stages).frames_needed(detail::item<I>(st.stages), k);
            if constexpr (I == 0) {
                return n;
            }
            else {
                return fold_need<I - 1>(st, n);
            }
        }

        // The configurations: this is all a chain object holds. A plain aggregate, so a chain's
        // configuration is trivially copyable when its stages' are, and can travel in a mailbox.
        detail::pack<Stages...> m_stages;
    };
    template <typename... Stages>
    chain(Stages...) -> chain<Stages...>;
    template <typename... Stages>
    struct is_stage<chain<Stages...>> : std::true_type {};

    template <typename T>
    struct is_chain : std::false_type {};
    template <typename... Stages>
    struct is_chain<chain<Stages...>> : std::true_type {};

    namespace detail {
        template <typename... As, typename... Bs, std::size_t... I, std::size_t... J>
        constexpr chain<As..., Bs...> joined(const chain<As...>& a, const chain<Bs...>& b, std::index_sequence<I...> /*unused*/,
                                             std::index_sequence<J...> /*unused*/) noexcept {
            return chain<As..., Bs...>(a.template stage<I>()..., b.template stage<J>()...);
        }
        template <typename T>
        inline constexpr bool is_lone_stage = is_stage<T>::value && !is_chain<T>::value;
    } // namespace detail

    // a | b | c composes CONFIGURATIONS, which are a few bytes each. No history moves. It
    // always flattens, whichever side a chain is on: (a | b) | (c | d) is chain<a, b, c, d>.
    template <typename... As, typename... Bs>
    constexpr chain<As..., Bs...> operator|(const chain<As...>& a, const chain<Bs...>& b) noexcept {
        return detail::joined(a, b, std::index_sequence_for<As...>{}, std::index_sequence_for<Bs...>{});
    }
    template <typename A, typename B, std::enable_if_t<detail::is_lone_stage<A> && detail::is_lone_stage<B>, int> = 0>
    constexpr chain<A, B> operator|(const A& a, const B& b) noexcept {
        return chain<A, B>(a, b);
    }
    template <typename... As, typename B, std::enable_if_t<detail::is_lone_stage<B>, int> = 0>
    constexpr chain<As..., B> operator|(const chain<As...>& a, const B& b) noexcept {
        return a | chain<B>(b);
    }
    template <typename A, typename... Bs, std::enable_if_t<detail::is_lone_stage<A>, int> = 0>
    constexpr chain<A, Bs...> operator|(const A& a, const chain<Bs...>& b) noexcept {
        return chain<A>(a) | b;
    }
    // One side a stage and the other not: say so, instead of the compiler's "no match".
    template <typename A, typename B, std::enable_if_t<is_stage<A>::value != is_stage<B>::value, int> = 0>
    constexpr void operator|(const A& /*unused*/, const B& /*unused*/) noexcept {
        static_assert(is_stage<A>::value && is_stage<B>::value, "catenae: both sides of | must be stages");
    }

    // =====================================================================================
    // D35. A chain whose ratio is 1:1 need not return n frames for n. steady<> makes it so.
    // =====================================================================================

    /// Wraps a 1:1 configuration that writes in bursts and presents a frame-synchronous one:
    /// n frames out for every n in. The price is a FIFO and the inner configuration's worst
    /// shortfall as added latency, both stated. Its reset primes the FIFO with that many frames
    /// of silence, so it can always pay out n frames however far behind the inner output is;
    /// and the FIFO has room for the inner output's worst lead as well as its largest burst.
    template <typename Inner>
    class steady {
        static_assert(Inner::rate_ratio == (ratio{1, 1}), "catenae: steady<> is for a 1:1 configuration");
        using sample = typename Inner::output_port::value_type;
        static constexpr std::size_t k_width    = Inner::output_port::width;
        static constexpr std::size_t k_primed   = ceiling(detail::arrival<Inner>::at(1).shortfall);
        static constexpr std::size_t k_ahead    = ceiling(detail::arrival<Inner>::at(1).lead);
        static constexpr std::size_t k_capacity = k_primed + k_ahead + Inner::max_outputs_for(k_block);

      public:
        using input_port  = typename Inner::input_port;
        using output_port = typename Inner::output_port;
        static constexpr ratio       rate_ratio           = Inner::rate_ratio;
        static constexpr std::size_t input_group          = 1;
        static constexpr ratio       max_shortfall{0, 1};
        static constexpr ratio       max_lead{0, 1};
        static constexpr std::size_t output_quantum       = 1;
        static constexpr std::size_t added_latency_frames = k_primed;

        struct state {
            state_of<Inner>                          inner;
            std::array<sample, k_capacity * k_width> fifo;
            std::size_t                              read;
            std::size_t                              held; // frames in the FIFO: k_primed at rest, never fewer than a call needs
        };
        struct workspace {
            workspace_of<Inner>                                           inner;
            std::array<sample, Inner::max_outputs_for(k_block) * k_width> burst; // what one inner call wrote
        };

        explicit constexpr steady(const Inner& inner) noexcept
            : m_inner(inner) {}

        /// At rest the FIFO holds k_primed frames of silence: that is not all zeros, so this
        /// stage writes its reset.
        void reset(state& st) const noexcept {
            reset_state(m_inner, st.inner);
            st.fifo.fill(sample{});
            st.read = 0;
            st.held = k_primed;
        }

        std::size_t run(state& st, workspace& ws, span<const typename input_port::value_type> in,
                        span<sample> out) const noexcept {
            const std::size_t frames = in.size() / input_port::width;
            std::size_t       done   = 0;
            while (done < frames) {
                const std::size_t n    = std::min(k_block, frames - done);
                const std::size_t made = m_inner.run(
                    st.inner, ws.inner,
                    span<const typename input_port::value_type>(in.data() + done * input_port::width, n * input_port::width),
                    span<sample>(ws.burst));
                // The inner output is never further ahead than its max_lead, so what the FIFO
                // holds and this burst fit; and never further behind than its max_shortfall, so
                // the FIFO then holds the n frames this call pays out. Both at the audit level.
                audits(st.held + made <= k_capacity, 11);
                audits(st.held + made >= n, 5);
                const std::size_t write = (st.read + st.held) % k_capacity;
                for (std::size_t i = 0; i < made * k_width; ++i) {
                    st.fifo[(write * k_width + i) % (k_capacity * k_width)] = ws.burst[i];
                }
                for (std::size_t i = 0; i < n * k_width; ++i) {
                    out[done * k_width + i] = st.fifo[(st.read * k_width + i) % (k_capacity * k_width)];
                }
                st.read = (st.read + n) % k_capacity;
                st.held = st.held + made - n;
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
    steady(Inner) -> steady<Inner>;
    template <typename Inner>
    struct is_stage<steady<Inner>> : std::true_type {};

    // ---- a junction, and the pull side that delivers exactly what was asked ---------------

    namespace detail {
        // A 32-bit word that two threads share. It is a plain word, read and written through
        // the compiler's atomic operations, so the state that holds it is plain storage at
        // every standard (std::atomic has a constructor from C++20 on, and a state with one
        // gets start-up code). This is the one place the sketch uses a compiler extension.
        struct word {
            std::uint32_t value;
        };
        inline std::uint32_t load_relaxed(const word& w) noexcept { return __atomic_load_n(&w.value, __ATOMIC_RELAXED); }
        inline std::uint32_t load_acquire(const word& w) noexcept { return __atomic_load_n(&w.value, __ATOMIC_ACQUIRE); }
        inline void store_relaxed(word& w, std::uint32_t v) noexcept { __atomic_store_n(&w.value, v, __ATOMIC_RELAXED); }
        inline void store_release(word& w, std::uint32_t v) noexcept { __atomic_store_n(&w.value, v, __ATOMIC_RELEASE); }
    } // namespace detail

    /// What a third thread may read of a junction. Each field is read on its own.
    struct handoff_report {
        bool          running; // false while the ring fills to its target
        std::uint32_t fill_frames;
        std::uint32_t underruns;
        std::uint32_t overruns;
    };

    /// A single-producer, single-consumer ring of frames (shared clock, two threads). Like a
    /// stage it is a configuration and a state; its configuration is its template arguments,
    /// so the object is empty. Push is the producer's largest push.
    ///
    /// Its fill policy, as numbers: pull delivers silence until the ring holds Target frames;
    /// on that pull it discards the oldest frames above Target and starts paying out, so the
    /// fill a pull sees when it starts, or starts again, is exactly Target whichever side ran
    /// first and however long. A pull the ring cannot fill is an underrun: it delivers what
    /// there is and sends the ring back to filling. A push the ring cannot hold is an overrun
    /// and drops the newest frames. While it runs, the fill a pull sees stays within one push
    /// of Target; that is the junction's latency, nominal Target. If the consumer stalls while
    /// the producer runs, the fill and the latency rise by what was missed and stay there
    /// until an overrun or restart().
    ///
    /// For the fill never to run out, Target is at least the largest pull, plus the largest
    /// push, less a number that divides every push and every pull (1 if their sizes vary):
    /// serves() says whether it is.
    template <std::size_t Ch, std::size_t Capacity, std::size_t Target, std::size_t Push>
    class handoff {
        static_assert((Capacity & (Capacity - 1)) == 0, "catenae: a handoff's capacity is a power of two");
        static_assert(Target > 0 && Push > 0, "catenae: a handoff's target fill and largest push are not zero");
        static_assert(Target + Push <= Capacity, "catenae: a handoff's capacity holds its target and one push");
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "catenae: a handoff needs a lock-free 32-bit atomic");

      public:
        using sink_port   = port<float, Ch>;
        using source_port = port<float, Ch>;
        static constexpr std::size_t capacity_frames = Capacity;
        static constexpr std::size_t target_frames   = Target;
        static constexpr std::size_t largest_push    = Push;
        static constexpr bool        rests_at_zero   = true; // an empty ring, filling, no counts

        /// Whether the ring serves pulls of up to largest_pull frames without running out.
        /// quantum divides every push and every pull; pass 1 when their sizes vary.
        static constexpr bool serves(std::size_t largest_pull, std::size_t quantum) noexcept {
            return Push % quantum == 0 && Target + quantum >= largest_pull + Push;
        }

        struct state {
            std::array<float, Capacity * Ch> ring;
            detail::word                     head;      // written by the producer
            detail::word                     tail;      // written by the consumer
            detail::word                     running;   // written by the consumer; 0 is filling
            detail::word                     underruns; // written by the consumer
            detail::word                     overruns;  // written by the producer
        };

        constexpr handoff() noexcept = default;

        /// Empties the ring and its counts. Only while neither side is running. Its rest state
        /// is all zeros, so a static state that was never reset is already at rest: a junction's
        /// state carries no ready word.
        void reset(state& st) const noexcept {
            static_assert(is_plain<state>, "catenae: a junction's state is a plain aggregate");
            detail::store_relaxed(st.head, 0);
            detail::store_relaxed(st.tail, 0);
            detail::store_relaxed(st.running, 0);
            detail::store_relaxed(st.underruns, 0);
            detail::store_release(st.overruns, 0);
        }

        /// The junction's latency in frames, nominal. A constant here; the state is an argument
        /// because a junction that follows a clock moves its target as it runs.
        constexpr std::size_t latency(const state& /*unused*/) const noexcept { return Target; }

        /// The producer's thread (exactly one). At most Push frames. Returns the frames accepted.
        [[nodiscard]] std::size_t push(state& st, span<const float> in) const noexcept {
            const std::size_t asked = in.size() / Ch;
            expects(asked <= Push, 12);
            const std::uint32_t head   = detail::load_relaxed(st.head);
            const std::uint32_t tail   = detail::load_acquire(st.tail);
            const std::size_t   room   = Capacity - static_cast<std::size_t>(head - tail);
            const std::size_t   frames = std::min(asked, room);
            for (std::size_t i = 0; i < frames * Ch; ++i) {
                st.ring[((static_cast<std::size_t>(head) % Capacity) * Ch + i) % (Capacity * Ch)] = in[i];
            }
            detail::store_release(st.head, head + static_cast<std::uint32_t>(frames));
            if (frames < asked) { // one writer, so a load and a store, not a fetch-and-add
                detail::store_relaxed(st.overruns, detail::load_relaxed(st.overruns) + 1U);
            }
            return frames;
        }

        /// The consumer's thread (exactly one other). At most Target frames. Always fills out;
        /// what it could not supply is silence. Returns the real frames.
        [[nodiscard]] std::size_t pull(state& st, span<float> out) const noexcept {
            const std::size_t want = out.size() / Ch;
            expects(want <= Target, 13);
            std::uint32_t       tail = detail::load_relaxed(st.tail);
            const std::uint32_t head = detail::load_acquire(st.head);
            std::size_t         fill = static_cast<std::size_t>(head - tail);
            if (detail::load_relaxed(st.running) == 0) {
                if (fill < Target) {
                    std::fill(out.begin(), out.end(), 0.0f);
                    return 0;
                }
                tail = head - static_cast<std::uint32_t>(Target); // discard the oldest frames above the target
                fill = Target;
                detail::store_relaxed(st.running, 1);
            }
            const std::size_t frames = std::min(want, fill);
            for (std::size_t i = 0; i < frames * Ch; ++i) {
                out[i] = st.ring[((static_cast<std::size_t>(tail) % Capacity) * Ch + i) % (Capacity * Ch)];
            }
            std::fill(out.begin() + frames * Ch, out.end(), 0.0f);
            detail::store_release(st.tail, tail + static_cast<std::uint32_t>(frames));
            if (frames < want) {
                detail::store_relaxed(st.underruns, detail::load_relaxed(st.underruns) + 1U);
                detail::store_relaxed(st.running, 0); // fill to the target again
            }
            return frames;
        }

        /// The consumer's thread, while the producer runs: discard what is buffered and fill
        /// to the target again. (reset() needs both sides at rest; this does not.)
        void restart(state& st) const noexcept {
            detail::store_release(st.tail, detail::load_acquire(st.head));
            detail::store_relaxed(st.running, 0);
        }

        /// Any thread. The consumer's index is read first and the producer's second, so the
        /// difference can only over-estimate, and it is clamped to the capacity.
        handoff_report report(const state& st) const noexcept {
            const std::uint32_t tail = detail::load_acquire(st.tail);
            const std::uint32_t head = detail::load_acquire(st.head);
            return handoff_report{detail::load_relaxed(st.running) != 0, std::min(head - tail, static_cast<std::uint32_t>(Capacity)),
                                  detail::load_relaxed(st.underruns), detail::load_relaxed(st.overruns)};
        }
    };

    // ---- crossing threads without a lock: a mailbox ------------------------------------------

    /// One value, handed from one thread to one other. The newest value wins; an older one that
    /// was never collected is dropped. Neither side ever waits. It is how a new configuration
    /// reaches the thread that runs a chain, and how a reading leaves it.
    ///
    /// Three slots: the poster's, the taker's, and one in the middle. A post writes the poster's
    /// slot and swaps it with the middle one; a take swaps the taker's with the middle one when
    /// that holds something new. Each side only ever touches its own slot, so a value is never
    /// read while it is being written. A mailbox is an object with a constant constructor, not
    /// a state: it starts empty, a static one is in .bss, and reset() empties it again.
    ///
    /// A posted value owns everything it refers to: no pointer, span or view inside it. The
    /// compiler cannot check that; it is an assumption of use.
    template <typename T>
    class mailbox {
        static_assert(std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> &&
                          std::is_trivially_destructible_v<T>,
                      "catenae: a mailbox carries a trivially copyable value");
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "catenae: a mailbox needs a lock-free 32-bit atomic");

      public:
        /// The posting thread (exactly one). Never waits. The value is constructed in the
        /// poster's slot (assignment could not start the lifetime of a T with no trivial
        /// default constructor, which a design is).
        void post(const T& value) noexcept {
            ::new (static_cast<void*>(&m_slots[m_poster])) T(value);
            const std::uint32_t was = m_middle.exchange(to_middle(m_poster) | k_fresh, std::memory_order_acq_rel);
            m_poster                = from_middle(was);
        }

        /// Empties the mailbox. Only while neither side is running.
        void reset() noexcept {
            m_middle.store(0, std::memory_order_release);
            m_poster = 0;
            m_taker  = 0;
        }

        /// The taking thread (exactly one). If a value has arrived since the last take, calls
        /// apply(value) and returns true. Never waits.
        template <typename Apply>
        bool take(Apply apply) noexcept {
            if ((m_middle.load(std::memory_order_acquire) & k_fresh) == 0) {
                return false;
            }
            const std::uint32_t mine = before(m_taker); // the taker's slot
            const std::uint32_t was  = m_middle.exchange(to_middle(mine), std::memory_order_acq_rel);
            const std::uint32_t now  = from_middle(was);
            m_taker                  = after(now);
            apply(static_cast<const T&>(*std::launder(reinterpret_cast<const T*>(&m_slots[now]))));
            return true;
        }

      private:
        // Stored so that zero is the starting arrangement: poster in slot 0, middle 1, taker 2.
        // Slots are counted round a ring of three with a compare, not a division.
        static constexpr std::uint32_t k_fresh = 4;
        static constexpr std::uint32_t after(std::uint32_t slot) noexcept { return slot == 2U ? 0U : slot + 1U; }
        static constexpr std::uint32_t before(std::uint32_t slot) noexcept { return slot == 0U ? 2U : slot - 1U; }
        static constexpr std::uint32_t to_middle(std::uint32_t slot) noexcept { return before(slot); }
        static constexpr std::uint32_t from_middle(std::uint32_t stored) noexcept { return after(stored & 3U); }

        struct slot {
            alignas(T) unsigned char bytes[sizeof(T)];
        };

        std::array<slot, 3>        m_slots{};
        std::atomic<std::uint32_t> m_middle{0};  // a word of its own: it shares with neither side's index
        std::uint32_t              m_poster = 0; // touched by the posting thread only
        std::uint32_t              m_taker  = 0; // touched by the taking thread only
    };

    // ---- the pull side ------------------------------------------------------------------

    namespace detail {
        // The most input frames a chain can need to write k output frames, from any state:
        // between two states its output can swing by its shortfall and its lead together.
        template <typename Chain>
        constexpr std::size_t max_frames_needed(std::size_t k) noexcept {
            return ceiling((ratio{k, 1} + Chain::max_shortfall + Chain::max_lead) / Chain::rate_ratio);
        }
    } // namespace detail

    /// What the pull side remembers: the chain's state and the holdover that lets it deliver
    /// exactly the frames asked for. Plain storage; reset(chain, state) makes it ready.
    template <typename Chain>
    struct fed_by_memory {
        static constexpr std::size_t k_out_width = Chain::output_port::width;
        static constexpr std::size_t k_overshoot = Chain::max_outputs_for(1) - 1; // frames, at most

        state_of<Chain>                                                                      chain;
        std::array<typename Chain::output_port::value_type, (k_overshoot + 1) * k_out_width> holdover;
        std::size_t                                                                          held;    // frames in holdover
        std::size_t                                                                          starved; // input frames that were silence
    };
    template <typename Chain>
    using fed_by_state = fed_by_memory<std::remove_cv_t<Chain>>;

    template <typename Chain>
    void reset(const Chain& chain, fed_by_memory<Chain>& st) noexcept {
        reset_state(chain, st.chain);
        st.held    = 0;
        st.starved = 0;
    }

    /// What the pull side needs while a call runs: the chain's workspace and the staging for
    /// one pull. MaxRequest is the largest request, in output frames: a request makes at most
    /// one pull on the junction, of at most k_max_pull frames, so the staging holds everything
    /// that request can need.
    template <typename Chain, std::size_t MaxRequest>
    struct fed_by_scratch {
        static constexpr std::size_t k_in_width  = Chain::input_port::width;
        static constexpr std::size_t k_out_width = Chain::output_port::width;
        static constexpr std::size_t k_max_pull  = detail::max_frames_needed<Chain>(MaxRequest);

        workspace_of<Chain>                                                                                chain;
        std::array<typename Chain::input_port::value_type, k_max_pull * k_in_width>                        pulled;
        std::array<typename Chain::output_port::value_type, Chain::max_outputs_for(k_block) * k_out_width> made;
    };
    template <typename Chain, std::size_t MaxRequest>
    using fed_by_workspace = fed_by_scratch<std::remove_cv_t<Chain>, MaxRequest>;

    /// Fills `out` with exactly out.size() / width frames of the chain's output, pulling what
    /// the chain needs from the junction in AT MOST ONE pull: a junction may clock a servo or
    /// a fill policy from its pulls, so a request is never split into several. A request the
    /// holdover covers makes none. Returns the frames written, which is all of `out`;
    /// st.starved counts the input frames that were silence. The chain is a chain<>: wrap a
    /// lone stage with chain(stage). This is where SampleRateTap's pull(out, k) lands.
    template <typename Junction, typename Chain, std::size_t MaxRequest>
    std::size_t fed_by(const Junction& junction, state_of<Junction>& junction_state, const Chain& chain, fed_by_memory<Chain>& st,
                       fed_by_scratch<Chain, MaxRequest>& ws, span<typename Chain::output_port::value_type> out) noexcept {
        using scratch_t         = fed_by_scratch<Chain, MaxRequest>;
        static_assert(Junction::serves(scratch_t::k_max_pull, 1),
                      "catenae: the junction's target is below this request's largest pull plus its largest push less one");
        static_assert(is_plain<fed_by_memory<Chain>> && is_plain<scratch_t>, "catenae: the pull side's memory is plain storage");
        using sample_in         = typename Chain::input_port::value_type;
        using sample_out        = typename Chain::output_port::value_type;
        const std::size_t width = scratch_t::k_out_width;
        expects(out.size() % width == 0, 1);
        const std::size_t want = out.size() / width;
        expects(want <= MaxRequest, 8); // the boundary check on the request
        audits(Chain::is_reset(st.chain), 10);
        std::size_t done = std::min(st.held, want);
        std::copy(st.holdover.begin(), st.holdover.begin() + static_cast<std::ptrdiff_t>(done * width), out.begin());
        std::copy(st.holdover.begin() + static_cast<std::ptrdiff_t>(done * width),
                  st.holdover.begin() + static_cast<std::ptrdiff_t>(st.held * width), st.holdover.begin());
        st.held -= done;
        if (done == want) {
            return want;
        }
        const std::size_t n = chain.frames_needed(st.chain, want - done);
        audits(n <= scratch_t::k_max_pull, 9);
        const std::size_t real = junction.pull(junction_state, span<sample_in>(ws.pulled.data(), n * scratch_t::k_in_width));
        st.starved += n - real;
        // All but the last block's worth goes straight to `out`: n is the smallest count that
        // reaches the request, so fewer frames cannot overshoot it.
        const std::size_t last  = std::min(n, k_block);
        const std::size_t first = n - last;
        done += chain.run(st.chain, ws.chain, span<const sample_in>(ws.pulled.data(), first * scratch_t::k_in_width),
                          out.subspan(done * width));
        const std::size_t made = chain.run(
            st.chain, ws.chain, span<const sample_in>(ws.pulled.data() + first * scratch_t::k_in_width, last * scratch_t::k_in_width),
            span<sample_out>(ws.made));
        const std::size_t take = std::min(made, want - done);
        std::copy(ws.made.begin(), ws.made.begin() + static_cast<std::ptrdiff_t>(take * width),
                  out.begin() + static_cast<std::ptrdiff_t>(done * width));
        st.held = made - take; // the overshoot waits for the next call
        audits(st.held <= fed_by_memory<Chain>::k_overshoot, 9);
        std::copy(ws.made.begin() + static_cast<std::ptrdiff_t>(take * width),
                  ws.made.begin() + static_cast<std::ptrdiff_t>(made * width), st.holdover.begin());
        audits(done + take == want, 9);
        return want;
    }

} // namespace catenae

#endif // CATENAE_SKETCHES_CONFIG_STATE_SKETCH_H
