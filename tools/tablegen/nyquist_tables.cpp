/// @file nyquist_tables.cpp
/// @brief The table generator (PLAN.md 3.3, 12): writes the Nyquist designs resample runs as
/// constants, so that no image designs at run time and none links the designer's double.
// Copyright 2026 Timothy Place and the catenae contributors
//
//   catenae_tablegen LIBRARY_HEADER TEST_HEADER
//
// writes include/catenae/nyquist_tables.h (the pinned designs, the primary template and each
// specialisation) and tests/support/catenae_test/nyquist_test_tables.h (the battery's small
// designs at other bands). Each tap is printed in hexadecimal, so every bit of the double is
// written. CTest runs it on the hosted Linux legs and fails when either file is not what it
// writes (tablegen.TablesAreTheGeneratorsOutput); another libm may round the sine otherwise.
//
// The designer is DspTap's design_nyquist (include/tap/dsp/nyquist.h at b26a7db, over kaiser.h's
// bessel_i0, kaiser_beta and sinc; NOTICE.md), C++17 here, with SampleRateTap's design_beta
// (rational/include/tap/sr/rational/design.h at 4496888): Kaiser's beta for the attenuation plus
// 4 dB. Built with contraction off, as the battery is, so the arithmetic is the source's.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

    constexpr double k_pi = 3.141592653589793;

    /// A design the generator writes: its band, its taps per branch, its attenuation in dB.
    struct key {
        std::size_t band;
        std::size_t taps_per_branch;
        unsigned    attenuation_db;
        const char* why; ///< the comment above it
    };

    /// The designs resample's pins name (catenae/resample.h): economy's half-bands at the design
    /// divisors 1 and 2 (SampleRateTap's k_economy_relaxations, band 2: m = 10 and 5).
    const std::vector<key> k_library = {
        {2, 10, 70, "economy's half-band at design divisor 1 (SampleRateTap's pin, m = 10)"},
        {2, 5, 70, "economy's half-band at design divisor 2, relaxed (SampleRateTap's pin, m = 5)"},
    };

    /// The battery's: short designs at each band the vocabulary has, for the machine's other ratios.
    const std::vector<key> k_tests = {
        {2, 2, 70, "a short half-band"},  {3, 2, 70, "a short third-band"},  {4, 2, 70, "a short fourth-band"},
        {6, 2, 70, "a short sixth-band"}, {8, 2, 70, "a short eighth-band"},
    };

    // DspTap's kaiser.h: the window and its parameter (Kaiser 1974), and the sinc.

    double bessel_i0(double x) {
        const double half_x = 0.5 * x;
        double       term   = 1.0;
        double       sum    = 1.0;
        for (int k = 1; k < 1000; ++k) {
            const double r = half_x / static_cast<double>(k);
            term *= r * r;
            sum += term;
            if (term < 1e-21 * sum) {
                break;
            }
        }
        return sum;
    }

    double kaiser_beta(double atten_db) {
        if (atten_db > 50.0) {
            return 0.1102 * (atten_db - 8.7);
        }
        if (atten_db > 21.0) {
            return 0.5842 * std::pow(atten_db - 21.0, 0.4) + 0.07886 * (atten_db - 21.0);
        }
        return 0.0;
    }

    double sinc(double x) {
        if (std::abs(x) < 1e-12) {
            return 1.0;
        }
        const double px = k_pi * x;
        return std::sin(px) / px;
    }

    /// DspTap's design_nyquist: the L-th-band Kaiser-windowed sinc, N = 2 m L - 1 taps, its centre
    /// exactly 1, every L-th tap from it exactly 0, each branch at DC gain 1, the halves one copy.
    std::vector<double> design_nyquist(std::size_t num_phases, std::size_t m, double beta) {
        const std::size_t   n = 2 * m * num_phases - 1;
        std::vector<double> h(n);
        const std::size_t   c = (n - 1) / 2;
        if (c == 0) {
            h[0] = 1.0;
            return h;
        }
        const double i0_beta = bessel_i0(beta);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t mirror = n - 1 - i;
            if (mirror < i) {
                h[i] = h[mirror];
                continue;
            }
            const double u = (static_cast<double>(i) - static_cast<double>(c)) / static_cast<double>(c);
            h[i]           = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - u * u)));
        }
        const double inv_l = 1.0 / static_cast<double>(num_phases);
        for (std::size_t i = 0; i < n; ++i) {
            if (i == c) {
                h[i] = 1.0;
            }
            else if ((i + num_phases - (c % num_phases)) % num_phases == 0) {
                h[i] = 0.0;
            }
            else {
                const double t = (static_cast<double>(i) - static_cast<double>(c)) * inv_l;
                h[i]           = inv_l * sinc(t) * (h[i] / i0_beta);
            }
        }
        for (std::size_t j = 0; j < num_phases; ++j) {
            if (j == c % num_phases) {
                continue;
            }
            double sum = 0.0;
            for (std::size_t i = j; i < n; i += num_phases) {
                sum += h[i];
            }
            const double gain = 1.0 / sum;
            for (std::size_t i = j; i < n; i += num_phases) {
                h[i] *= gain;
            }
        }
        for (std::size_t i = c + 1; i < n; ++i) {
            h[i] = h[n - 1 - i];
        }
        return h;
    }

    /// One specialisation, as clang-format writes it.
    std::string specialisation(const key& k) {
        const double              beta = kaiser_beta(static_cast<double>(k.attenuation_db) + 4.0);
        const std::vector<double> h    = design_nyquist(k.band, k.taps_per_branch, beta);
        std::string               s;
        char                      line[160];
        std::snprintf(line, sizeof line, "        /// %s: %zu taps.\n", k.why, h.size());
        s += line;
        s += "        template <>\n";
        std::snprintf(line, sizeof line, "        struct nyquist_design<%zu, %zu, %u> {\n", k.band, k.taps_per_branch,
                      k.attenuation_db);
        s += line;
        s += "            /// The band B: the centre tap is exactly 1 and every B-th tap from it exactly 0.\n";
        std::snprintf(line, sizeof line, "            static constexpr std::size_t band = %zu;\n", k.band);
        s += line;
        s += "            /// m, the taps of each of the B branches: N = 2 m B - 1.\n";
        std::snprintf(line, sizeof line, "            static constexpr std::size_t taps_per_branch = %zu;\n",
                      k.taps_per_branch);
        s += line;
        s += "            /// N, the prototype's length.\n";
        std::snprintf(line, sizeof line, "            static constexpr std::size_t taps = %zu;\n", h.size());
        s += line;
        s += "            /// The stopband attenuation the design was made for, in dB.\n";
        std::snprintf(line, sizeof line, "            static constexpr unsigned attenuation_db = %u;\n",
                      k.attenuation_db);
        s += line;
        std::snprintf(line, sizeof line, "            /// h[0] to h[%zu], in double.\n", h.size() - 1);
        s += line;
        std::snprintf(line, sizeof line, "            static constexpr std::array<double, %zu> prototype = {{\n",
                      h.size());
        s += line;
        // One tap a line, each with its index, the comments aligned as clang-format aligns them.
        std::vector<std::string> values;
        std::size_t              widest = 0;
        for (const double v : h) {
            std::snprintf(line, sizeof line, "%a,", v);
            values.emplace_back(line);
            widest = std::max(widest, values.back().size());
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::snprintf(line, sizeof line, "                %-*s // h[%zu]\n", static_cast<int>(widest),
                          values[i].c_str(), i);
            s += line;
        }
        s += "            }};\n";
        s += "        };\n";
        return s;
    }

    const char* const k_library_head = R"(/// @file nyquist_tables.h
/// @brief The Nyquist designs resample runs, as constants: generated by tools/tablegen, never
/// edited by hand (PLAN.md 3.3, 12).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | A design  | `nyquist_design<Band, TapsPerBranch, AttenuationDb>`: the Band-th-band filter of |
/// |           | N = 2 m Band - 1 taps, Kaiser-windowed, beta for the attenuation plus 4 dB        |
/// | Structure | the centre tap exactly 1, every Band-th tap from it exactly 0, each branch at DC |
/// |           | gain 1, symmetric bit for bit (DspTap's design_nyquist)                          |
/// | Keys      | only what the generator wrote exists; any other key does not compile, which is  |
/// |           | how a stage at an unpinned divisor is refused                                    |
/// | Values    | each tap in hexadecimal: every bit, from the hosted Linux build (glibc's sin)    |
/// | Rationale | D37, PLAN.md 3.3; CF finding F15 (a design is keyed by band, length, attenuation)|
// Copyright 2026 Timothy Place and the catenae contributors
//
// Generated by tools/tablegen/nyquist_tables.cpp; regenerate with
//   catenae_tablegen include/catenae/nyquist_tables.h tests/support/catenae_test/nyquist_test_tables.h
#ifndef CATENAE_NYQUIST_TABLES_H
#define CATENAE_NYQUIST_TABLES_H

#include <array>
#include <cstddef>

#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {
            template <std::size_t Band, std::size_t TapsPerBranch, unsigned AttenuationDb>
            inline constexpr bool k_no_nyquist_table = false;
        } // namespace detail

        /// @brief The Band-th-band design of TapsPerBranch taps per branch for AttenuationDb: a
        /// table the generator wrote, or no such type.
        template <std::size_t Band, std::size_t TapsPerBranch, unsigned AttenuationDb>
        struct nyquist_design {
            static_assert(detail::k_no_nyquist_table<Band, TapsPerBranch, AttenuationDb>,
                          "catenae: no generated Nyquist table at this band, length and attenuation: a design is a "
                          "type tools/tablegen wrote");
        };

)";

    const char* const k_library_tail = R"(
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_NYQUIST_TABLES_H
)";

    const char* const k_test_head = R"(/// @file nyquist_test_tables.h
/// @brief The battery's short Nyquist designs at the bands the pinned ones do not cover:
/// generated by tools/tablegen beside include/catenae/nyquist_tables.h, never edited by hand.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TESTS_SUPPORT_CATENAE_TEST_NYQUIST_TEST_TABLES_H
#define CATENAE_TESTS_SUPPORT_CATENAE_TEST_NYQUIST_TEST_TABLES_H

#include <array>
#include <cstddef>

#include "catenae/nyquist_tables.h"

namespace catenae {
    inline namespace CATENAE_ABI {

)";

    const char* const k_test_tail = R"(
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_TESTS_SUPPORT_CATENAE_TEST_NYQUIST_TEST_TABLES_H
)";

    bool write(const char* path, const char* head, const std::vector<key>& keys, const char* tail) {
        std::FILE* f = std::fopen(path, "wb");
        if (f == nullptr) {
            std::fprintf(stderr, "catenae_tablegen: cannot write %s\n", path);
            return false;
        }
        std::string text = head;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            text += i == 0 ? "" : "\n";
            text += specialisation(keys[i]);
        }
        text += tail;
        const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        return std::fclose(f) == 0 && ok;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: catenae_tablegen LIBRARY_HEADER TEST_HEADER\n");
        return 2;
    }
    const bool ok =
        write(argv[1], k_library_head, k_library, k_library_tail) && write(argv[2], k_test_head, k_tests, k_test_tail);
    return ok ? 0 : 1;
}
