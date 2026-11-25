/// @file version.cpp
/// @brief The compiled core's version: the first compiled unit.
// Copyright 2026 Timothy Place and the catenae contributors
#include "catenae/version.h"

#include <cstdint>

namespace catenae {

    std::uint32_t version() noexcept {
        return CATENAE_VERSION;
    }

} // namespace catenae
