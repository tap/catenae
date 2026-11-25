/// @file fwd.h
/// @brief How a consumer forward-declares a catenae type (PLAN.md section 3.4).
///
/// Every name of catenae is declared in an inline namespace that carries the build's
/// configuration (catenae/detail/abi.h), so a declaration written as `namespace catenae {
/// class span; }` would name another entity. Include this header instead. It is also the one
/// place that gives each template its default arguments; the defining headers include it.
///
/// @req{CAT-FWD-001} Every type this header declares is the type its defining header defines,
/// so a translation unit that includes only this header can name it, and one that includes both
/// sees one entity.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FWD_H
#define CATENAE_FWD_H

#include <cstddef>
#include <cstdint>

#include "catenae/detail/abi.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// The extent of a span whose size is known only at run time.
        inline constexpr std::size_t dynamic_extent = SIZE_MAX;
        template <typename T, std::size_t Extent = dynamic_extent>
        class span;

        enum class check : std::uint8_t;
        enum class [[nodiscard]] status : std::uint8_t;
        template <typename E>
        class unexpected;
        template <typename T, typename E>
        class expected;

        struct ratio;
        template <typename Storage, int F>
        struct fixed;
        template <typename T>
        struct complex;

        /// How an audio port's channels lie in memory (D5, D7).
        enum class layout : std::uint8_t {
            interleaved, ///< one frame after another, each Ch samples
            planar,      ///< Ch spans, one per channel
        };
        enum class signal_kind : std::uint8_t;
        template <typename S, std::size_t Ch, layout L = layout::interleaved>
        struct port;
        template <typename S, std::size_t Ch, std::size_t Bins>
        struct spectrum;
        template <typename Format, std::size_t Ch>
        struct wire_port;

        struct no_workspace;
        struct stage_latency;
        template <typename T>
        struct is_stage;
        template <typename T>
        struct is_chain;
        template <typename T>
        struct stage_traits;

        enum class clocks : std::uint8_t;
        struct junction_latency;
        template <typename T>
        struct is_junction;
        template <typename T>
        struct junction_traits;

        template <typename T>
        struct is_analyzer;
        template <typename T>
        struct analyzer_traits;

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_FWD_H
