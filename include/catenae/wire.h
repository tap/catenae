/// @file wire.h
/// @brief The wire formats `unpack` reads and `pack` writes, and the compiled loops that move
/// them (D8, D29, PLAN.md 7.1).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Formats   | signed two's complement, in either byte order: 16-bit; 24-bit packed in three   |
/// |           | bytes; 24 bits in a 32-bit word, at its least or its most significant end; 32-bit |
/// | Native    | 16-bit is `q15`; 24- and 32-bit are `q31`, 24 bits at its top (D29)             |
/// | Unused    | the byte of a 24-in-32 word that holds no sample bits is ignored when read; written |
/// |           | as the sign's extension (LSB-justified) or as zero (MSB-justified)              |
/// | Narrowing | `q31` to 24 bits once, half up (add 2^7, floor), saturated at 24 bits' limits, as |
/// |           | `convert` narrows (CAT-CONVERT-005); every other direction is exact             |
/// | Compiled  | one loop per format and direction, explicitly instantiated in `src/wire.cpp`; a |
/// |           | linked image keeps only the ones it calls (`-ffunction-sections`, `--gc-sections`) |
/// | Host      | any byte order: a sample is assembled from its bytes, never read as a word       |
/// | Rationale | D4, D8, D29, D47, PLAN.md 7.1 and 12 (written new)                               |
///
/// @req{CAT-WIRE-001} Every value of every format unpacks to the native value with the same
/// bits at the top and zeros below them, and packs back to the bytes it came from, in either
/// byte order: each 16-bit and 24-bit value, and 32-bit values at every bit position.
/// @req{CAT-WIRE-002} The bytes of a sample are in the format's order whatever the host's: the
/// value 0x123456 is the bytes 56 34 12 packed little-endian and 12 34 56 big-endian.
/// @req{CAT-WIRE-003} A 24-in-32 word's unused byte is ignored when it is read, and written as
/// the sign's extension at the most significant end of an LSB-justified word and as zero at the
/// least significant end of an MSB-justified one.
/// @req{CAT-WIRE-004} `q31` packs into a 24-bit format rounded once, half up, and saturated:
/// k x 2^8 + 2^7 gives k + 1 and k x 2^8 + 2^7 - 1 gives k, and `q31`'s maximum gives 24 bits'
/// maximum.
/// @req{CAT-WIRE-005} A wire format is 16-bit, 24-bit packed, 24 bits in 32 justified either
/// way, or 32-bit, in either byte order; any other does not compile, and `unpack` and `pack`
/// take nothing that is not a wire format.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_WIRE_H
#define CATENAE_WIRE_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/fwd.h"
#include "catenae/sample_format.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace wire {

            /// The order of a sample's bytes on the wire.
            enum class order : std::uint8_t {
                little, ///< least significant byte first
                big,    ///< most significant byte first
            };

            /// Where a sample narrower than its word sits in it.
            enum class justify : std::uint8_t {
                lsb, ///< at the least significant end; the bytes above it are the sign's extension
                msb, ///< at the most significant end; the bytes below it are zero
            };

            /// @brief A wire format: `Bits` of signed two's complement in `Bytes` bytes, in `Order`.
            template <std::size_t Bytes, std::size_t Bits, order Order, justify Justify = justify::lsb>
            struct format {
                static_assert((Bytes == 2 && Bits == 16) || (Bytes == 3 && Bits == 24) || (Bytes == 4 && Bits == 24)
                                  || (Bytes == 4 && Bits == 32),
                              "catenae: a wire format is 16-bit, 24-bit packed, 24 bits in 32, or 32-bit (D8)");
                static_assert(Justify == justify::lsb || Bytes * 8 > Bits,
                              "catenae: only a sample narrower than its word is justified");

                static constexpr std::size_t bytes_per_sample = Bytes;   ///< bytes a sample takes
                static constexpr std::size_t bits             = Bits;    ///< bits of the sample
                static constexpr order       byte_order       = Order;   ///< the order of its bytes
                static constexpr justify     justification    = Justify; ///< where a narrow sample sits
                /// The format it unpacks to and packs from (D29).
                using native = std::conditional_t<Bits == 16, q15, q31>;
            };

            using s16le = format<2, 16, order::little>; ///< 16-bit, little-endian
            using s16be = format<2, 16, order::big>;    ///< 16-bit, big-endian
            /// 24-bit in three bytes, little-endian.
            using s24le_packed = format<3, 24, order::little>;
            /// 24-bit in three bytes, big-endian.
            using s24be_packed = format<3, 24, order::big>;
            /// 24 bits at the least significant end of a 32-bit word, little-endian (ALSA's S24_LE).
            using s24le_in32_lsb = format<4, 24, order::little, justify::lsb>;
            /// 24 bits at the least significant end of a 32-bit word, big-endian.
            using s24be_in32_lsb = format<4, 24, order::big, justify::lsb>;
            /// 24 bits at the most significant end of a 32-bit word, little-endian.
            using s24le_in32_msb = format<4, 24, order::little, justify::msb>;
            /// 24 bits at the most significant end of a 32-bit word, big-endian.
            using s24be_in32_msb = format<4, 24, order::big, justify::msb>;
            using s32le          = format<4, 32, order::little>; ///< 32-bit, little-endian
            using s32be          = format<4, 32, order::big>;    ///< 32-bit, big-endian

            /// Whether a type is a wire format.
            template <typename T>
            struct is_format : std::false_type {};
            template <std::size_t Bytes, std::size_t Bits, order Order, justify Justify>
            struct is_format<format<Bytes, Bits, Order, Justify>> : std::true_type {};

        } // namespace wire

        namespace detail {

            /// The native value of a wire format's samples, as raw storage.
            template <typename Format>
            using wire_native_t = storage_t<typename Format::native>;

            /// `count` samples read from the wire into their native format. Compiled: one
            /// definition per format, in src/wire.cpp (CAT-WIRE-001 to 003).
            template <typename Format>
            void wire_unpack(const std::byte* in, wire_native_t<Format>* out, std::size_t count) noexcept;
            /// `count` native samples written to the wire (CAT-WIRE-001 to 004).
            template <typename Format>
            void wire_pack(const wire_native_t<Format>* in, std::byte* out, std::size_t count) noexcept;

            // The formats compiled in src/wire.cpp; nothing else is.
            extern template void wire_unpack<wire::s16le>(const std::byte*, std::int16_t*, std::size_t) noexcept;
            extern template void wire_unpack<wire::s16be>(const std::byte*, std::int16_t*, std::size_t) noexcept;
            extern template void wire_unpack<wire::s24le_packed>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            extern template void wire_unpack<wire::s24be_packed>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            extern template void wire_unpack<wire::s24le_in32_lsb>(const std::byte*, std::int32_t*,
                                                                   std::size_t) noexcept;
            extern template void wire_unpack<wire::s24be_in32_lsb>(const std::byte*, std::int32_t*,
                                                                   std::size_t) noexcept;
            extern template void wire_unpack<wire::s24le_in32_msb>(const std::byte*, std::int32_t*,
                                                                   std::size_t) noexcept;
            extern template void wire_unpack<wire::s24be_in32_msb>(const std::byte*, std::int32_t*,
                                                                   std::size_t) noexcept;
            extern template void wire_unpack<wire::s32le>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            extern template void wire_unpack<wire::s32be>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            extern template void wire_pack<wire::s16le>(const std::int16_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s16be>(const std::int16_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s24le_packed>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s24be_packed>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s24le_in32_lsb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s24be_in32_lsb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s24le_in32_msb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s24be_in32_msb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s32le>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            extern template void wire_pack<wire::s32be>(const std::int32_t*, std::byte*, std::size_t) noexcept;

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_WIRE_H
