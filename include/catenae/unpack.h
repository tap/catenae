/// @file unpack.h
/// @brief `unpack`: samples off the wire, in their format's byte order and packing, into the
/// native format a chain carries (D8, D29).
///
/// | Contract   |                                                                              |
/// |------------|------------------------------------------------------------------------------|
/// | Ports      | `wire_port<Format, Ch>` in, `port<Format::native, Ch>` out: 16-bit to `q15`, |
/// |            | 24- and 32-bit to `q31`, 24 bits at its top                                  |
/// | Ratio      | 1:1, input group 1 (frame-synchronous); not in place, the two ports differ   |
/// | Configured | by nothing: the format and the channel count are its type                    |
/// | State      | none (`rests_at_zero`)                                                       |
/// | Workspace  | none                                                                         |
/// | Latency    | exactly 0                                                                    |
/// | Tail       | 0 frames                                                                     |
/// | Formats    | `wire.h`'s ten; every value exact, a 24-in-32 word's unused byte ignored     |
/// | Method     | the format's compiled loop over every sample (`src/wire.cpp`, PLAN.md 7.1)    |
/// | Rationale  | D8, D29, D47, PLAN.md 12 (written new)                                       |
///
/// @req{CAT-UNPACK-001} `unpack<Format, Ch>` writes, for every sample of every frame, the value
/// `wire.h` gives its bytes (CAT-WIRE-001 to 003), frame for frame, and passes the generic
/// tests a stage does.
/// @req{CAT-UNPACK-002} A wire port is an `unpack`'s input and nothing else's: a chain whose
/// first stage reads one begins with the bytes the caller passes, checked as whole frames of
/// `Ch * bytes_per_sample` bytes.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_UNPACK_H
#define CATENAE_UNPACK_H

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

        /// @brief `Ch` channels of samples in wire format `Format`, as their native format.
        template <typename Format, std::size_t Ch>
        class unpack {
            static_assert(wire::is_format<Format>::value, "catenae: unpack reads a wire format (catenae/wire.h)");

          public:
            using input_port  = wire_port<Format, Ch>;             ///< the bytes it reads
            using output_port = port<typename Format::native, Ch>; ///< what it writes

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
                const std::size_t samples = in.size() / Format::bytes_per_sample;
                detail::wire_unpack<Format>(in.data(), out.data(), samples);
                return samples / Ch;
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
        struct is_stage<unpack<Format, Ch>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_UNPACK_H
