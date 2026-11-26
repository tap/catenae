/// @file test_wire.cpp
/// @brief The wire formats' contract (include/catenae/wire.h, src/wire.cpp).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/detail/shift.h"
#include "catenae/wire.h"

namespace {

    namespace wire = catenae::wire;
    using catenae::detail::wire_pack;
    using catenae::detail::wire_unpack;

    /// Every 24-bit value on a hosted build; one in 4099 under emulation, where the sweep would
    /// cost minutes (the ends and both sides of zero are always in).
#if defined(CATENAE_TEST_EMULATED)
    constexpr std::int32_t k_stride = 4099;
#else
    constexpr std::int32_t k_stride = 1;
#endif

    std::byte b(std::uint32_t v) {
        return static_cast<std::byte>(v & 0xFFu);
    }

    /// The 24-bit values the sweep visits, as q31: v x 2^8.
    std::vector<std::int32_t> q31_of_24_bit_values() {
        std::vector<std::int32_t> values;
        for (std::int32_t v = -0x800000; v <= 0x7FFFFF; v += k_stride) {
            values.push_back(catenae::detail::shift_left(v, 8));
        }
        for (const std::int32_t v : {-0x800000, -1, 0, 1, 0x7FFFFF}) {
            values.push_back(catenae::detail::shift_left(v, 8));
        }
        return values;
    }

    /// Packs `values` in Format, checks each sample's bytes with `bytes_of`, unpacks them, and
    /// expects the values back.
    template <typename Format, typename BytesOf>
    ::testing::AssertionResult round_trip(const std::vector<std::int32_t>& values, BytesOf bytes_of) {
        constexpr std::size_t  n = Format::bytes_per_sample;
        std::vector<std::byte> packed(values.size() * n);
        wire_pack<Format>(values.data(), packed.data(), values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::array<std::byte, 4> want = bytes_of(values[i]);
            for (std::size_t k = 0; k < n; ++k) {
                if (packed[i * n + k] != want[k]) {
                    return ::testing::AssertionFailure() << "value " << values[i] << ", byte " << k;
                }
            }
        }
        std::vector<std::int32_t> back(values.size());
        wire_unpack<Format>(packed.data(), back.data(), values.size());
        if (back != values) {
            return ::testing::AssertionFailure() << "a value did not unpack to itself";
        }
        return ::testing::AssertionSuccess();
    }

    /// The bytes of a 32-bit word, least significant first.
    std::array<std::byte, 4> le(std::uint32_t w) {
        return {b(w & 0xFFu), b((w >> 8) & 0xFFu), b((w >> 16) & 0xFFu), b(w >> 24)};
    }
    /// The low `n` bytes of a word, most significant first.
    std::array<std::byte, 4> be(std::uint32_t w, std::size_t n) {
        std::array<std::byte, 4> out{};
        for (std::size_t k = 0; k < n; ++k) {
            out[k] = b((w >> (8 * (n - 1 - k))) & 0xFFu);
        }
        return out;
    }

    // Verifies CAT-WIRE-001.
    TEST(wire, EverySixteenBitValueRoundTrips) {
        std::vector<std::int16_t> values;
        for (std::int32_t v = -32768; v <= 32767; ++v) {
            values.push_back(static_cast<std::int16_t>(v));
        }
        std::vector<std::byte>    little(values.size() * 2);
        std::vector<std::byte>    big(values.size() * 2);
        std::vector<std::int16_t> back(values.size());
        wire_pack<wire::s16le>(values.data(), little.data(), values.size());
        wire_pack<wire::s16be>(values.data(), big.data(), values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::uint32_t bits = static_cast<std::uint16_t>(values[i]);
            ASSERT_EQ(little[2 * i], b(bits & 0xFFu)) << values[i];
            ASSERT_EQ(little[2 * i + 1], b(bits >> 8)) << values[i];
            ASSERT_EQ(big[2 * i], b(bits >> 8)) << values[i];
            ASSERT_EQ(big[2 * i + 1], b(bits & 0xFFu)) << values[i];
        }
        wire_unpack<wire::s16le>(little.data(), back.data(), values.size());
        EXPECT_EQ(back, values);
        wire_unpack<wire::s16be>(big.data(), back.data(), values.size());
        EXPECT_EQ(back, values);
    }

    // Verifies CAT-WIRE-001: each 24-bit value, at the top of q31, in all six 24-bit formats.
    TEST(wire, EveryTwentyFourBitValueRoundTrips) {
        const std::vector<std::int32_t> values = q31_of_24_bit_values();
        const auto top24 = [](std::int32_t q) { return (static_cast<std::uint32_t>(q) >> 8) & 0xFFFFFFu; };
        const auto sext  = [&top24](std::int32_t q) { return top24(q) | (q < 0 ? 0xFF000000u : 0u); };
        EXPECT_TRUE(round_trip<wire::s24le_packed>(values, [&](std::int32_t q) { return le(top24(q)); }));
        EXPECT_TRUE(round_trip<wire::s24be_packed>(values, [&](std::int32_t q) { return be(top24(q), 3); }));
        EXPECT_TRUE(round_trip<wire::s24le_in32_lsb>(values, [&](std::int32_t q) { return le(sext(q)); }));
        EXPECT_TRUE(round_trip<wire::s24be_in32_lsb>(values, [&](std::int32_t q) { return be(sext(q), 4); }));
        EXPECT_TRUE(
            round_trip<wire::s24le_in32_msb>(values, [](std::int32_t q) { return le(static_cast<std::uint32_t>(q)); }));
        EXPECT_TRUE(round_trip<wire::s24be_in32_msb>(
            values, [](std::int32_t q) { return be(static_cast<std::uint32_t>(q), 4); }));
    }

    // Verifies CAT-WIRE-001: 32-bit values with each bit set alone, each bit clear alone, the
    // two ends, and a pseudo-random set.
    TEST(wire, ThirtyTwoBitValuesRoundTripAtEveryBit) {
        std::vector<std::int32_t> values = {0, -1, std::numeric_limits<std::int32_t>::min(),
                                            std::numeric_limits<std::int32_t>::max()};
        for (unsigned bit = 0; bit < 32; ++bit) {
            const std::uint32_t one = 1u << bit;
            for (const std::uint32_t w : {one, ~one}) {
                values.push_back(w <= 0x7FFFFFFFu ? static_cast<std::int32_t>(w) : -static_cast<std::int32_t>(~w) - 1);
            }
        }
        std::uint32_t x = 0x2545F491u;
        for (int i = 0; i < 4096; ++i) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            values.push_back(x <= 0x7FFFFFFFu ? static_cast<std::int32_t>(x) : -static_cast<std::int32_t>(~x) - 1);
        }
        EXPECT_TRUE(round_trip<wire::s32le>(values, [](std::int32_t q) { return le(static_cast<std::uint32_t>(q)); }));
        EXPECT_TRUE(
            round_trip<wire::s32be>(values, [](std::int32_t q) { return be(static_cast<std::uint32_t>(q), 4); }));
    }

    // Verifies CAT-WIRE-002.
    TEST(wire, TheBytesAreInTheFormatsOrderOnAnyHost) {
        const std::int32_t       q = 0x12345600; // 0x123456 at the top of q31
        std::array<std::byte, 4> out{};
        wire_pack<wire::s24le_packed>(&q, out.data(), 1);
        EXPECT_EQ(out[0], b(0x56));
        EXPECT_EQ(out[1], b(0x34));
        EXPECT_EQ(out[2], b(0x12));
        wire_pack<wire::s24be_packed>(&q, out.data(), 1);
        EXPECT_EQ(out[0], b(0x12));
        EXPECT_EQ(out[1], b(0x34));
        EXPECT_EQ(out[2], b(0x56));
        const std::int16_t h = 0x1234;
        wire_pack<wire::s16le>(&h, out.data(), 1);
        EXPECT_EQ(out[0], b(0x34));
        EXPECT_EQ(out[1], b(0x12));
        const std::int32_t w = 0x12345678;
        wire_pack<wire::s32be>(&w, out.data(), 1);
        EXPECT_EQ(out, (std::array<std::byte, 4>{b(0x12), b(0x34), b(0x56), b(0x78)}));
        wire_pack<wire::s32le>(&w, out.data(), 1);
        EXPECT_EQ(out, (std::array<std::byte, 4>{b(0x78), b(0x56), b(0x34), b(0x12)}));
    }

    // Verifies CAT-WIRE-003.
    TEST(wire, AWordsUnusedByteIsIgnoredAndWrittenPlainly) {
        // Read: whatever the unused byte holds, the sample is the other three.
        const std::array<std::byte, 4> lsb_le = {b(0x56), b(0x34), b(0x12), b(0xA5)}; // top byte garbage
        const std::array<std::byte, 4> lsb_be = {b(0x5A), b(0x92), b(0x34), b(0x56)}; // 0x923456: negative
        const std::array<std::byte, 4> msb_le = {b(0xC3), b(0x56), b(0x34), b(0x12)}; // low byte garbage
        const std::array<std::byte, 4> msb_be = {b(0x92), b(0x34), b(0x56), b(0x3C)};
        std::int32_t                   q      = 0;
        wire_unpack<wire::s24le_in32_lsb>(lsb_le.data(), &q, 1);
        EXPECT_EQ(q, 0x12345600);
        wire_unpack<wire::s24be_in32_lsb>(lsb_be.data(), &q, 1);
        EXPECT_EQ(q, -0x6DCBAA00); // 0x923456 as 24-bit two's complement, at the top
        wire_unpack<wire::s24le_in32_msb>(msb_le.data(), &q, 1);
        EXPECT_EQ(q, 0x12345600);
        wire_unpack<wire::s24be_in32_msb>(msb_be.data(), &q, 1);
        EXPECT_EQ(q, -0x6DCBAA00);
        // Written: the sign's extension above an LSB-justified sample, zero below an MSB-justified one.
        const std::int32_t       negative = -0x6DCBAA00;
        std::array<std::byte, 4> out{};
        wire_pack<wire::s24le_in32_lsb>(&negative, out.data(), 1);
        EXPECT_EQ(out, (std::array<std::byte, 4>{b(0x56), b(0x34), b(0x92), b(0xFF)}));
        wire_pack<wire::s24be_in32_msb>(&negative, out.data(), 1);
        EXPECT_EQ(out, (std::array<std::byte, 4>{b(0x92), b(0x34), b(0x56), b(0x00)}));
        const std::int32_t positive = 0x12345600;
        wire_pack<wire::s24be_in32_lsb>(&positive, out.data(), 1);
        EXPECT_EQ(out, (std::array<std::byte, 4>{b(0x00), b(0x12), b(0x34), b(0x56)}));
    }

    // Verifies CAT-WIRE-004: half up at every LSB the sweep visits, and saturation at the top.
    TEST(wire, NarrowingToTwentyFourBitsRoundsHalfUpAndSaturates) {
        const auto packed = [](std::int32_t q) {
            std::array<std::byte, 4> out{};
            wire_pack<wire::s24le_in32_lsb>(&q, out.data(), 1);
            std::int32_t back = 0;
            wire_unpack<wire::s24le_in32_lsb>(out.data(), &back, 1);
            return catenae::detail::shift_right(back, 8); // the 24-bit value written
        };
        for (std::int32_t k = -0x800000; k < 0x7FFFFF; k += k_stride) {
            const std::int32_t at = catenae::detail::shift_left(k, 8);
            ASSERT_EQ(packed(at + 0x7F), k) << k;     // below half an LSB: down
            ASSERT_EQ(packed(at + 0x80), k + 1) << k; // half: up
            ASSERT_EQ(packed(at + 0xFF), k + 1) << k;
        }
        EXPECT_EQ(packed(0x7FFFFF7F), 0x7FFFFF);
        EXPECT_EQ(packed(0x7FFFFF80), 0x7FFFFF); // would round to 2^23: saturated
        EXPECT_EQ(packed(std::numeric_limits<std::int32_t>::max()), 0x7FFFFF);
        EXPECT_EQ(packed(std::numeric_limits<std::int32_t>::min()), -0x800000);
        EXPECT_EQ(packed(-0x81), -1);
        EXPECT_EQ(packed(-0x80), 0);
        // Every 24-bit format narrows alike.
        const std::int32_t       q = 0x12345680;
        std::array<std::byte, 4> out{};
        wire_pack<wire::s24be_packed>(&q, out.data(), 1);
        EXPECT_EQ(out[2], b(0x57));
        wire_pack<wire::s24le_in32_msb>(&q, out.data(), 1);
        EXPECT_EQ(out, (std::array<std::byte, 4>{b(0x00), b(0x57), b(0x34), b(0x12)}));
    }

} // namespace
