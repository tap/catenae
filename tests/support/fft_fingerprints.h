/// @file fft_fingerprints.h
/// @brief The floating FFT's output pins (tests/test_fft.cpp, CAT-FFT-003).
// Copyright 2026 Timothy Place and the catenae contributors
//
// For each format and every power of two N = 4 to 2^20, FNV-1a-64 over the bits of every value
// of a forward transform of a fixed input (N/2 + 1 bins), then of the inverse of that spectrum
// (N/2 entries). One row for every compiler and target CI runs, at contraction off: the tables
// are integer arithmetic and the statements IEEE operations in a fixed order. A pin that moves
// is a numeric change to the engine; a host that needs a second row is a finding.
#ifndef CATENAE_TESTS_SUPPORT_FFT_FINGERPRINTS_H
#define CATENAE_TESTS_SUPPORT_FFT_FINGERPRINTS_H

#include <cstdint>

namespace catenae_test {
    namespace fft_fingerprints {

        /// The xorshift32 seed of the fixed input.
        constexpr std::uint32_t seed = 0x2545f491u;

        /// N = 4, 8, ..., 2^20.
        constexpr std::uint64_t float_pins[19] = {
            0x7a3aacb200d106ecu, 0xdd053a43fdd3fc1cu, 0xcf840ce7bcf87edcu, 0x6a18c265753013a0u, 0xce9bc07c0555f845u,
            0x2c3e85a29d692dd7u, 0x5352c78a04f91726u, 0x4b07b4b375a79234u, 0x8c94f9fafc00a1a7u, 0xc31a06a6c29e50b3u,
            0x494c6f227c4f60feu, 0x1ed054767542f7e2u, 0xe982bc53be5c0999u, 0xaa97c47394cf1388u, 0xc6f6e6ea5beb325cu,
            0xa32432cb5eba41a2u, 0x93abd08b2819a24cu, 0x3673a60db345585fu, 0xe368e5cafd38b6d6u,
        };
        constexpr std::uint64_t double_pins[19] = {
            0x7ed46a85c0f6ef8du, 0x490ce8f6636bddb1u, 0x59267cf05ae40b44u, 0x01b0f7f4f7812600u, 0x471d72af652e3bfbu,
            0xba357470eab6966eu, 0x3939b63b797b7db7u, 0x4b10d3828c50f5d8u, 0x6b3dd80d9112e172u, 0xea54f9afc6b4d75du,
            0x7b933f1a08b6e0f5u, 0xa95666be149447c4u, 0x1abd54469c1d6986u, 0xdcc372481515669bu, 0xb63d86d9526d60c4u,
            0x41a3820844ca8af1u, 0xe908bb6dfc75738du, 0x9fbb1761ab990df9u, 0xa0498cee03fcc8a6u,
        };

    } // namespace fft_fingerprints
} // namespace catenae_test

#endif // CATENAE_TESTS_SUPPORT_FFT_FINGERPRINTS_H
