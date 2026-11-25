/// @file arrival.h
/// @brief How a chain's arrival numbers compose from its stages' (PLAN.md sections 3.2 and 4, D35).
// Copyright 2026 Timothy Place and the catenae contributors
//
// A stage is reduced to its leaf: its ratio, input group, shortfall, lead and output quantum.
// A chain is its stages' leaves end to end, a nested chain's composed through, so the rules
// here run over one array, and the battery's count model (tests/test_chain_composition.cpp)
// runs the same functions over chains it builds at run time.
//
//   input_group     the smallest n for which the frames reaching every stage are a whole
//                   number of its groups
//   shortfall, lead the quantum q reaching each stage is carried along: 1 at the input; q L/M
//                   after a stage fed whole groups; its output_quantum after any other. Each
//                   stage's numbers at its q (the law: scaled by (group - gcd(q, group)) /
//                   (group - 1), none at whole groups), carried to the output rate, summed
//   output_quantum  the q that leaves the last stage
#ifndef CATENAE_DETAIL_ARRIVAL_H
#define CATENAE_DETAIL_ARRIVAL_H

#include <cstddef>
#include <cstdint>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/detail/convert.h"
#include "catenae/ratio.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            /// One stage's published arrival numbers.
            struct arrival_leaf {
                ratio       rate;
                std::size_t group;
                ratio       shortfall;
                ratio       lead;
                std::size_t quantum;
            };

            /// How far output can trail and lead what is owed, and the multiples it is written in.
            struct pace {
                ratio       shortfall;
                ratio       lead;
                std::size_t quantum;
            };

            /// One stage fed in whole multiples of q: the law of PLAN.md section 3.2.
            constexpr pace leaf_at(const arrival_leaf& stage, std::size_t q) noexcept {
                if (q % stage.group == 0) {
                    // Whole groups in: exactly q L/M out, every call.
                    const std::uint64_t made = std::uint64_t{q} * stage.rate.up;
                    check_audit(made % stage.rate.down == 0, check::invariant);
                    return pace{ratio{0, 1}, ratio{0, 1}, narrow<std::size_t>(made / stage.rate.down)};
                }
                const std::uint64_t whole = gcd(q, stage.group);
                const ratio         scale = reduce64(stage.group - whole, std::uint64_t{stage.group} - 1);
                return pace{stage.shortfall * scale, stage.lead * scale, stage.quantum};
            }

            /// The smallest n for which the frames reaching every stage are whole groups of it.
            constexpr std::size_t composed_group(const arrival_leaf* stages, std::size_t count) noexcept {
                std::uint64_t n = 1;
                ratio         before{1, 1}; // from the chain's input to this stage's input
                for (std::size_t i = 0; i < count; ++i) {
                    const std::uint64_t need = std::uint64_t{before.down} * stages[i].group;
                    n *= need / gcd(n * before.up, need);
                    before = before * stages[i].rate;
                }
                return narrow<std::size_t>(n);
            }

            /// The chain's shortfall, lead and quantum when its input arrives in multiples of q.
            constexpr pace composed_pace(const arrival_leaf* stages, std::size_t count, std::size_t q) noexcept {
                pace total{ratio{0, 1}, ratio{0, 1}, q};
                for (std::size_t i = 0; i < count; ++i) {
                    const pace mine = leaf_at(stages[i], total.quantum);
                    // Carry what is already summed to this stage's output rate, then add its own.
                    total = pace{total.shortfall * stages[i].rate + mine.shortfall,
                                 total.lead * stages[i].rate + mine.lead, mine.quantum};
                }
                return total;
            }

            /// The product of the stages' ratios.
            constexpr ratio composed_rate(const arrival_leaf* stages, std::size_t count) noexcept {
                ratio rate{1, 1};
                for (std::size_t i = 0; i < count; ++i) {
                    rate = rate * stages[i].rate;
                }
                return rate;
            }

            /// The most input frames a stage needs, from any state, to write k frames: its output
            /// over n frames is at least n L/M less its shortfall and its lead together.
            constexpr std::size_t most_frames_in(ratio rate, ratio shortfall, ratio lead, std::size_t k) noexcept {
                if (k == 0) {
                    return 0;
                }
                const std::uint64_t den = std::uint64_t{shortfall.down} * lead.down;
                const std::uint64_t num = std::uint64_t{k} * den + std::uint64_t{shortfall.up} * lead.down
                                          + std::uint64_t{lead.up} * shortfall.down;
                const std::uint64_t top    = num * rate.down;
                const std::uint64_t bottom = den * rate.up;
                return narrow<std::size_t>((top + bottom - 1) / bottom);
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_ARRIVAL_H
