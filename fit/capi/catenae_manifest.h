/* @file catenae_manifest.h
 * @brief The fit milestone's first C ABI manifest (PLAN.md section 7.5, D45): one list,
 * expanded twice.
 *
 * Copyright 2026 Timothy Place and the catenae contributors
 *
 * Plain C, macros only, so both the C header (catenae_c.h) and the C++ definitions
 * (catenae_c.cpp) can expand it; no generator and no step that reads a built object. An entry
 * names a configuration type the definitions alias in namespace catenae_c_types and fixes only
 * type-level numbers:
 *
 *   X(name, in_channels, out_channels,
 *     configuration_capacity, configuration_alignment,
 *     state_capacity, state_alignment,
 *     workspace_capacity, workspace_alignment,
 *     shares_workspace_with)
 *
 * The capacities are what a C caller reserves on every target; the definitions
 * static_assert that the target's own sizes fit them, and export the exact sizes. An entry
 * whose workspace type is another entry's names that entry in the last column (itself when
 * it shares with none): entries of one chain type that run on one thread may share one
 * workspace, and the definitions assert that the two types are the same. Samples are float.
 */
#ifndef CATENAE_FIT_CAPI_CATENAE_MANIFEST_H
#define CATENAE_FIT_CAPI_CATENAE_MANIFEST_H

/* clang-format off */
#define CATENAE_C_MANIFEST(X)                                       \
    X(strip,      2, 2,  64, 8,  512, 8, 1024, 8, strip)            \
    X(strip_mono, 1, 1,  64, 8,  256, 8,  512, 8, strip_mono)
/* clang-format on */

/* The parameters an entry exports as setters: P(entry, parameter, value type). */
#define CATENAE_C_PARAMETERS(P)                                                                                        \
    P(strip, level, float)                                                                                             \
    P(strip_mono, level, float)

#endif /* CATENAE_FIT_CAPI_CATENAE_MANIFEST_H */
