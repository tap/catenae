/// @file resample.h
/// @brief `resample`: one Nyquist stage at a compile-time L/M, its filter a generated design
/// (PLAN.md 3.3); `up2` and `down2` are its half-bands, pinned at each design divisor.
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<S, Ch>` in and out                                                     |
/// | Ratio      | L/M of `Ratio`, a `ratio_t<L, M>` in lowest terms, L and M of 2^a 3^b, L != M |
/// | Design     | `Design`, a `nyquist_design` of band max(L, M) (`nyquist_tables.h`): a type, |
/// |            | so the configuration is empty (D54) and the rows a constant of the type, in ROM |
/// | Machines   | L >= 2: the L-phase machine, output n dotting row (n M) mod L against the T = |
/// |            | ceil(N / L) newest inputs; L = 1: the M-branch commutator, every branch under |
/// |            | one rounding: y[n] = sum_k x[floor(n M / L) - k] h[(n M mod L) + k L], x[< 0] = 0 |
/// | Rows       | tap-reversed, trimmed to their nonzero span, built at compile time from the   |
/// |            | double prototype: going up the design's branches; a decimator h / M; a mixed  |
/// |            | ratio going down each phase at unity; Q15 and Q31 by DspTap's row-sum-       |
/// |            | preserving quantization, so every row sums to unity in the format exactly     |
/// | Q15 gains  | a decimator's branches each at unity and the 1/M in the one rounding; a mixed |
/// |            | ratio going down at a power-of-two gain G (G L / M < 2) shifted back out     |
/// | Sum        | double and fixed point: the traits' mac chain, rows in order, each oldest tap |
/// |            | first, one rounding (SampleRateTap's, bit for bit); float: four partial sums, |
/// |            | s_m over each row's taps t = m mod 4, then (s0 + s1) + (s2 + s3) (D36)        |
/// | State      | the history lines (one per channel, M for a decimator), each T + 64 frames, |
/// |            | where each ends, the schedule position, the pending gap, the decimator's phase |
/// | Reset      | each line a row of silence, the pre-advance pending (PLAN.md 12)             |
/// | Arrival    | input group M, no shortfall, a lead of (M - 1) / M, output quantum L when M = 1 |
/// |            | else 1; n frames from rest write ceil(n L / M)                               |
/// | Latency    | exactly (N - 1) / (2 M) output frames, a constant of the type (D39)          |
/// | Tail       | N - 1 frames decimating, T otherwise (SampleRateTap's window); no residue    |
/// | Pins       | `up2<Ch, Divisor>` and `down2<Ch, Divisor>`: SampleRateTap's economy (70 dB,  |
/// |            | the passband 3/8 of the chain's lowest rate) at divisors 1 and 2, 39 and 19 taps |
/// | Formats    | float, double, q15, q31, Q4.27 (Q31's arithmetic on the same words)           |
/// | Method     | SampleRateTap's rational/stage.h at 4496888 (NOTICE.md): the schedule, both   |
/// |            | machines, build_table and its gains, as a stage with a plain state           |
/// | Rationale  | D18, D35, D36, D37, D39, D47, D54, PLAN.md 3.3, 12                            |
///
/// @req{CAT-RESAMPLE-001} Each machine is the formula its card writes: in double bit for bit
/// against the reference summed in the card's order, every ratio of the battery, every channel
/// alike; float within its stated floor of double; and the same bits for any chunking.
/// @req{CAT-RESAMPLE-002} The rows are the card's: going up the prototype's branches, a
/// decimator's h / M, a mixed ratio's phases at unity; in Q15 and Q31 every row sums to its
/// gain in the format exactly, and full-scale DC comes out at full scale.
/// @req{CAT-RESAMPLE-003} The arrival numbers, the counts from every state and from rest, the
/// exact latency (N - 1) / (2 M) and the tail are the card's, and an impulse's peak lands on the
/// latency.
/// @req{CAT-RESAMPLE-004} Only the charter's ratios, a design of the ratio's band and a pinned
/// divisor compile; each refusal names its reason.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_RESAMPLE_H
#define CATENAE_RESAMPLE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/nyquist_tables.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/sample_traits.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// Whether n is 2^a 3^b.
            constexpr bool is_two_three(std::uint32_t n) noexcept {
                if (n == 0) {
                    return false;
                }
                while (n % 2 == 0) {
                    n /= 2;
                }
                while (n % 3 == 0) {
                    n /= 3;
                }
                return n == 1;
            }

            /// A resample's ratio, checked against the charter, and its band.
            template <typename Ratio>
            struct resample_ratio {
                static_assert(std::is_same<Ratio, typename Ratio::type>::value,
                              "catenae: a resample's ratio is a ratio_t in lowest terms");
                static_assert(is_two_three(Ratio::up) && is_two_three(Ratio::down),
                              "catenae: a resample's L and M are each of the form 2^a 3^b");
                static_assert(Ratio::up != Ratio::down, "catenae: a resample changes the rate: L != M");
                static constexpr std::size_t k_up           = Ratio::up;
                static constexpr std::size_t k_down         = Ratio::down;
                static constexpr std::size_t k_band         = k_up > k_down ? k_up : k_down;
                static constexpr bool        k_is_up        = k_up > k_down;
                static constexpr bool        k_is_decimator = k_up == 1;
            };

            /// Whether two doubles are one value, compared without ==.
            constexpr bool same_double(double a, double b) noexcept {
                return !(a < b) && !(a > b);
            }

            /// Whether a design has the Nyquist structure the machines rely on: N = 2 m B - 1, the
            /// centre tap exactly 1, every B-th tap from it exactly 0, symmetric bit for bit.
            template <typename Design>
            constexpr bool is_nyquist_table() noexcept {
                constexpr std::size_t n = Design::taps;
                constexpr std::size_t b = Design::band;
                constexpr std::size_t c = (n - 1) / 2;
                if (n != 2 * Design::taps_per_branch * b - 1 || Design::prototype.size() != n) {
                    return false;
                }
                bool ok = same_double(Design::prototype[c], 1.0);
                for (std::size_t i = 0; i < n; ++i) {
                    const bool structural = i != c && (i + b - c % b) % b == 0;
                    ok                    = ok && (!structural || same_double(Design::prototype[i], 0.0))
                         && same_double(Design::prototype[i], Design::prototype[n - 1 - i]);
                }
                return ok;
            }

            /// The geometry and gains of a stage's table.
            template <typename S, typename Ratio, typename Design>
            struct resample_shape {
                using ratio_traits                     = resample_ratio<Ratio>;
                using value_type                       = storage_t<S>;
                using traits                           = sample_traits<value_type>;
                using coeff                            = typename traits::coeff;
                static constexpr std::size_t k_up      = ratio_traits::k_up;
                static constexpr std::size_t k_down    = ratio_traits::k_down;
                static constexpr bool        k_is_up   = ratio_traits::k_is_up;
                static constexpr bool        k_is_dec  = ratio_traits::k_is_decimator;
                static constexpr std::size_t k_taps    = Design::taps;
                static constexpr std::size_t k_rows    = k_is_dec ? k_down : k_up;
                static constexpr std::size_t k_row_len = (k_taps + k_rows - 1) / k_rows;
                static constexpr bool        k_is_q15  = std::is_same<value_type, std::int16_t>::value;
                /// A Q15 decimator: each branch quantized at unity, the 1/M in the rounding.
                static constexpr bool k_branch_quantized = k_is_dec && k_is_q15;
                /// A Q15 mixed ratio going down: its rows at a power-of-two gain.
                static constexpr bool        k_row_scaled      = !k_is_up && !k_is_dec && k_is_q15;
                static constexpr std::size_t k_mixed_down_gain = [] {
                    std::size_t g = 1;
                    while (2 * g * k_up < 2 * k_down) {
                        g *= 2;
                    }
                    return g;
                }();
                /// What the rows sum to in units of the format's unity, divided out in the rounding.
                static constexpr std::size_t k_table_gain =
                    k_branch_quantized ? k_down : (k_row_scaled ? k_mixed_down_gain : 1);
            };

            /// DspTap's correction: `residual` steps of one, each to the tap of the largest
            /// remainder that can still move that way (the lowest index on a tie), none past a rail.
            template <typename C, std::size_t N>
            constexpr void spread_residual(std::array<double, N>& remainder, std::array<C, N>& dst, std::size_t n,
                                           std::int64_t residual) noexcept {
                while (residual != 0) {
                    const double sgn  = residual > 0 ? 1.0 : -1.0;
                    const C      rail = residual > 0 ? std::numeric_limits<C>::max() : std::numeric_limits<C>::min();
                    std::size_t  best = n;
                    for (std::size_t u = 0; u < n; ++u) {
                        if (dst[u] != rail && (best == n || sgn * remainder[u] > sgn * remainder[best])) {
                            best = u;
                        }
                    }
                    if (best == n) {
                        return; // every tap at this rail
                    }
                    const std::int64_t step = residual > 0 ? 1 : -1;
                    dst[best]               = narrow<C>(dst[best] + step);
                    remainder[best] -= sgn;
                    residual -= step;
                }
            }

            /// DspTap's quantize_row_preserving_sum at b26a7db, constexpr over a prefix of n of an
            /// array: largest remainder, the lowest index on a tie, never a step past a rail; the
            /// floating formats convert.
            template <typename T, std::size_t N>
            constexpr void quantize_row(const std::array<double, N>& src, std::size_t n,
                                        std::array<typename sample_traits<T>::coeff, N>& dst) noexcept {
                using traits = sample_traits<T>;
                if constexpr (!traits::is_fixed_point) {
                    for (std::size_t t = 0; t < n; ++t) {
                        dst[t] = traits::make_coeff(src[t]);
                    }
                }
                else {
                    std::array<double, N> remainder{};
                    double                exact_sum = 0.0;
                    std::int64_t          quant_sum = 0;
                    for (std::size_t t = 0; t < n; ++t) {
                        const double scaled = src[t] * traits::coeff_scale;
                        dst[t]              = traits::make_coeff(src[t]);
                        remainder[t]        = scaled - static_cast<double>(dst[t]);
                        exact_sum += scaled;
                        quant_sum += dst[t];
                    }
                    spread_residual(remainder, dst, n, round_sat<std::int64_t>(exact_sum) - quant_sum);
                }
            }

            /// A stage's rows, k_rows of k_row_len, tap-reversed, and each row's nonzero span.
            template <typename S, typename Ratio, typename Design>
            struct resample_table {
                using shape = resample_shape<S, Ratio, Design>;
                std::array<typename shape::coeff, shape::k_rows * shape::k_row_len> rows;  ///< oldest tap first
                std::array<std::uint32_t, shape::k_rows>                            first; ///< span start
                std::array<std::uint32_t, shape::k_rows>                            count; ///< span length
            };

            /// Whether a coefficient is zero, compared without == for a float.
            template <typename C>
            constexpr bool is_zero_coeff(C c) noexcept {
                return !(c < C{0}) && !(c > C{0});
            }

            /// A decimator's rows: h / M quantized as one row (in Q15 each branch at unity), then
            /// scattered to the M branches, each right-aligned to the newest sample.
            template <typename S, typename Ratio, typename Design>
            constexpr void decimator_rows(resample_table<S, Ratio, Design>& t) noexcept {
                using shape               = resample_shape<S, Ratio, Design>;
                using value_type          = typename shape::value_type;
                using coeff               = typename shape::coeff;
                constexpr std::size_t n   = shape::k_taps;
                constexpr std::size_t len = shape::k_row_len;
                constexpr std::size_t m   = shape::k_down;
                const auto&           h   = Design::prototype;
                std::array<coeff, n>  q{};
                if constexpr (shape::k_branch_quantized) {
                    for (std::size_t j = 0; j < m; ++j) {
                        const std::size_t     taps = (n - j + m - 1) / m;
                        std::array<double, n> branch{};
                        std::array<coeff, n>  qb{};
                        for (std::size_t s = 0; s < taps; ++s) {
                            branch[s] = h[j + s * m];
                        }
                        quantize_row<value_type, n>(branch, taps, qb);
                        for (std::size_t s = 0; s < taps; ++s) {
                            q[j + s * m] = qb[s];
                        }
                    }
                }
                else {
                    std::array<double, n> scaled{};
                    for (std::size_t i = 0; i < n; ++i) {
                        scaled[i] = h[i] / static_cast<double>(m);
                    }
                    quantize_row<value_type, n>(scaled, n, q);
                }
                for (std::size_t j = 0; j < m; ++j) {
                    const std::size_t taps = (n - j + m - 1) / m;
                    for (std::size_t s = 0; s < taps; ++s) {
                        t.rows[j * len + (len - 1 - s)] = q[j + s * m];
                    }
                }
            }

            /// The L-phase machine's rows: phase p is taps p, p + L, ..., tap-reversed; going down
            /// each is normalised to its gain.
            template <typename S, typename Ratio, typename Design>
            constexpr void phase_rows(resample_table<S, Ratio, Design>& t) noexcept {
                using shape               = resample_shape<S, Ratio, Design>;
                using value_type          = typename shape::value_type;
                using coeff               = typename shape::coeff;
                constexpr std::size_t n   = shape::k_taps;
                constexpr std::size_t len = shape::k_row_len;
                constexpr std::size_t l   = shape::k_up;
                const auto&           h   = Design::prototype;
                for (std::size_t p = 0; p < l; ++p) {
                    std::array<double, len> row{};
                    std::array<coeff, len>  q{};
                    double                  sum = 0.0;
                    for (std::size_t k = 0; k < len; ++k) {
                        const std::size_t i = p + k * l;
                        const double      v = i < n ? h[i] : 0.0;
                        row[len - 1 - k]    = v;
                        sum += v;
                    }
                    if constexpr (!shape::k_is_up) {
                        for (std::size_t k = 0; k < len; ++k) {
                            row[k] = row[k] / sum * static_cast<double>(shape::k_table_gain);
                        }
                    }
                    quantize_row<value_type, len>(row, len, q);
                    for (std::size_t k = 0; k < len; ++k) {
                        t.rows[p * len + k] = q[k];
                    }
                }
            }

            /// SampleRateTap's build_table, at compile time (the card's Rows and Q15 gains), each
            /// row then trimmed to its nonzero span.
            template <typename S, typename Ratio, typename Design>
            constexpr resample_table<S, Ratio, Design> make_resample_table() noexcept {
                using shape                          = resample_shape<S, Ratio, Design>;
                constexpr std::size_t            len = shape::k_row_len;
                resample_table<S, Ratio, Design> t{};
                if constexpr (shape::k_is_dec) {
                    decimator_rows(t);
                }
                else {
                    phase_rows(t);
                }
                for (std::size_t r = 0; r < shape::k_rows; ++r) {
                    std::size_t first = 0;
                    while (first < len && is_zero_coeff(t.rows[r * len + first])) {
                        ++first;
                    }
                    std::size_t last = len;
                    while (last > first && is_zero_coeff(t.rows[r * len + last - 1])) {
                        --last;
                    }
                    t.first[r] = static_cast<std::uint32_t>(first);
                    t.count[r] = static_cast<std::uint32_t>(last - first);
                }
                return t;
            }

            /// A stage's rows, a constant of its type: in ROM once per format, ratio and design.
            template <typename S, typename Ratio, typename Design>
            struct resample_rows {
                static constexpr resample_table<S, Ratio, Design> k_value = make_resample_table<S, Ratio, Design>();
            };

            /// One output step of the L-phase machine: the row to dot and the inputs after it.
            struct resample_step {
                std::uint32_t phase;   ///< the row, in [0, L)
                std::uint32_t advance; ///< input frames consumed after this output
            };

            /// The superblock of L outputs: phase(n) = n M mod L, the advances summing to M.
            template <std::size_t L, std::size_t M>
            struct resample_schedule {
                static constexpr std::array<resample_step, L> make() noexcept {
                    std::array<resample_step, L> s{};
                    for (std::size_t n = 0; n < L; ++n) {
                        s[n].phase   = static_cast<std::uint32_t>((n * M) % L);
                        s[n].advance = static_cast<std::uint32_t>(((n + 1) * M) / L - (n * M) / L);
                    }
                    return s;
                }
                static constexpr std::array<resample_step, L> k_steps = make();
            };

            /// acc and the tap-order mac chain of a row against a window (SampleRateTap's
            /// accumulate_row): double and the fixed formats.
            template <typename T>
            inline typename sample_traits<T>::accum row_mac(typename sample_traits<T>::accum        acc,
                                                            const typename sample_traits<T>::coeff* row, const T* w,
                                                            std::size_t n) noexcept {
                for (std::size_t t = 0; t < n; ++t) {
                    acc = sample_traits<T>::mac(acc, w[t], row[t]);
                }
                return acc;
            }

            /// A row against a window into four partial sums, s_m over the taps t = m mod 4: float.
            inline void row_partial(float (&s)[4], const float* row, const float* w, std::size_t n) noexcept {
                std::size_t t = 0;
                for (; t + 4 <= n; t += 4) {
                    s[0] += row[t] * w[t];
                    s[1] += row[t + 1] * w[t + 1];
                    s[2] += row[t + 2] * w[t + 2];
                    s[3] += row[t + 3] * w[t + 3];
                }
                for (std::size_t m = 0; t < n; ++t, ++m) {
                    s[m] += row[t] * w[t];
                }
            }

        } // namespace detail

        /// @brief One Nyquist stage at the ratio `Ratio` over a `port<S, Ch>`, its filter `Design`.
        template <typename S, std::size_t Ch, typename Ratio, typename Design>
        class basic_resample {
            static_assert(Ch >= 1, "catenae: a resample has at least one channel");
            static_assert(std::is_same<S, float>::value || std::is_same<S, double>::value || std::is_same<S, q15>::value
                              || std::is_same<S, q31>::value || std::is_same<S, q4_27>::value,
                          "catenae: a resample is float, double, q15, q31 or q4_27");
            using ratio_traits = detail::resample_ratio<Ratio>;
            static_assert(Design::band == ratio_traits::k_band,
                          "catenae: a resample's design is the max(L, M)-th-band filter of its ratio");
            static_assert(detail::is_nyquist_table<Design>(),
                          "catenae: a resample's design has the Nyquist structure: N = 2 m B - 1, the centre 1, "
                          "every B-th tap from it 0, symmetric");
            using shape                                 = detail::resample_shape<S, Ratio, Design>;
            using value_type                            = typename shape::value_type;
            using traits                                = typename shape::traits;
            using coeff                                 = typename shape::coeff;
            using rows                                  = detail::resample_rows<S, Ratio, Design>;
            static constexpr std::size_t k_up           = shape::k_up;
            static constexpr std::size_t k_down         = shape::k_down;
            static constexpr bool        k_is_decimator = shape::k_is_dec;
            static constexpr std::size_t k_rows         = shape::k_rows;
            static constexpr std::size_t k_row_len      = shape::k_row_len;
            static constexpr std::size_t k_lines        = k_is_decimator ? k_down : 1; ///< lines a channel
            static constexpr std::size_t k_slack        = 64; ///< appends between compactions (SampleRateTap's)
            static constexpr std::size_t k_cap          = k_row_len + k_slack;

          public:
            using input_port  = port<S, Ch>; ///< what it reads
            using output_port = port<S, Ch>; ///< what it writes
            using design      = Design;      ///< the filter: a type, its rows a constant

            static constexpr ratio       rate_ratio  = Ratio::value; ///< L/M
            static constexpr std::size_t input_group = k_down;       ///< M frames write exactly L
            /// Never behind: a group writes when its first frame arrives (PLAN.md 12).
            static constexpr ratio max_shortfall{0, 1};
            /// Up to (M - 1)/M ahead: one frame in from rest writes one out.
            static constexpr ratio max_lead{static_cast<std::uint32_t>(k_down - 1), static_cast<std::uint32_t>(k_down)};
            /// An interpolator writes L a frame; every other stage single frames.
            static constexpr std::size_t output_quantum    = k_down == 1 ? k_up : 1;
            static constexpr bool        is_in_place       = false;
            static constexpr bool        has_exact_latency = true;
            /// (N - 1)/2 samples at the composite rate: (N - 1)/(2 M) output frames.
            static constexpr ratio exact_latency =
                reduce(ratio{static_cast<std::uint32_t>((Design::taps - 1) / 2), static_cast<std::uint32_t>(k_down)});

            /// Everything one instance remembers. Channel c's sub-line `sub` is
            /// hist[(c k_lines + sub) cap ...], planar inside and interleaved at the ports.
            struct state {
                std::array<value_type, Ch * k_lines * k_cap> hist;    ///< the history lines
                std::array<std::uint32_t, k_lines>           end;     ///< newest sample + 1, per sub-line
                std::uint32_t                                pos;     ///< superblock position in [0, L)
                std::uint32_t                                pending; ///< inputs before the next output
                std::uint32_t                                dphase;  ///< inputs since the last output (decimator)
            };
            using workspace = no_workspace; ///< nothing

            /// Each line a row of silence and the pre-advance pending: the next frame writes.
            void reset(state& st) const noexcept {
                st.hist.fill(traits::silence());
                st.end.fill(static_cast<std::uint32_t>(k_row_len));
                st.pos     = 0;
                st.pending = 1;
                st.dphase  = 0;
            }

            /// Consumes every input frame and writes outputs_for(frames) frames.
            std::size_t run(state& st, workspace&, span<const value_type> in, span<value_type> out) const noexcept {
                const std::size_t frames = in.size() / Ch;
                if constexpr (detail::k_audit_level) {
                    detail::check_audit(out.size() >= outputs_for(st, frames) * Ch, check::index_out_of_range);
                    detail::check_audit(st.pos < k_up && st.dphase < k_down, check::invariant);
                }
                std::size_t       produced = 0;
                const value_type* x        = in.data();
                value_type*       y        = out.data();
                if constexpr (k_is_decimator) {
                    for (std::size_t i = 0; i < frames; ++i) {
                        append(st, x + i * Ch);
                        if (st.dphase == 0) {
                            emit(st, y + produced * Ch);
                            ++produced;
                        }
                        st.dphase = st.dphase + 1 == k_down ? 0 : st.dphase + 1;
                    }
                }
                else {
                    const std::size_t total = outputs_for(st, frames);
                    std::size_t       fed   = 0;
                    for (; produced < total; ++produced) {
                        while (st.pending != 0) { // never outruns the input: total is exact
                            append(st, x + fed * Ch);
                            ++fed;
                            --st.pending;
                        }
                        emit(st, y + produced * Ch);
                    }
                    while (fed < frames) { // what is left is fewer than the next gap
                        append(st, x + fed * Ch);
                        ++fed;
                        --st.pending;
                    }
                }
                return produced;
            }

            /// Exactly what the next n frames write, from the schedule position or the phase.
            static constexpr std::size_t outputs_for(const state& st, std::size_t n) noexcept {
                if constexpr (k_is_decimator) {
                    const std::size_t first = 1 + (k_down - st.dphase) % k_down;
                    return n < first ? 0 : 1 + (n - first) / k_down;
                }
                else {
                    if (n < st.pending) {
                        return 0;
                    }
                    const std::size_t base = st.pos * k_down / k_up;
                    const std::size_t a    = n - st.pending;
                    return (k_up * (base + a + 1) - 1) / k_down - st.pos + 1;
                }
            }
            /// The smallest n with outputs_for(n) >= k.
            static constexpr std::size_t frames_needed(const state& st, std::size_t k) noexcept {
                if (k == 0) {
                    return 0;
                }
                if constexpr (k_is_decimator) {
                    return 1 + (k_down - st.dphase) % k_down + (k - 1) * k_down;
                }
                else {
                    const std::size_t p = st.pos;
                    return st.pending + (p + k - 1) * k_down / k_up - p * k_down / k_up;
                }
            }
            /// ceil(n L / M): from any state, n frames write no more.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept {
                return (n * k_up + k_down - 1) / k_down;
            }
            /// From rest the first frame of a group writes: ceil(n L / M).
            static constexpr std::size_t outputs_from_rest(std::size_t n) noexcept { return max_outputs_for(n); }
            /// The exact latency, no deviation.
            stage_latency latency() const noexcept {
                return {static_cast<float>(exact_latency.up) / static_cast<float>(exact_latency.down), 0.0f};
            }
            /// The longest history a row reads: N - 1 decimating, T otherwise.
            std::size_t tail_frames() const noexcept { return k_is_decimator ? Design::taps - 1 : k_row_len; }
            /// The same: an FIR's output is silence after it.
            std::size_t practical_tail_frames() const noexcept { return tail_frames(); }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }

            /// Row r's tap t in storage order (t = 0 the oldest sample's); trimmed taps read 0.
            static constexpr coeff coefficient(std::size_t r, std::size_t t) noexcept {
                return rows::k_value.rows[r * k_row_len + t];
            }
            /// T, taps a row.
            static constexpr std::size_t row_length() noexcept { return k_row_len; }
            /// What the rows sum to, in units of the format's unity (the card's Q15 gains).
            static constexpr std::size_t table_gain() noexcept { return shape::k_table_gain; }
            /// The multiplies an output costs, over a superblock of L outputs.
            static constexpr std::size_t macs_per_superblock() noexcept {
                std::size_t total = 0;
                for (std::size_t r = 0; r < k_rows; ++r) {
                    total += rows::k_value.count[r];
                }
                return total;
            }

          private:
            static value_type* line(state& st, std::size_t c, std::size_t sub) noexcept {
                return st.hist.data() + (c * k_lines + sub) * k_cap;
            }
            static const value_type* line(const state& st, std::size_t c, std::size_t sub) noexcept {
                return st.hist.data() + (c * k_lines + sub) * k_cap;
            }

            /// Slides the newest T - 1 samples of sub-line sub to the front of every channel's.
            static void compact(state& st, std::size_t sub) noexcept {
                constexpr std::size_t keep = k_row_len - 1;
                for (std::size_t c = 0; c < Ch; ++c) {
                    value_type* l = line(st, c, sub);
                    std::memmove(l, l + (st.end[sub] - keep), keep * sizeof(value_type));
                }
                st.end[sub] = static_cast<std::uint32_t>(keep);
            }

            /// Appends one interleaved frame to the history (the decimator's current sub-line).
            static void append(state& st, const value_type* frame) noexcept {
                const std::size_t sub = k_is_decimator ? st.dphase : 0;
                if (st.end[sub] == k_cap) {
                    compact(st, sub);
                }
                for (std::size_t c = 0; c < Ch; ++c) {
                    line(st, c, sub)[st.end[sub]] = frame[c];
                }
                ++st.end[sub];
            }

            /// The one rounding: the traits' finalize, or finalize_divided by the table's gain.
            static value_type finalize(typename traits::accum acc) noexcept {
                if constexpr (shape::k_table_gain != 1) {
                    return finalize_divided<value_type, static_cast<std::uint32_t>(shape::k_table_gain)>(acc);
                }
                else {
                    return traits::finalize(acc);
                }
            }

            /// One output frame at the current position; advances the schedule (the decimator's
            /// phase advances in run).
            static void emit(state& st, value_type* out) noexcept {
                for (std::size_t c = 0; c < Ch; ++c) {
                    if constexpr (std::is_same<value_type, float>::value) {
                        float s[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        for_rows(st, c, [&s](const coeff* row, const value_type* w, std::size_t n) noexcept {
                            detail::row_partial(s, row, w, n);
                        });
                        out[c] = (s[0] + s[1]) + (s[2] + s[3]);
                    }
                    else {
                        typename traits::accum acc{};
                        for_rows(st, c, [&acc](const coeff* row, const value_type* w, std::size_t n) noexcept {
                            acc = detail::row_mac<value_type>(acc, row, w, n);
                        });
                        out[c] = finalize(acc);
                    }
                }
                if constexpr (!k_is_decimator) {
                    const detail::resample_step step = detail::resample_schedule<k_up, k_down>::k_steps[st.pos];
                    st.pending                       = step.advance;
                    st.pos                           = st.pos + 1 == k_up ? 0 : st.pos + 1;
                }
            }

            /// Each row this output dots, in order, with its window: every branch of a decimator,
            /// the scheduled phase of an L-phase machine.
            template <typename F>
            static void for_rows(const state& st, std::size_t c, F&& f) noexcept {
                const auto& table = rows::k_value;
                if constexpr (k_is_decimator) {
                    for (std::size_t j = 0; j < k_rows; ++j) {
                        const std::size_t sub = (k_down - j) % k_down;
                        const value_type* w   = line(st, c, sub) + st.end[sub] - k_row_len + table.first[j];
                        f(table.rows.data() + j * k_row_len + table.first[j], w, table.count[j]);
                    }
                }
                else {
                    const std::size_t phase = detail::resample_schedule<k_up, k_down>::k_steps[st.pos].phase;
                    const value_type* w     = line(st, c, 0) + st.end[0] - k_row_len + table.first[phase];
                    f(table.rows.data() + phase * k_row_len + table.first[phase], w, table.count[phase]);
                }
            }
        };

        namespace detail {
            template <std::uint32_t Divisor>
            inline constexpr bool k_no_half_band_pin = false;
        } // namespace detail

        /// The half-band an up2 or down2 at design divisor Divisor runs: SampleRateTap's economy
        /// pins (rational/design.h, k_economy_relaxations, band 2), the passband 3/8 of the chain's
        /// lowest rate at 70 dB. Divisors 1 and 2 now; the rest arrive with C7's profiles.
        template <std::uint32_t Divisor>
        struct half_band_pin {
            static_assert(detail::k_no_half_band_pin<Divisor>,
                          "catenae: no half-band is pinned at this design divisor: 1 and 2 are, the rest arrive with "
                          "the profiles at C7");
        };
        /// At divisor 1: m = 10, 39 taps.
        template <>
        struct half_band_pin<1> {
            using type = nyquist_design<2, 10, 70>; ///< the design
        };
        /// At divisor 2, relaxed: m = 5, 19 taps.
        template <>
        struct half_band_pin<2> {
            using type = nyquist_design<2, 5, 70>; ///< the design
        };
        /// The pinned half-band at a design divisor.
        template <std::uint32_t Divisor>
        using half_band = typename half_band_pin<Divisor>::type;

        template <std::size_t Ch, typename Ratio, typename Design>
        using resample = basic_resample<float, Ch, Ratio, Design>; ///< in float (D24)
        template <std::size_t Ch, typename Ratio, typename Design>
        using resample_f64 = basic_resample<double, Ch, Ratio, Design>; ///< in double
        template <std::size_t Ch, typename Ratio, typename Design>
        using resample_q15 = basic_resample<q15, Ch, Ratio, Design>; ///< in Q15
        template <std::size_t Ch, typename Ratio, typename Design>
        using resample_q31 = basic_resample<q31, Ch, Ratio, Design>; ///< in Q31
        template <std::size_t Ch, typename Ratio, typename Design>
        using resample_q4_27 = basic_resample<q4_27, Ch, Ratio, Design>; ///< in Q4.27

        template <typename S, std::size_t Ch, std::uint32_t Divisor = 1>
        using basic_up2 = basic_resample<S, Ch, ratio_t<2, 1>, half_band<Divisor>>; ///< up by 2
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using up2 = basic_up2<float, Ch, Divisor>; ///< up by 2, in float
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using up2_f64 = basic_up2<double, Ch, Divisor>; ///< up by 2, in double
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using up2_q15 = basic_up2<q15, Ch, Divisor>; ///< up by 2, in Q15
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using up2_q31 = basic_up2<q31, Ch, Divisor>; ///< up by 2, in Q31
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using up2_q4_27 = basic_up2<q4_27, Ch, Divisor>; ///< up by 2, in Q4.27

        template <typename S, std::size_t Ch, std::uint32_t Divisor = 1>
        using basic_down2 = basic_resample<S, Ch, ratio_t<1, 2>, half_band<Divisor>>; ///< down by 2
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using down2 = basic_down2<float, Ch, Divisor>; ///< down by 2, in float
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using down2_f64 = basic_down2<double, Ch, Divisor>; ///< down by 2, in double
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using down2_q15 = basic_down2<q15, Ch, Divisor>; ///< down by 2, in Q15
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using down2_q31 = basic_down2<q31, Ch, Divisor>; ///< down by 2, in Q31
        template <std::size_t Ch, std::uint32_t Divisor = 1>
        using down2_q4_27 = basic_down2<q4_27, Ch, Divisor>; ///< down by 2, in Q4.27

        template <typename S, std::size_t Ch, typename Ratio, typename Design>
        struct is_stage<basic_resample<S, Ch, Ratio, Design>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_RESAMPLE_H
