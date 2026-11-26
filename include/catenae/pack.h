/// @file pack.h
/// @brief `pack`: the native format a chain carries, written to the wire in a format's byte
/// order and packing (D8, D29).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `port<Format::native, Ch>` in, `wire_port<Format, Ch>` out: `q15` to 16-bit, |
/// |            | `q31` to 24- and 32-bit                                                       |
/// | Ratio      | 1:1, input group 1 (frame-synchronous); not in place, the two ports differ   |
/// | Configured | by nothing: the format and the channel count are its type                    |
/// | State      | none (`rests_at_zero`)                                                       |
/// | Workspace  | none                                                                         |
/// | Latency    | exactly 0                                                                    |
/// | Tail       | 0 frames                                                                     |
/// | Rounding   | `q31` to 24 bits once, half up (add 2^7, floor), as `convert` narrows; every |
/// |            | other format exact                                                           |
/// | Saturation | 24 bits at their limits: `q31` of 0x7FFFFF80 and above is 24 bits' maximum   |
/// | Unused     | a 24-in-32 word's unused byte: the sign's extension (LSB-justified) or zero  |
/// |            | (MSB-justified)                                                              |
/// | Method     | the format's compiled loop over every sample (`src/wire.cpp`, PLAN.md 7.1)    |
/// | Rationale  | D8, D29, D47, PLAN.md 12 (written new)                                       |
///
/// @req{CAT-PACK-001} `pack<Format, Ch>` writes, for every sample of every frame, the bytes
/// `wire.h` gives its value (CAT-WIRE-001 to 004), frame for frame, and passes the generic
/// tests a stage does; `unpack` after `pack` gives back every 16- and 32-bit value, and every
/// q31 value with zero below its top 24 bits.
/// @req{CAT-PACK-002} A wire port is a `pack`'s output and nothing else's: a chain whose last
/// stage writes one fills the bytes the caller passes, checked to hold its output frames.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PACK_H
#define CATENAE_PACK_H

#include <cstddef>
#include <type_traits>

#include "catenae/chain.h" // so that a | b composes what this header declares
#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/ratio.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"
#include "catenae/stage_traits.h"
#include "catenae/wire.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// @brief `Ch` channels of native samples, written in wire format `Format`.
        template <typename Format, std::size_t Ch>
        class pack {
            static_assert(wire::is_format<Format>::value, "catenae: pack writes a wire format (catenae/wire.h)");

          public:
            using input_port  = port<typename Format::native, Ch>; ///< what it reads
            using output_port = wire_port<Format, Ch>;             ///< the bytes it writes

            static constexpr ratio       rate_ratio{1, 1};      ///< 1:1
            static constexpr std::size_t input_group = 1;       ///< frame by frame
            static constexpr ratio       max_shortfall{0, 1};   ///< never behind
            static constexpr ratio       max_lead{0, 1};        ///< never ahead
            static constexpr std::size_t output_quantum    = 1; ///< a frame at a time
            static constexpr bool        is_in_place       = false;
            static constexpr bool        has_exact_latency = true;
            static constexpr ratio       exact_latency{0, 1};
            static constexpr bool        rests_at_zero = true; ///< it remembers nothing

            /// Nothing.
            struct state {};
            using workspace = no_workspace; ///< nothing

            /// Every sample of every frame.
            std::size_t run(state&, workspace&, typename input_port::input_view in,
                            typename output_port::output_view out) const noexcept {
                detail::wire_pack<Format>(in.data(), out.data(), in.size());
                return in.size() / Ch;
            }

            /// n frames in, n out.
            static constexpr std::size_t outputs_for(const state&, std::size_t n) noexcept { return n; }
            /// k frames out need k in.
            static constexpr std::size_t frames_needed(const state&, std::size_t k) noexcept { return k; }
            /// n, from any state.
            static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
            /// None.
            stage_latency latency() const noexcept { return {0.0f, 0.0f}; }
            /// None.
            std::size_t tail_frames() const noexcept { return 0; }
            /// None.
            std::size_t practical_tail_frames() const noexcept { return 0; }
            /// None.
            float flush_residue() const noexcept { return 0.0f; }
        };

        template <typename Format, std::size_t Ch>
        struct is_stage<pack<Format, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PACK_H
