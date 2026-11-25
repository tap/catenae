/// @file sample_format.h
/// @brief Sample formats: `float`, `double`, and `fixed<Storage, F>` tags over raw integers (D29).
///
/// | Contract  |                                                                                    |
/// |-----------|------------------------------------------------------------------------------------|
/// | Formats   | `float`, `double`, `fixed<std::int16_t, F>`, `fixed<std::int32_t, F>`              |
/// | Tags      | `q15` = fixed<int16_t, 15>, `q31` = fixed<int32_t, 31>, `q4_27` = fixed<int32_t, 27> |
/// | Storage   | a fixed<> port's value type is its storage; the tag never reaches a kernel        |
/// | Lookup    | `format_traits<S>`: storage, kernel traits, fraction and integer bits             |
/// | Not       | a raw `std::int16_t` or `std::int32_t` is not a sample format                     |
/// | Rationale | D24, D29, D36                                                                      |
///
/// @req{CAT-FORMAT-001} `float`, `double` and `fixed<S, F>` over `std::int16_t` (F 0 to 15) or
/// `std::int32_t` (F 0 to 31) are sample formats; nothing else is, raw integers included.
/// @req{CAT-FORMAT-002} A `fixed<S, F>` format's storage is `S`, its kernel traits are
/// `sample_traits<S>` whatever `F` is, and it has F fraction bits and width - 1 - F integer bits.
/// @req{CAT-FORMAT-003} `complex<T>` is a plain aggregate `{T re; T im;}`: standard layout,
/// trivially copyable, two `T` with no padding between.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_SAMPLE_FORMAT_H
#define CATENAE_SAMPLE_FORMAT_H

#include <cstdint>
#include <limits>
#include <type_traits>

#include "catenae/detail/abi.h"
#include "catenae/fwd.h"
#include "catenae/sample_traits.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// A fixed-point format: signed two's complement in `Storage` with `F` fraction bits.
        /// A tag only: no value of it exists, and a port carries its storage.
        template <typename Storage, int F>
        struct fixed {
            static_assert(std::is_same<Storage, std::int16_t>::value || std::is_same<Storage, std::int32_t>::value,
                          "catenae: a fixed-point format is stored in std::int16_t or std::int32_t");
            static_assert(F >= 0 && F < std::numeric_limits<Storage>::digits + 1,
                          "catenae: a fixed-point format has from 0 to its width less one fraction bits");
            using storage = Storage; ///< the raw integer a port of this format carries
            /// Fraction bits.
            static constexpr int fraction_bits = F;
        };

        using q15   = fixed<std::int16_t, 15>; ///< Q0.15 in 16 bits
        using q31   = fixed<std::int32_t, 31>; ///< Q0.31 in 32 bits
        using q4_27 = fixed<std::int32_t, 27>; ///< Q4.27: a sign, 4 integer and 27 fraction bits

        /// Whether a type is a sample format.
        template <typename S>
        struct is_sample_format : std::false_type {};
        template <>
        struct is_sample_format<float> : std::true_type {};
        template <>
        struct is_sample_format<double> : std::true_type {};
        template <int F>
        struct is_sample_format<fixed<std::int16_t, F>> : std::bool_constant<F >= 0 && F <= 15> {};
        template <int F>
        struct is_sample_format<fixed<std::int32_t, F>> : std::bool_constant<F >= 0 && F <= 31> {};

        /// What a sample format is made of: its storage, its kernel's traits, its binary point.
        template <typename S>
        struct format_traits;
        /// float: its own storage and kernel.
        template <>
        struct format_traits<float> {
            using storage                        = float;                ///< what a port carries
            using kernel                         = sample_traits<float>; ///< the kernel's arithmetic
            static constexpr bool is_fixed_point = false;                ///< whether the storage is an integer
            static constexpr int  fraction_bits  = 0;                    ///< none: a floating format
            static constexpr int  integer_bits   = 0;                    ///< none: a floating format
        };
        /// double, the golden model: its own storage and kernel.
        template <>
        struct format_traits<double> {
            using storage                        = double;                ///< what a port carries
            using kernel                         = sample_traits<double>; ///< the kernel's arithmetic
            static constexpr bool is_fixed_point = false;                 ///< whether the storage is an integer
            static constexpr int  fraction_bits  = 0;                     ///< none: a floating format
            static constexpr int  integer_bits   = 0;                     ///< none: a floating format
        };
        /// A fixed-point format: its storage's kernel, whatever its fraction bits.
        template <typename Storage, int F>
        struct format_traits<fixed<Storage, F>> {
            using storage                        = Storage;                ///< what a port carries
            using kernel                         = sample_traits<Storage>; ///< the same for every F
            static constexpr bool is_fixed_point = true;                   ///< whether the storage is an integer
            static constexpr int  fraction_bits  = F;                      ///< bits after the binary point
            /// Bits before it, the sign aside.
            static constexpr int integer_bits = std::numeric_limits<Storage>::digits - F;
        };

        /// The value type of a port of format S.
        template <typename S>
        using storage_t = typename format_traits<S>::storage;

        /// A complex value: a transform coefficient of a spectrum (PLAN.md section 5.2).
        template <typename T>
        struct complex {
            T re; ///< the real part
            T im; ///< the imaginary part
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SAMPLE_FORMAT_H
