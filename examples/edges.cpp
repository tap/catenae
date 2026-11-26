/// @file edges.cpp
/// @brief Bytes off a codec's wire, through float, and onto another wire; and a host's floats,
/// made safe, mixed to mono and dithered onto a 16-bit wire (guide: The edges).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/convert.h"
#include "catenae/dither.h"
#include "catenae/gain.h"
#include "catenae/mix.h"
#include "catenae/pack.h"
#include "catenae/sanitize.h"
#include "catenae/unpack.h"

namespace {

    // ANCHOR: path
    namespace wire = catenae::wire;

    // In: a codec's DMA buffer, 24-bit stereo at the top of 32-bit little-endian words.
    // Out: 24-bit stereo packed in three bytes, little-endian, as a file or a USB stream has it.
    constexpr auto path = catenae::chain(catenae::unpack<wire::s24le_in32_msb, 2>{}) // bytes -> q31
                          | catenae::convert<catenae::q31, float, 2>{}               // q31 -> float
                          | catenae::gain<2>{0.5f, 0}                                // the work, in float
                          | catenae::convert<float, catenae::q31, 2>{}               // float -> q31
                          | catenae::pack<wire::s24le_packed, 2>{};                  // q31 -> bytes
    using path_t = std::remove_cv_t<decltype(path)>;

    CATENAE_CONSTINIT catenae::memory_of<path_t> s_path{};

    // 64 frames of two 4-byte words in, 64 frames of two 3-byte samples out: the compiler
    // checks both sizes against the chain's ports.
    void audio_callback(const std::array<std::byte, 64 * 2 * 4>& in, std::array<std::byte, 64 * 2 * 3>& out) noexcept {
        static_cast<void>(path.process(s_path, in, out));
    }
    // ANCHOR_END: path

    // ANCHOR: host
    // In: a host's float stereo, which this code cannot vouch for. Out: 16-bit mono on the wire.
    constexpr auto to_sixteen = catenae::chain(catenae::sanitize<2>{})       // NaN, infinity, subnormal -> 0
                                | catenae::mix<2, 1>{{0.5f, 0.5f}, 64}       // stereo -> mono, ramped if changed
                                | catenae::dither<1, 16>{}                   // TPDF of one LSB at 16 bits
                                | catenae::convert<float, catenae::q15, 1>{} // the one rounding, to 16 bits
                                | catenae::pack<wire::s16le, 1>{};           // q15 -> bytes
    using to_sixteen_t = std::remove_cv_t<decltype(to_sixteen)>;

    CATENAE_CONSTINIT catenae::memory_of<to_sixteen_t> s_to_sixteen{};

    void host_callback(const std::array<float, 64 * 2>& in, std::array<std::byte, 64 * 2>& out) noexcept {
        static_cast<void>(to_sixteen.process(s_to_sixteen, in, out));
    }
    // ANCHOR_END: host

    void print_bytes(const char* label, const std::byte* bytes, std::size_t n) {
        std::printf("%s", label);
        for (std::size_t i = 0; i < n; ++i) {
            std::printf(" %02x", static_cast<unsigned>(bytes[i]));
        }
        std::printf("\n");
    }

} // namespace

int main() {
    path.reset(s_path.state);
    std::array<std::byte, 64 * 2 * 4> in{};
    std::array<std::byte, 64 * 2 * 3> out{};
    // Every frame: left +0.5 (0x400000 at the top of its word, the unused low byte holding
    // 0x5a, which unpack ignores), right -0.5 (0xc00000).
    for (std::size_t frame = 0; frame < 64; ++frame) {
        const std::array<std::byte, 8> words = {std::byte{0x5a}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
                                                std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc0}};
        for (std::size_t i = 0; i < words.size(); ++i) {
            in[frame * 8 + i] = words[i];
        }
    }
    audio_callback(in, out);
    print_bytes("in  frame 0:", in.data(), 8);
    print_bytes("out frame 0:", out.data(), 6);
    // Half of each: +0.25 is 0x200000, -0.25 is 0xe00000, least significant byte first.
    const std::array<std::byte, 6> owed = {std::byte{0x00}, std::byte{0x00}, std::byte{0x20},
                                           std::byte{0x00}, std::byte{0x00}, std::byte{0xe0}};
    bool                           ok   = true;
    for (std::size_t frame = 0; frame < 64; ++frame) {
        for (std::size_t i = 0; i < owed.size(); ++i) {
            ok = ok && out[frame * 6 + i] == owed[i];
        }
    }

    // The host's path: every frame (0.5, 0.25), but frame 1's left channel is a NaN.
    to_sixteen.reset(s_to_sixteen.state);
    std::array<float, 64 * 2> host{};
    for (std::size_t frame = 0; frame < 64; ++frame) {
        host[2 * frame]     = 0.5f;
        host[2 * frame + 1] = 0.25f;
    }
    host[2] = std::numeric_limits<float>::quiet_NaN();
    std::array<std::byte, 64 * 2> sixteen{};
    host_callback(host, sixteen);
    std::printf("16-bit out, frames 0 to 3:");
    for (std::size_t frame = 0; frame < 4; ++frame) {
        const auto low  = static_cast<unsigned>(sixteen[2 * frame]);
        const auto high = static_cast<unsigned>(sixteen[2 * frame + 1]);
        std::printf(" %u", low | (high << 8));
    }
    std::printf("\n");
    // 0.375 is 12288 at 16 bits, and the NaN frame's 0.125 is 4096: each within one LSB, the
    // dither's reach.
    for (std::size_t frame = 0; frame < 64; ++frame) {
        const auto value  = static_cast<long>(static_cast<unsigned>(sixteen[2 * frame])
                                              | (static_cast<unsigned>(sixteen[2 * frame + 1]) << 8));
        const long target = frame == 1 ? 4096 : 12288;
        ok                = ok && value >= target - 1 && value <= target + 1;
    }
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
