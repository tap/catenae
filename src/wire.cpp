/// @file wire.cpp
/// @brief The wire formats' bulk loops (include/catenae/wire.h), compiled once per format and
/// direction (PLAN.md 7.1).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Written new (PLAN.md 12). A sample is assembled from its bytes in the format's order with
// unsigned arithmetic, so the host's byte order never shows, and becomes a signed value through
// one mapping of two's complement bits that every C++ standard defines. The only rounding is
// q31 to 24 bits, half up and saturated, as convert narrows.
#include "catenae/wire.h"

#include <cstddef>
#include <cstdint>

#include "catenae/detail/shift.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            namespace {

                /// The bits of a 32-bit two's complement word as its value, with no conversion
                /// C++17 leaves to the implementation.
                constexpr std::int32_t value_of32(std::uint32_t bits) noexcept {
                    return bits <= 0x7FFFFFFFu ? static_cast<std::int32_t>(bits)
                                               : -static_cast<std::int32_t>(~bits) - 1;
                }
                /// The low 16 bits of `bits` as a 16-bit two's complement value.
                constexpr std::int16_t value_of16(std::uint32_t bits) noexcept {
                    const std::uint32_t low = bits & 0xFFFFu;
                    return static_cast<std::int16_t>(low <= 0x7FFFu ? static_cast<std::int32_t>(low)
                                                                    : -static_cast<std::int32_t>(low ^ 0xFFFFu) - 1);
                }
                static_assert(value_of32(0x80000000u) == -2147483647 - 1 && value_of32(0xFFFFFFFFu) == -1,
                              "two's complement, 32 bits");
                static_assert(value_of16(0x8000u) == -32768 && value_of16(0xFFFFu) == -1
                                  && value_of16(0x7FFFu) == 32767,
                              "two's complement, 16 bits");

                /// A q31 value rounded half up to its top 24 bits and saturated there.
                constexpr std::int32_t round_to_24(std::int32_t x) noexcept {
                    // At and above this, x + 2^7 reaches 2^31: 24 bits' maximum.
                    constexpr std::int32_t top = 0x7FFFFF80;
                    return x >= top ? 0x7FFFFF : shift_right(x + 0x80, 8);
                }
                static_assert(round_to_24(0x7FFFFFFF) == 0x7FFFFF && round_to_24(-2147483647 - 1) == -0x800000,
                              "saturated at both ends");
                static_assert(round_to_24(0x180) == 2 && round_to_24(0x17F) == 1 && round_to_24(-0x80) == 0
                                  && round_to_24(-0x81) == -1,
                              "half up");

                /// The low `Bytes` bytes of `w` in the reverse order, as one word. Written with
                /// shifts and masks, which compilers turn into a byte-reversing instruction, so a
                /// big-endian sample costs a swap and not a shuffle of its own (GCC 13 vectorised a
                /// big-endian store loop into a 792-byte frame at -O3).
                template <std::size_t Bytes>
                constexpr std::uint32_t swapped(std::uint32_t w) noexcept {
                    if constexpr (Bytes == 2) {
                        return ((w & 0xFFu) << 8) | ((w >> 8) & 0xFFu);
                    }
                    else if constexpr (Bytes == 3) {
                        return ((w & 0xFFu) << 16) | (w & 0xFF00u) | ((w >> 16) & 0xFFu);
                    }
                    else {
                        return ((w & 0xFFu) << 24) | ((w & 0xFF00u) << 8) | ((w >> 8) & 0xFF00u) | (w >> 24);
                    }
                }
                static_assert(swapped<2>(0x1234u) == 0x3412u && swapped<3>(0x123456u) == 0x563412u
                                  && swapped<4>(0x12345678u) == 0x78563412u,
                              "byte reversal");

                /// The `Bytes` bytes at `p` as one unsigned word, in the order `Order`.
                template <std::size_t Bytes, wire::order Order>
                std::uint32_t read_word(const std::byte* p) noexcept {
                    std::uint32_t word = 0;
                    for (std::size_t k = 0; k < Bytes; ++k) {
                        word |= std::to_integer<std::uint32_t>(p[k]) << (8 * k);
                    }
                    return Order == wire::order::little ? word : swapped<Bytes>(word);
                }
                /// The low `Bytes` bytes of `word` at `p`, in the order `Order`.
                template <std::size_t Bytes, wire::order Order>
                void write_word(std::uint32_t word, std::byte* p) noexcept {
                    const std::uint32_t le = Order == wire::order::little ? word : swapped<Bytes>(word);
                    for (std::size_t k = 0; k < Bytes; ++k) {
                        p[k] = static_cast<std::byte>((le >> (8 * k)) & 0xFFu);
                    }
                }

            } // namespace

            template <typename Format>
            void wire_unpack(const std::byte* in, wire_native_t<Format>* out, std::size_t count) noexcept {
                constexpr std::size_t bytes    = Format::bytes_per_sample;
                constexpr wire::order in_order = Format::byte_order;
                for (std::size_t i = 0; i < count; ++i) {
                    const std::uint32_t word = read_word<bytes, in_order>(in + i * bytes);
                    if constexpr (Format::bits == 16) {
                        out[i] = value_of16(word);
                    }
                    else if constexpr (Format::bits == 32) {
                        out[i] = value_of32(word);
                    }
                    else if constexpr (bytes == 3 || Format::justification == wire::justify::lsb) {
                        out[i] = value_of32((word & 0x00FFFFFFu) << 8); // the top byte of a word is ignored
                    }
                    else {
                        out[i] = value_of32(word & 0xFFFFFF00u); // the low byte is ignored
                    }
                }
            }

            template <typename Format>
            void wire_pack(const wire_native_t<Format>* in, std::byte* out, std::size_t count) noexcept {
                constexpr std::size_t bytes     = Format::bytes_per_sample;
                constexpr wire::order out_order = Format::byte_order;
                for (std::size_t i = 0; i < count; ++i) {
                    std::uint32_t word = 0;
                    if constexpr (Format::bits == 16) {
                        word = static_cast<std::uint16_t>(in[i]); // modular: the value's two's complement bits
                    }
                    else if constexpr (Format::bits == 32) {
                        word = static_cast<std::uint32_t>(in[i]);
                    }
                    else if constexpr (Format::justification == wire::justify::msb) {
                        // The rounded value at the top and zero in the low byte: x + 2^7 with its
                        // low byte cleared is round_to_24(x) x 2^8, below the saturation point.
                        word = in[i] >= 0x7FFFFF80 ? 0x7FFFFF00u
                                                   : (static_cast<std::uint32_t>(in[i]) + 0x80u) & 0xFFFFFF00u;
                    }
                    else {
                        // Sign-extended to 32 bits; a packed format writes the low three bytes of it.
                        word = static_cast<std::uint32_t>(round_to_24(in[i]));
                    }
                    write_word<bytes, out_order>(word, out + i * bytes);
                }
            }

            // The formats wire.h declares, and nothing else.
            template void wire_unpack<wire::s16le>(const std::byte*, std::int16_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s16be>(const std::byte*, std::int16_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s24le_packed>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s24be_packed>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s24le_in32_lsb>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s24be_in32_lsb>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s24le_in32_msb>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s24be_in32_msb>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s32le>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_unpack<wire::s32be>(const std::byte*, std::int32_t*, std::size_t) noexcept;
            template void wire_pack<wire::s16le>(const std::int16_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s16be>(const std::int16_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s24le_packed>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s24be_packed>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s24le_in32_lsb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s24be_in32_lsb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s24le_in32_msb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s24be_in32_msb>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s32le>(const std::int32_t*, std::byte*, std::size_t) noexcept;
            template void wire_pack<wire::s32be>(const std::int32_t*, std::byte*, std::size_t) noexcept;

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
