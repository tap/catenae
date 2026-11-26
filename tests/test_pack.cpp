/// @file test_pack.cpp
/// @brief pack's contract (include/catenae/pack.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "catenae/chain.h"
#include "catenae/detail/shift.h"
#include "catenae/pack.h"
#include "catenae/unpack.h"
#include "catenae_test/check_trap.h"
#include "catenae_test/harness.h"

namespace {

    namespace wire = catenae::wire;

    std::vector<std::int32_t> noise_q31(std::size_t values) {
        std::vector<std::int32_t> out(values);
        std::uint32_t             x = 0x2545F491u;
        for (std::int32_t& v : out) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v = x <= 0x7FFFFFFFu ? static_cast<std::int32_t>(x) : -static_cast<std::int32_t>(~x) - 1;
        }
        return out;
    }

    // Verifies CAT-PACK-001.
    TEST(pack, EverySampleIsWrittenInItsFormat) {
        using p_t = catenae::pack<wire::s24be_in32_lsb, 2>;
        static_assert(std::is_same<p_t::input_port, catenae::port<catenae::q31, 2>>::value, "q31 in (D29)");
        static_assert(std::is_same<p_t::output_port, catenae::wire_port<wire::s24be_in32_lsb, 2>>::value, "bytes out");
        constexpr p_t                   p{};
        p_t::state                      st{};
        p_t::workspace                  ws{};
        const std::vector<std::int32_t> in = noise_q31(41 * 2);
        std::vector<std::byte>          out(41 * 8);
        std::vector<std::byte>          owed(41 * 8);
        catenae::stage_traits<p_t>::reset(p, st);
        EXPECT_EQ(p.run(st, ws, catenae::span<const std::int32_t>(in), catenae::span<std::byte>(out)), 41u);
        catenae::detail::wire_pack<wire::s24be_in32_lsb>(in.data(), owed.data(), in.size());
        EXPECT_EQ(out, owed);
    }

    /// `in` packed by one chain and unpacked by another, the bytes between them: a wire port
    /// joins no two stages of one chain (D8).
    template <typename Format>
    std::vector<std::int32_t> through_the_wire(const std::vector<std::int32_t>& in) {
        constexpr auto out_chain = catenae::chain(catenae::pack<Format, 2>{});
        constexpr auto in_chain  = catenae::chain(catenae::unpack<Format, 2>{});
        auto           mo        = std::make_unique<catenae::memory_of<std::remove_cv_t<decltype(out_chain)>>>();
        auto           mi        = std::make_unique<catenae::memory_of<std::remove_cv_t<decltype(in_chain)>>>();
        out_chain.reset(mo->state);
        in_chain.reset(mi->state);
        std::vector<std::byte>    bytes(in.size() * Format::bytes_per_sample);
        std::vector<std::int32_t> back(in.size());
        EXPECT_EQ(out_chain.process(*mo, catenae::span<const std::int32_t>(in), catenae::span<std::byte>(bytes)),
                  in.size() / 2);
        EXPECT_EQ(in_chain.process(*mi, catenae::span<const std::byte>(bytes), catenae::span<std::int32_t>(back)),
                  in.size() / 2);
        return back;
    }

    // Verifies CAT-PACK-001: unpack after pack gives back each 32-bit value, and each q31 value
    // with zero below its top 24 bits.
    TEST(pack, UnpackAfterPackGivesTheValueBack) {
        const std::vector<std::int32_t> in = noise_q31(256 * 2);
        EXPECT_EQ(through_the_wire<wire::s32be>(in), in);
        // A value with zero below its top 24 bits comes back as it went.
        std::vector<std::int32_t> at24(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            at24[i] = catenae::detail::shift_left(catenae::detail::shift_right(in[i], 8), 8);
        }
        EXPECT_EQ(through_the_wire<wire::s24le_in32_msb>(at24), at24);
        EXPECT_EQ(through_the_wire<wire::s24be_packed>(at24), at24);
    }

    // Verifies CAT-PACK-001: the generic tests, in three formats.
    TEST(pack, PassesTheGenericTests) {
        const catenae::pack<wire::s24le_packed, 2>   packed{};
        const catenae::pack<wire::s16le, 1>          mono{};
        const catenae::pack<wire::s24le_in32_lsb, 3> words{};
        EXPECT_TRUE(catenae_test::reset_ignores_storage(packed));
        EXPECT_TRUE(catenae_test::counts_are_exact(packed));
        EXPECT_TRUE(catenae_test::arrival_numbers_hold(packed, true));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(packed));
        EXPECT_TRUE(catenae_test::counts_are_exact(mono));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(mono));
        EXPECT_TRUE(catenae_test::chunking_is_bit_exact(words));
    }

    // Verifies CAT-PACK-002: wire to wire, little-endian packed in, big-endian packed out, and
    // the caller's bytes checked to hold the output.
    TEST(pack, AChainEndsWithTheCallersBytes) {
        constexpr auto swap =
            catenae::chain(catenae::unpack<wire::s24le_packed, 1>{}) | catenae::pack<wire::s24be_packed, 1>{};
        using swap_t = std::remove_cv_t<decltype(swap)>;
        static_assert(std::is_same<swap_t::output_port, catenae::wire_port<wire::s24be_packed, 1>>::value, "bytes out");
        auto m = std::make_unique<catenae::memory_of<swap_t>>();
        swap.reset(m->state);
        const std::array<std::byte, 6> in = {std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
                                             std::byte{0x01}, std::byte{0x00}, std::byte{0x80}};
        std::array<std::byte, 6>       out{};
        EXPECT_EQ(swap.process(*m, in, out), 2u);
        EXPECT_EQ(out, (std::array<std::byte, 6>{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x80},
                                                 std::byte{0x00}, std::byte{0x01}}));
        std::array<std::byte, 6> room{};
        CATENAE_EXPECT_CHECK(catenae::check::output_too_small,
                             static_cast<void>(swap.process(*m, catenae::span<const std::byte>(in),
                                                            catenae::span<std::byte>(room.data(), 5))));
    }

} // namespace
