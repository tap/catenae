/// @file test_chain_composition.cpp
/// @brief The composition of the arrival numbers against a brute-force count model (PLAN.md 4).
// Copyright 2026 Timothy Place and the catenae contributors
//
// A model stage is its published numbers and its output count from reset as a closed form of
// the frames it has been given. A chain of them counts by composing the forms. For every chain
// of up to three of the model's stages, the composed numbers (detail::composed_*, the functions
// chain<> itself uses) are checked against the counts: never too small, frame by frame and at
// every divisor of the group, and the group a group by its definition.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/detail/arrival.h"

namespace {

    using catenae::ratio;
    using catenae::detail::arrival_leaf;

    enum class kind { down_first, down_last, up, block, lm_first, lm_last };

    struct model {
        kind          what;
        std::uint32_t l;
        std::uint32_t m;

        /// Output frames from reset after n input frames.
        std::uint64_t out(std::uint64_t n) const noexcept {
            switch (what) {
            case kind::down_first:
                return (n + m - 1) / m;
            case kind::down_last:
                return n / m;
            case kind::up:
                return n * l;
            case kind::block:
                return n / l * l;
            case kind::lm_first:
                return (n * l + m - 1) / m;
            case kind::lm_last:
                return n * l / m;
            }
            return 0;
        }

        /// What such a stage publishes.
        arrival_leaf leaf() const noexcept {
            const ratio none{0, 1};
            switch (what) {
            case kind::down_first:
                return {ratio{1, m}, m, none, ratio{m - 1, m}, 1};
            case kind::down_last:
                return {ratio{1, m}, m, ratio{m - 1, m}, none, 1};
            case kind::up:
                return {ratio{l, 1}, 1, none, none, l};
            case kind::block:
                return {ratio{1, 1}, l, ratio{l - 1, 1}, none, l};
            case kind::lm_first:
                return {ratio{l, m}, m, none, ratio{m - 1, m}, 1};
            case kind::lm_last:
                return {ratio{l, m}, m, ratio{m - 1, m}, none, 1};
            }
            return {ratio{1, 1}, 1, none, none, 1};
        }
    };

    const std::array<model, 12> k_pool = {{
        {kind::down_first, 1, 2},
        {kind::down_first, 1, 3},
        {kind::down_last, 1, 2},
        {kind::down_last, 1, 3},
        {kind::up, 2, 1},
        {kind::up, 3, 1},
        {kind::block, 3, 1},
        {kind::block, 4, 1},
        {kind::block, 5, 1},
        {kind::lm_first, 2, 3},
        {kind::lm_first, 3, 2},
        {kind::lm_last, 3, 2},
    }};

    struct counted {
        ratio trail;
        ratio lead;
    };

    /// The worst trail and lead of a chain fed in multiples of q, up to n_max frames.
    counted count(const std::vector<model>& c, ratio rate, std::uint64_t q, std::uint64_t n_max) {
        counted worst{ratio{0, 1}, ratio{0, 1}};
        for (std::uint64_t n = q; n <= n_max; n += q) {
            std::uint64_t o = n;
            for (const model& s : c) {
                o = s.out(o);
            }
            // O(n) against n L/M, in units of 1/M.
            const std::uint64_t have = o * rate.down;
            const std::uint64_t due  = n * rate.up;
            if (due > have) {
                const ratio t = catenae::detail::reduce64(due - have, rate.down);
                worst.trail   = t > worst.trail ? t : worst.trail;
            }
            else {
                const ratio l = catenae::detail::reduce64(have - due, rate.down);
                worst.lead    = l > worst.lead ? l : worst.lead;
            }
        }
        return worst;
    }

    void check_chain(const std::vector<model>& c, std::size_t& checked) {
        std::vector<arrival_leaf> leaves;
        for (const model& s : c) {
            leaves.push_back(s.leaf());
        }
        const ratio         rate    = catenae::detail::composed_rate(leaves.data(), leaves.size());
        const std::size_t   group   = catenae::detail::composed_group(leaves.data(), leaves.size());
        const std::uint64_t horizon = 3 * std::uint64_t{group} + 16;
        // The group is a group: whole multiples of it are owed exactly.
        for (std::uint64_t k = 1; k <= 3; ++k) {
            std::uint64_t o = k * group;
            for (const model& s : c) {
                o = s.out(o);
            }
            ASSERT_EQ(o * rate.down, k * group * rate.up) << "chain " << checked << ": not a group at " << k;
        }
        // Never too small, at every divisor of the group.
        for (std::size_t q = 1; q <= group; ++q) {
            if (group % q != 0) {
                continue;
            }
            const catenae::detail::pace published = catenae::detail::composed_pace(leaves.data(), leaves.size(), q);
            const counted               seen      = count(c, rate, q, horizon);
            ASSERT_LE(seen.trail.up * std::uint64_t{published.shortfall.down},
                      published.shortfall.up * std::uint64_t{seen.trail.down})
                << "chain " << checked << " at q " << q << ": shortfall too small";
            ASSERT_LE(seen.lead.up * std::uint64_t{published.lead.down},
                      published.lead.up * std::uint64_t{seen.lead.down})
                << "chain " << checked << " at q " << q << ": lead too small";
        }
        ++checked;
    }

    // Verifies CAT-CHAIN-013.
    TEST(chain_composition, NeverTooSmallOverTheCountModel) {
        std::size_t checked = 0;
        for (const model& a : k_pool) {
            check_chain({a}, checked);
            for (const model& b : k_pool) {
                check_chain({a, b}, checked);
                for (const model& c : k_pool) {
                    check_chain({a, b, c}, checked);
                }
            }
        }
        EXPECT_EQ(checked, 12u + 144u + 1728u);
    }

    // Verifies CAT-CHAIN-013: exact for two block stages whose sizes do not nest.
    TEST(chain_composition, ExactForTwoBlockStages) {
        for (const auto& pair :
             {std::array<std::uint32_t, 3>{{96, 128, 191}}, std::array<std::uint32_t, 3>{{480, 512, 959}},
              std::array<std::uint32_t, 3>{{7, 5, 10}}}) {
            const std::vector<model>        c      = {{kind::block, pair[0], 1}, {kind::block, pair[1], 1}};
            const std::vector<arrival_leaf> leaves = {c[0].leaf(), c[1].leaf()};
            const catenae::detail::pace     p      = catenae::detail::composed_pace(leaves.data(), 2, 1);
            const std::size_t               group  = catenae::detail::composed_group(leaves.data(), 2);
            const counted                   seen   = count(c, ratio{1, 1}, 1, 2 * std::uint64_t{group});
            EXPECT_EQ(p.shortfall.up, pair[2]);
            EXPECT_EQ(seen.trail.up, pair[2]) << pair[0] << " before " << pair[1];
            EXPECT_EQ(seen.trail.down, 1u);
        }
    }

} // namespace
