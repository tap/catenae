/// @file schedule.h
/// @brief CF skeleton: bridge's compile-time (phase, advance) schedule for L/M = 160/147.
///
/// Entry n serves output frame kL + n: phase(n) = nM mod L visits every branch once per
/// superblock, and advance(n) = floor((n + 1)M / L) - floor(nM / L) (0 or 1 going up) sums to M,
/// so L outputs always take M inputs. A constant table in ROM: no modulo and no division run.
///
/// Carried from SampleRateTap 4496888 bridge/include/tap/sr/bridge/schedule.h (schedule_entry,
/// make_schedule, k_schedule), on L and M rather than on bridge's direction.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_BRIDGE_SCHEDULE_H
#define CATENAE_FIT_BRIDGE_SCHEDULE_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace catenae_fit::bridge {

    /// One output frame's step: the branch to dot, then the input frames to take.
    struct schedule_entry {
        std::uint16_t phase;   ///< the polyphase branch, in [0, L)
        std::uint8_t  advance; ///< input frames taken after this output
    };

    /// The superblock of L/M.
    template <std::size_t L, std::size_t M>
    constexpr std::array<schedule_entry, L> make_schedule() noexcept {
        std::array<schedule_entry, L> s{};
        for (std::size_t n = 0; n < L; ++n) {
            s[n].phase   = static_cast<std::uint16_t>((n * M) % L);
            s[n].advance = static_cast<std::uint8_t>(((n + 1) * M) / L - (n * M) / L);
        }
        return s;
    }

    /// The schedule of L/M, in ROM.
    template <std::size_t L, std::size_t M>
    inline constexpr std::array<schedule_entry, L> schedule = make_schedule<L, M>();

} // namespace catenae_fit::bridge

#endif // CATENAE_FIT_BRIDGE_SCHEDULE_H
