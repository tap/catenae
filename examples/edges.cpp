/// @file edges.cpp
/// @brief Bytes off a codec's wire, through float, and onto another wire (guide: The edges).
// Copyright 2026 Timothy Place and the catenae contributors
#include <array>
#include <cstddef>
#include <cstdio>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/compiler.h"
#include "catenae/convert.h"
#include "catenae/gain.h"
#include "catenae/pack.h"
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
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
