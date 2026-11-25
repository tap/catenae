/// @file version.h
/// @brief The version a translation unit was compiled against, and the one it linked.
///
/// | Contract  |                                                                          |
/// |-----------|--------------------------------------------------------------------------|
/// | Numbers   | `CATENAE_VERSION_MAJOR`, `_MINOR`, `_PATCH`: the build's own version     |
/// | Packed    | `CATENAE_VERSION`: major x 65536 + minor x 256 + patch, unsigned         |
/// | Compiled  | `catenae::version()`: the packed number the compiled core was built with |
/// | Rationale | D4 (the compiled core), D26 (the builds that state the version)          |
///
/// @req{CAT-VERSION-001} The three numbers are the version the build states: CMake's
/// `project()` and the `wscript`'s `VERSION`.
/// @req{CAT-VERSION-002} `CATENAE_VERSION` is `CATENAE_VERSION_MAJOR` x 65536 +
/// `CATENAE_VERSION_MINOR` x 256 + `CATENAE_VERSION_PATCH`, as an unsigned number.
/// @req{CAT-VERSION-003} `catenae::version()` returns the `CATENAE_VERSION` that the compiled
/// core was built with, so a translation unit can tell whether it was compiled against the
/// headers of the library it links.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_VERSION_H
#define CATENAE_VERSION_H

#include <cstdint>

/// @brief The major version: the build's own (CAT-VERSION-001).
#define CATENAE_VERSION_MAJOR 0
/// @brief The minor version (see `CATENAE_VERSION_MAJOR`).
#define CATENAE_VERSION_MINOR 0
/// @brief The patch version (see `CATENAE_VERSION_MAJOR`).
#define CATENAE_VERSION_PATCH 0
/// @brief The three numbers in one (see `CATENAE_VERSION_MAJOR`).
#define CATENAE_VERSION (CATENAE_VERSION_MAJOR * 65536u + CATENAE_VERSION_MINOR * 256u + CATENAE_VERSION_PATCH)

/// @brief Everything catenae declares.
namespace catenae {

    /// @brief The version of the compiled core this image linked.
    /// @return `CATENAE_VERSION` as it was when the compiled core was built.
    [[nodiscard]] std::uint32_t version() noexcept;

} // namespace catenae

#endif // CATENAE_VERSION_H
