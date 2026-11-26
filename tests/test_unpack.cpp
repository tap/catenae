/// @file test_unpack.cpp
/// @brief unpack's contract (include/catenae/unpack.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/convert.h"
#include "catenae/unpack.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

namespace {

    namespace wire = catenae::wire;

    /// `frames` stereo frames of bytes from a xorshift: any bytes are valid samples.
    std::vector<std::byte> noise_bytes(std::size_t bytes) {
        std::vector<std::byte> out(bytes);
        std::uint32_t          x = 0x9E3779B9u;
        for (std::byte& v : out) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v = static_cast<std::byte>(x >> 24);
        }
        return out;
    }

    // Verifies CAT-UNPACK-001.
    TEST(unpack, EverySampleIsItsFormatsValue) {
        using u_t = catenae::unpack<wire::s24le_packed, 2>;
        static_assert(std::is_same<u_t::input_port, catenae::wire_port<wire::s24le_packed, 2>>::value, "bytes in");
        static_assert(std::is_same<u_t::output_port, catenae::port<catenae::q31, 2>>::value, "q31 out (D29)");
        static_assert(std::is_same<catenae::unpack<wire::s16be, 1>::output_port, catenae::port<catenae::q15, 1>>::value,
                      "16-bit to q15");
        constexpr u_t                u{};
        u_t::state                   st{};
        u_t::workspace               ws{};
        const std::vector<std::byte> in = noise_bytes(37 * 6);
        std::vector<std::int32_t>    out(37 * 2);
        std::vector<std::int32_t>    owed(37 * 2);
        catenae::stage_traits<u_t>::reset(u, st);
        EXPECT_EQ(u.run(st, ws, catenae::span<const std::byte>(in), catenae::span<std::int32_t>(out)), 37u);
        catenae::detail::wire_unpack<wire::s24le_packed>(in.data(), owed.data(), owed.size());
        EXPECT_EQ(out, owed);
        // Channel 1 of frame 2 is bytes 15, 16 and 17, least significant first.
        const std::uint32_t bits = std::to_integer<std::uint32_t>(in[15])
                                   | (std::to_integer<std::uint32_t>(in[16]) << 8)
                                   | (std::to_integer<std::uint32_t>(in[17]) << 16);
        EXPECT_EQ(static_cast<std::uint32_t>(out[5]), bits << 8);
    }

    // Verifies CAT-UNPACK-001: the generic tests, in three formats.
    TEST(unpack, PassesTheGenericTests) {
        const catenae::unpack<wire::s24le_packed, 2>   packed{};
        const catenae::unpack<wire::s16be, 1>          mono{};
        const catenae::unpack<wire::s24be_in32_msb, 3> words{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(packed));
        EXPECT_TRUE(catenae_test::counts_are_exact(packed));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(packed, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(packed));
        EXPECT_TRUE(catenae_test::counts_are_exact(mono));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(mono));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(words));
    }

    // Verifies CAT-UNPACK-002.
    TEST(unpack, AChainBeginsWithTheCallersBytes) {
        constexpr auto chain =
            catenae::chain(catenae::unpack<wire::s16le, 2>{}) | catenae::convert<catenae::q15, float, 2>{};
        using chain_t = std::remove_cv_t<decltype(chain)>;
        static_assert(std::is_same<chain_t::input_port, catenae::wire_port<wire::s16le, 2>>::value,
                      "its input is bytes");
        auto m = std::make_unique<catenae::memory_of<chain_t>>();
        chain.reset(m->state);
        // 0x4000 and 0xC000: +1/2 and -1/2, little-endian.
        const std::array<std::byte, 8> in = {std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0xC0},
                                             std::byte{0x01}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}};
        std::array<float, 4>           out{};
        EXPECT_EQ(chain.process(*m, in, out), 2u);
        EXPECT_EQ(out[0], 0.5f);
        EXPECT_EQ(out[1], -0.5f);
        EXPECT_EQ(out[2], 1.0f / 32768.0f);
        EXPECT_EQ(out[3], -1.0f / 32768.0f);
        // A frame is four bytes: three is not whole frames, in every build.
        std::array<float, 4> more{};
        CATENAE_EXPECT_CHECK(catenae::check::input_not_whole_frames,
                             static_cast<void>(chain.process(*m, catenae::span<const std::byte>(in.data(), 3),
                                                             catenae::span<float>(more))));
    }

} // namespace
