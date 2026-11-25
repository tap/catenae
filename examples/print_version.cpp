/// @file print_version.cpp
/// @brief Which catenae a program was compiled against, and which it linked.
// Copyright 2026 Timothy Place and the catenae contributors
#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "catenae/version.h"

int main() {
    // ANCHOR: check
    const std::uint32_t linked = catenae::version(); // the compiled core's number
    const bool          same   = linked == CATENAE_VERSION;
    std::printf("catenae %d.%d.%d, compiled core %" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n", CATENAE_VERSION_MAJOR,
                CATENAE_VERSION_MINOR, CATENAE_VERSION_PATCH, linked / 65536u, (linked / 256u) % 256u, linked % 256u);
    // ANCHOR_END: check
    std::printf("CATENAE_EXAMPLE_DONE ok=%d\n", same ? 1 : 0);
    return same ? 0 : 1;
}
