/// @file port.h
/// @brief Ports: the types a stage reads and writes, and the joint that proves two meet (D6, D17).
///
/// | Contract  |                                                                                      |
/// |-----------|--------------------------------------------------------------------------------------|
/// | Audio     | `port<S, Ch>` interleaved, `port<S, Ch, layout::planar>` as `Ch` spans               |
/// | Spectrum  | `spectrum<S, Ch, Bins>`: `Ch * Bins` values of `complex<storage>`, channels end to end |
/// | Wire      | `wire_port<Format, Ch>`: bytes; the formats are in namespace `wire` (from C2)        |
/// | Views     | `input_view`, `output_view`: what a stage's `run` takes for the port                 |
/// | Joint     | `joint<Index, Out, In>`: one fixed message per way two ports can differ             |
/// | Rationale | D5, D6, D7, D8, D17, D25, D42                                                        |
///
/// @req{CAT-PORT-001} A port's value type is its format's storage; a frame is `Ch` values of an
/// audio port, `Ch * Bins` of a spectrum and `Ch * bytes_per_sample` bytes of a wire port.
/// @req{CAT-PORT-002} An interleaved audio port is viewed as one span, a planar one as an array
/// of `Ch` spans, a spectrum as one span of complex values and a wire port as one span of bytes.
/// @req{CAT-PORT-003} Two ports join only when they are the same type; each way they can differ
/// (kind, sample format, channel count, layout, bins) fails with its own fixed message.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_PORT_H
#define CATENAE_PORT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/fwd.h"
#include "catenae/sample_format.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// What a port carries (D17).
        enum class signal_kind : std::uint8_t {
            audio,    ///< time-domain samples
            spectrum, ///< frames of transform coefficients
            wire,     ///< packed bytes, only at a chain's edges (D8)
        };

        /// Time-domain audio in sample format `S`, `Ch` channels, interleaved or planar.
        template <typename S, std::size_t Ch, layout L>
        struct port {
            static_assert(
                is_sample_format<S>::value,
                "catenae: a port carries a sample format: float, double or a fixed<> tag, never a raw integer");
            static_assert(Ch >= 1, "catenae: a port has at least one channel");
            using format                                  = S;                  ///< the sample format
            using value_type                              = storage_t<S>;       ///< what the port's spans hold
            static constexpr signal_kind kind             = signal_kind::audio; ///< what it carries
            static constexpr std::size_t channels         = Ch;                 ///< channels, a constant (D42)
            static constexpr layout      arrangement      = L;                  ///< interleaved or planar
            static constexpr std::size_t values_per_frame = Ch;                 ///< values in one frame
            /// What a stage reads: one span, or Ch spans when planar.
            using input_view = std::conditional_t<L == layout::interleaved, span<const value_type>,
                                                  std::array<span<const value_type>, Ch>>;
            /// What a stage writes.
            using output_view =
                std::conditional_t<L == layout::interleaved, span<value_type>, std::array<span<value_type>, Ch>>;
        };

        /// Frames of `Bins` transform coefficients per channel, channels end to end (D25).
        template <typename S, std::size_t Ch, std::size_t Bins>
        struct spectrum {
            static_assert(is_sample_format<S>::value, "catenae: a spectrum carries a sample format");
            static_assert(Ch >= 1 && Bins >= 1, "catenae: a spectrum has at least one channel and one bin");
            using format                                  = S;                      ///< the sample format of each part
            using value_type                              = complex<storage_t<S>>;  ///< one coefficient
            static constexpr signal_kind kind             = signal_kind::spectrum;  ///< what it carries
            static constexpr std::size_t channels         = Ch;                     ///< channels
            static constexpr std::size_t bins             = Bins;                   ///< coefficients per channel
            static constexpr std::size_t values_per_frame = Ch * Bins;              ///< values in one frame
            using input_view                              = span<const value_type>; ///< what a stage reads
            using output_view                             = span<value_type>;       ///< what a stage writes
        };

        /// The wire formats, written with unpack and pack at C2 (D8).
        namespace wire {}

        /// Packed bytes in a wire format: the outer port of `unpack` and `pack` only (D8).
        template <typename Format, std::size_t Ch>
        struct wire_port {
            static_assert(Ch >= 1, "catenae: a wire port has at least one channel");
            using format                                  = Format;                   ///< the wire format
            using value_type                              = std::byte;                ///< what the port's spans hold
            static constexpr signal_kind kind             = signal_kind::wire;        ///< what it carries
            static constexpr std::size_t channels         = Ch;                       ///< channels
            static constexpr std::size_t bytes_per_sample = Format::bytes_per_sample; ///< bytes a sample
            static constexpr std::size_t values_per_frame = Ch * bytes_per_sample;    ///< bytes a frame
            using input_view                              = span<const std::byte>;    ///< what unpack reads
            using output_view                             = span<std::byte>;          ///< what pack writes
        };

        /// Whether a type is a port.
        template <typename T>
        struct is_port : std::false_type {};
        template <typename S, std::size_t Ch, layout L>
        struct is_port<port<S, Ch, L>> : std::true_type {};
        template <typename S, std::size_t Ch, std::size_t Bins>
        struct is_port<spectrum<S, Ch, Bins>> : std::true_type {};
        template <typename F, std::size_t Ch>
        struct is_port<wire_port<F, Ch>> : std::true_type {};

        namespace detail {
            /// What a joint compares, safe for any type (a non-port compares as a non-port).
            template <typename T, bool = is_port<T>::value>
            struct port_facts {
                static constexpr bool        k_is_port     = false;
                static constexpr signal_kind k_kind        = signal_kind::audio;
                static constexpr std::size_t k_channels    = 0;
                static constexpr layout      k_arrangement = layout::interleaved;
                static constexpr std::size_t k_bins        = 0;
                using format                               = void;
            };
            template <typename S, std::size_t Ch, layout L>
            struct port_facts<port<S, Ch, L>, true> {
                static constexpr bool        k_is_port     = true;
                static constexpr signal_kind k_kind        = signal_kind::audio;
                static constexpr std::size_t k_channels    = Ch;
                static constexpr layout      k_arrangement = L;
                static constexpr std::size_t k_bins        = 0;
                using format                               = S;
            };
            template <typename S, std::size_t Ch, std::size_t Bins>
            struct port_facts<spectrum<S, Ch, Bins>, true> {
                static constexpr bool        k_is_port     = true;
                static constexpr signal_kind k_kind        = signal_kind::spectrum;
                static constexpr std::size_t k_channels    = Ch;
                static constexpr layout      k_arrangement = layout::interleaved;
                static constexpr std::size_t k_bins        = Bins;
                using format                               = S;
            };
            template <typename F, std::size_t Ch>
            struct port_facts<wire_port<F, Ch>, true> {
                static constexpr bool        k_is_port     = true;
                static constexpr signal_kind k_kind        = signal_kind::wire;
                static constexpr std::size_t k_channels    = Ch;
                static constexpr layout      k_arrangement = layout::interleaved;
                static constexpr std::size_t k_bins        = 0;
                using format                               = F;
            };
        } // namespace detail

        /// The joint between stage `Index`'s output port and the next stage's input port. Forming
        /// it proves the two are one type, or fails with the message for the way they differ.
        template <std::size_t Index, typename Out, typename In>
        struct joint {
            using out = detail::port_facts<Out>; ///< what the output port is
            using in  = detail::port_facts<In>;  ///< what the input port is
            static_assert(out::k_is_port && in::k_is_port, "catenae: a joint joins two ports");
            static_assert(!(out::k_kind == signal_kind::spectrum && in::k_kind == signal_kind::audio),
                          "catenae: a spectrum cannot feed a time-domain port");
            static_assert(!(out::k_kind == signal_kind::audio && in::k_kind == signal_kind::spectrum),
                          "catenae: a time-domain port cannot feed a spectrum");
            static_assert(
                out::k_kind != signal_kind::wire && in::k_kind != signal_kind::wire,
                "catenae: a wire port joins nothing inside a chain: only unpack reads one and pack writes one");
            static constexpr bool same_kind = out::k_kind == in::k_kind; ///< the kinds agree
            static_assert(!same_kind || std::is_same<typename out::format, typename in::format>::value,
                          "catenae: one stage's output and the next stage's input carry different sample formats");
            static_assert(!same_kind || out::k_channels == in::k_channels,
                          "catenae: one stage's output and the next stage's input have different channel counts");
            static_assert(
                !same_kind || out::k_arrangement == in::k_arrangement,
                "catenae: one stage's output and the next stage's input have different layouts (interleaved, planar)");
            static_assert(!same_kind || out::k_bins == in::k_bins,
                          "catenae: one stage's output and the next stage's input have different numbers of bins");
            static_assert(std::is_same<Out, In>::value,
                          "catenae: one stage's output port is not the next stage's input port");
            static constexpr std::size_t index = Index; ///< the stage whose output it is
            static constexpr bool        value = true;  ///< formed: the ports meet
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_PORT_H
