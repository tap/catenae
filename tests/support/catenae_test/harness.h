/// @file harness.h
/// @brief The generic tests every stage and chain runs from C1 (PLAN.md section 10, C0b).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Each takes a stage's configuration and returns a GoogleTest AssertionResult that names what
// broke. They drive the stage through its own run, with its own state and workspace, as a chain
// will; the input is a deterministic pseudo-random signal. For an interleaved port of any
// sample format; planar and spectrum ports join when their stages do.
//
//   reset_ignores_storage    storage filled with garbage then reset, and zeroed then reset,
//                            give bit-equal output (D49)
//   counts_are_exact         outputs_for is what run writes, frames_needed is the smallest n
//                            that writes k, max_outputs_for is never exceeded (PLAN.md 3.2)
//   arrival_numbers_hold     input_group, max_shortfall, max_lead and output_quantum by
//                            counting, and the law of section 3.2 for every divisor of the group
//   chunking_is_bit_exact    any chunking of one input gives bit-identical output
#ifndef CATENAE_TESTS_SUPPORT_CATENAE_TEST_HARNESS_H
#define CATENAE_TESTS_SUPPORT_CATENAE_TEST_HARNESS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/ratio.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"

namespace catenae_test {

    /// A deterministic signal: xorshift32 mapped into the format.
    class signal {
      public:
        explicit signal(std::uint32_t seed) noexcept
            : m_state(seed == 0 ? 0x9E3779B9u : seed) {}
        template <typename V>
        V next() noexcept {
            m_state ^= m_state << 13;
            m_state ^= m_state >> 17;
            m_state ^= m_state << 5;
            if constexpr (std::is_floating_point<V>::value) {
                return static_cast<V>(static_cast<double>(m_state) / 2147483648.0 - 1.0);
            }
            else {
                return static_cast<V>(m_state);
            }
        }

      private:
        std::uint32_t m_state;
    };

    /// One stage, its memory, and the counts it has written: what each test drives.
    template <typename Stage>
    class driver {
      public:
        using traits                              = catenae::stage_traits<Stage>;
        using state                               = typename traits::state;
        using workspace                           = typename traits::workspace;
        using value_type                          = typename traits::input_port::value_type;
        using out_value                           = typename traits::output_port::value_type;
        static constexpr std::size_t in_channels  = traits::input_port::values_per_frame;
        static constexpr std::size_t out_channels = traits::output_port::values_per_frame;

        /// The stage, its state filled with `fill` and then reset.
        driver(const Stage& configuration, unsigned char fill) noexcept
            : m_configuration(configuration) {
            std::memset(&m_state, fill, sizeof(state));
            std::memset(&m_workspace, fill, sizeof(workspace));
            traits::reset(m_configuration, m_state);
        }

        const state& current() const noexcept { return m_state; }

        /// Runs n frames of input; appends what the stage writes to `out` and returns its count.
        /// The output has room for the larger of max_outputs_for and outputs_for, so that a
        /// stage whose bound is wrong is caught by the count test rather than overrunning it.
        std::size_t run(const std::vector<value_type>& in, std::size_t from, std::size_t n,
                        std::vector<out_value>& out) {
            const std::size_t room = std::max(Stage::max_outputs_for(n), m_configuration.outputs_for(m_state, n));
            m_scratch.assign(room * out_channels, out_value{});
            const std::size_t written = m_configuration.run(
                m_state, m_workspace, catenae::span<const value_type>(in.data() + from * in_channels, n * in_channels),
                catenae::span<out_value>(m_scratch.data(), room * out_channels));
            out.insert(out.end(), m_scratch.begin(),
                       m_scratch.begin() + static_cast<std::ptrdiff_t>(written * out_channels));
            return written;
        }

      private:
        Stage                  m_configuration;
        state                  m_state;
        workspace              m_workspace;
        std::vector<out_value> m_scratch;
    };

    /// Input of `frames` frames for a stage.
    template <typename Stage>
    std::vector<typename driver<Stage>::value_type> input_for(std::size_t frames, std::uint32_t seed) {
        signal                                          s(seed);
        std::vector<typename driver<Stage>::value_type> in(frames * driver<Stage>::in_channels);
        for (auto& v : in) {
            v = s.next<typename driver<Stage>::value_type>();
        }
        return in;
    }

    /// Call sizes from 0 to `largest` frames, summing to `total`.
    inline std::vector<std::size_t> chunks(std::size_t total, std::size_t largest, std::uint32_t seed) {
        signal                   s(seed);
        std::vector<std::size_t> sizes;
        std::size_t              done = 0;
        while (done < total) {
            const std::size_t n = std::min<std::size_t>(s.next<std::uint32_t>() % (largest + 1), total - done);
            sizes.push_back(n);
            done += n;
        }
        return sizes;
    }

    template <typename V>
    bool bit_equal(const std::vector<V>& a, const std::vector<V>& b) {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(V)) == 0);
    }

    /// Storage filled with garbage then reset, and zeroed then reset, give bit-equal output (D49).
    /// Three garbage patterns, since one can hide a forgotten member: 0xA5 bytes are a float of
    /// about -3e-16, lost beside a signal near 1, where 0x5A bytes are 2e16 and 0xFF a NaN.
    template <typename Stage>
    ::testing::AssertionResult reset_ignores_storage(const Stage& configuration, std::size_t frames = 1000) {
        const auto                                     in    = input_for<Stage>(frames, 1);
        const auto                                     sizes = chunks(frames, 97, 2);
        std::vector<typename driver<Stage>::out_value> from_zero;
        driver<Stage>                                  zero(configuration, 0x00);
        std::size_t                                    at = 0;
        for (const std::size_t n : sizes) {
            zero.run(in, at, n, from_zero);
            at += n;
        }
        for (const unsigned fill : {0xA5u, 0x5Au, 0xFFu}) {
            std::vector<typename driver<Stage>::out_value> from_garbage;
            driver<Stage>                                  garbage(configuration, static_cast<unsigned char>(fill));
            at = 0;
            for (const std::size_t n : sizes) {
                garbage.run(in, at, n, from_garbage);
                at += n;
            }
            if (!bit_equal(from_garbage, from_zero)) {
                return ::testing::AssertionFailure()
                       << "a state reset over bytes of " << fill << " and one reset over zero write different output";
            }
        }
        return ::testing::AssertionSuccess();
    }

    /// outputs_for is exact, frames_needed is smallest, max_outputs_for is never exceeded.
    template <typename Stage>
    ::testing::AssertionResult counts_are_exact(const Stage& configuration, std::size_t frames = 2000) {
        const auto                                     in = input_for<Stage>(frames, 3);
        std::vector<typename driver<Stage>::out_value> out;
        driver<Stage>                                  d(configuration, 0);
        std::size_t                                    at = 0;
        for (const std::size_t n : chunks(frames, 150, 4)) {
            for (std::size_t k = 0; k <= 40; ++k) {
                const std::size_t need = configuration.frames_needed(d.current(), k);
                if (configuration.outputs_for(d.current(), need) < k
                    || (need > 0 && configuration.outputs_for(d.current(), need - 1) >= k)) {
                    return ::testing::AssertionFailure()
                           << "frames_needed(" << k << ") = " << need << " is not the smallest n that writes " << k;
                }
            }
            const std::size_t predicted = configuration.outputs_for(d.current(), n);
            const std::size_t written   = d.run(in, at, n, out);
            at += n;
            if (written != predicted) {
                return ::testing::AssertionFailure()
                       << "outputs_for(" << n << ") said " << predicted << ", run wrote " << written;
            }
            if (written > Stage::max_outputs_for(n)) {
                return ::testing::AssertionFailure() << "run wrote " << written << " frames for " << n
                                                     << ", above max_outputs_for " << Stage::max_outputs_for(n);
            }
        }
        return ::testing::AssertionSuccess();
    }

    /// The output count after N frames from reset, fed in calls of q frames, for N up to `horizon`.
    template <typename Stage>
    std::vector<std::size_t> counts_by(const Stage& configuration, std::size_t q, std::size_t horizon) {
        const auto                                     in = input_for<Stage>(horizon, 5);
        std::vector<typename driver<Stage>::out_value> out;
        driver<Stage>                                  d(configuration, 0);
        std::vector<std::size_t>                       counts(horizon / q + 1, 0);
        std::size_t                                    total = 0;
        for (std::size_t call = 0; (call + 1) * q <= horizon; ++call) {
            total += d.run(in, call * q, q, out);
            counts[call + 1] = total;
        }
        return counts;
    }

    /// N L/M as a ratio: the output frames owed after N input frames.
    inline catenae::ratio owed(std::size_t n, catenae::ratio l_m) {
        return catenae::reduce(catenae::ratio{static_cast<std::uint32_t>(n), 1} * l_m);
    }

    /// The law's scale at calls of q frames, a divisor of the group (PLAN.md section 3.2):
    /// (group - q) / (group - 1), and 0 when q is the whole group.
    inline catenae::ratio law_scale(std::size_t group, std::size_t q) {
        if (group == 1) {
            return catenae::ratio{0, 1};
        }
        return catenae::reduce(
            catenae::ratio{static_cast<std::uint32_t>(group - q), static_cast<std::uint32_t>(group - 1)});
    }

    /// The smallest n with O(kn) = kn L/M for every k up to the horizon, counted frame by frame.
    template <typename Stage>
    std::size_t observed_group(const Stage& configuration, std::size_t horizon) {
        const auto unit = counts_by(configuration, 1, horizon);
        for (std::size_t n = 1; n <= horizon; ++n) {
            bool holds = true;
            for (std::size_t kn = n; kn <= horizon && holds; kn += n) {
                holds = catenae::ratio{static_cast<std::uint32_t>(unit[kn]), 1}
                        == owed(kn, catenae::stage_traits<Stage>::rate_ratio);
            }
            if (holds) {
                return n;
            }
        }
        return 0;
    }

    /// What counting finds at calls of q frames: the worst trail and lead, and the greatest
    /// common divisor of the counts the calls write.
    struct arrival {
        catenae::ratio trail;
        catenae::ratio lead;
        std::size_t    quantum;
    };
    template <typename Stage>
    arrival arrival_at(const Stage& configuration, std::size_t q, std::size_t horizon) {
        const auto by_q  = counts_by(configuration, q, horizon);
        arrival    found = {{0, 1}, {0, 1}, 0};
        for (std::size_t call = 1; call < by_q.size(); ++call) {
            const catenae::ratio have{static_cast<std::uint32_t>(by_q[call]), 1};
            const catenae::ratio due = owed(call * q, catenae::stage_traits<Stage>::rate_ratio);
            found.trail              = have < due && due - have > found.trail ? due - have : found.trail;
            found.lead               = have > due && have - due > found.lead ? have - due : found.lead;
            found.quantum            = std::gcd(found.quantum, by_q[call] - by_q[call - 1]);
        }
        return found;
    }

    /// The greatest common divisor of the counts of calls of random size: the tightest quantum.
    template <typename Stage>
    std::size_t observed_quantum(const Stage& configuration, std::size_t horizon) {
        const auto                                     in = input_for<Stage>(horizon, 6);
        std::vector<typename driver<Stage>::out_value> out;
        driver<Stage>                                  d(configuration, 0);
        std::size_t                                    at = 0, quantum = 0;
        for (const std::size_t n : chunks(horizon, 3 * catenae::stage_traits<Stage>::input_group + 1, 7)) {
            quantum = std::gcd(quantum, d.run(in, at, n, out));
            at += n;
        }
        return quantum;
    }

    /// The bound on trail and lead at calls of q frames. A stage's is its published numbers by
    /// the law; a chain's is its stages' composed at q (PLAN.md section 4: a chain is composed
    /// through, so its own numbers scaled by the law are not its bound at q).
    template <typename Stage>
    std::pair<catenae::ratio, catenae::ratio> bound_at(std::size_t q) {
        using traits = catenae::stage_traits<Stage>;
        if constexpr (catenae::is_chain<Stage>::value) {
            const auto leaves = catenae::detail::leaves_of<Stage>::get();
            const auto pace   = catenae::detail::composed_pace(leaves.data(), leaves.size(), q);
            return {pace.shortfall, pace.lead};
        }
        else {
            const auto scale = law_scale(traits::input_group, q);
            return {traits::max_shortfall * scale, traits::max_lead * scale};
        }
    }

    /// The law at one divisor q of the group: the trail and lead within the bound at q, every
    /// count a multiple of output_quantum, and, frame by frame and when `exact`, the published
    /// numbers reached.
    template <typename Stage>
    ::testing::AssertionResult law_holds_at(const Stage& configuration, std::size_t q, std::size_t horizon,
                                            bool exact) {
        using traits        = catenae::stage_traits<Stage>;
        const arrival found = arrival_at(configuration, q, horizon);
        const auto    bound = bound_at<Stage>(q);
        const auto    print = [](catenae::ratio r) { return std::to_string(r.up) + "/" + std::to_string(r.down); };
        if (found.trail > bound.first || found.lead > bound.second) {
            return ::testing::AssertionFailure()
                   << "at calls of " << q << " frames the output trails by " << print(found.trail) << " and leads by "
                   << print(found.lead) << ", beyond the law's bound";
        }
        if (q == 1 && exact && (found.trail != traits::max_shortfall || found.lead != traits::max_lead)) {
            return ::testing::AssertionFailure()
                   << "frame by frame the worst trail is " << print(found.trail) << " and lead " << print(found.lead)
                   << ", not the published max_shortfall and max_lead";
        }
        if (found.quantum % traits::output_quantum != 0) {
            return ::testing::AssertionFailure()
                   << "a call wrote a count that is not a multiple of output_quantum " << traits::output_quantum;
        }
        return ::testing::AssertionSuccess();
    }

    /// The arrival numbers by counting (PLAN.md section 3.2): input_group, the law at every
    /// divisor of the group, and output_quantum. `exact` also requires the published shortfall,
    /// lead and quantum to be the tightest true values, not only bounds that hold.
    template <typename Stage>
    ::testing::AssertionResult arrival_numbers_hold(const Stage& configuration, bool exact, std::size_t groups = 64) {
        using traits              = catenae::stage_traits<Stage>;
        const std::size_t group   = traits::input_group;
        const std::size_t horizon = group * groups;
        const std::size_t counted = observed_group(configuration, horizon);
        if (counted != group) {
            return ::testing::AssertionFailure() << "input_group is " << group << ", counting finds " << counted;
        }
        for (std::size_t q = 1; q <= group; ++q) {
            if (group % q == 0) {
                const auto at_q = law_holds_at(configuration, q, horizon, exact);
                if (!at_q) {
                    return at_q;
                }
            }
        }
        if (exact && observed_quantum(configuration, horizon) != traits::output_quantum) {
            return ::testing::AssertionFailure()
                   << "calls write multiples of " << observed_quantum(configuration, horizon)
                   << ", not of the published output_quantum " << traits::output_quantum;
        }
        return ::testing::AssertionSuccess();
    }

    /// Any chunking of one input gives bit-identical output.
    template <typename Stage>
    ::testing::AssertionResult chunking_is_bit_exact(const Stage& configuration, std::size_t frames = 1500) {
        const auto                                     in = input_for<Stage>(frames, 8);
        std::vector<typename driver<Stage>::out_value> whole;
        driver<Stage>                                  once(configuration, 0);
        once.run(in, 0, frames, whole);
        for (std::uint32_t seed = 10; seed < 16; ++seed) {
            std::vector<typename driver<Stage>::out_value> pieces;
            driver<Stage>                                  d(configuration, 0);
            std::size_t                                    at = 0;
            for (const std::size_t n : chunks(frames, seed * 7, seed)) {
                d.run(in, at, n, pieces);
                at += n;
            }
            // The whole call may hold back what a chunked one has written, or the reverse: compare
            // what both wrote.
            const std::size_t common = std::min(whole.size(), pieces.size());
            if (std::memcmp(whole.data(), pieces.data(), common * sizeof(whole[0])) != 0) {
                return ::testing::AssertionFailure() << "chunking pattern " << seed << " writes different output";
            }
        }
        return ::testing::AssertionSuccess();
    }

} // namespace catenae_test

#endif // CATENAE_TESTS_SUPPORT_CATENAE_TEST_HARNESS_H
