/* @file catenae_manifest.h
 * @brief The fit milestone's first C ABI manifest (PLAN.md section 7.5, D45): one list,
 * expanded twice.
 *
 * Copyright 2026 Timothy Place and the catenae contributors
 *
 * Plain C, macros only, so both the C header (catenae_c.h) and the C++ definitions
 * (catenae_c.cpp) can expand it; no generator and no step that reads a built object. An entry
 * names a type the definitions alias in namespace catenae_c_types and fixes only type-level
 * numbers. Samples are float. The capacities are what a C caller reserves on every target; the
 * definitions static_assert that the target's own sizes fit them, and export the exact sizes.
 *
 * Chains and stages, run by process:
 *
 *   X(name, in_channels, out_channels,
 *     configuration_capacity, configuration_alignment,
 *     state_capacity, state_alignment,
 *     workspace_capacity, workspace_alignment,
 *     shares_workspace_with)
 *
 * An entry whose workspace type is another entry's names that entry in the last column (itself
 * when it shares with none): entries of one chain type that run on one thread may share one
 * workspace, and the definitions assert that the two types are the same.
 *
 * Junctions, run by push and pull (the async skeleton):
 *
 *   J(name, channels, configuration_capacity, configuration_alignment,
 *     state_capacity, state_alignment, report_capacity, report_alignment)
 *
 * Table designs (PLAN.md 3.3: built by make(spec, table, workspace) into the caller's storage),
 * each for an entry above, and the spec's parameters as a parenthesised list, twice:
 *
 *   D(entry, table_capacity, table_alignment, scratch_capacity, scratch_alignment,
 *     (parameter declarations), (parameter names))
 *
 * Configurations made from numbers (D37's make() on a type with no table):
 *
 *   M(entry, (parameter declarations), (parameter names))
 */
#ifndef CATENAE_FIT_CAPI_CATENAE_MANIFEST_H
#define CATENAE_FIT_CAPI_CATENAE_MANIFEST_H

/* clang-format off */
#define CATENAE_C_MANIFEST(X)                                                   \
    X(strip,        2, 2,  64, 8,  512, 8,  1024, 8, strip)                     \
    X(strip_mono,   1, 1,  64, 8,  256, 8,   512, 8, strip_mono)                \
    X(rational_3_2, 2, 2,   8, 8, 2048, 8,  4096, 8, rational_3_2)              \
    X(bridge_48k,   2, 2,  16, 8, 1024, 8,  4096, 8, bridge_48k)

#define CATENAE_C_JUNCTIONS(J)                                                  \
    J(async_2, 2, 256, 8, 16384, 8, 64, 8)

#define CATENAE_C_DESIGNS(D)                                                    \
    D(bridge_48k, 2560, 8, 10240, 8,                                            \
      (double passband_hz, double stopband_atten_db), (passband_hz, stopband_atten_db))

#define CATENAE_C_MAKERS(M)                                                     \
    M(async_2, (unsigned target_latency_frames), (target_latency_frames))
/* clang-format on */

/* The parameters an entry exports as setters: P(entry, parameter, value type). */
#define CATENAE_C_PARAMETERS(P)                                                                                        \
    P(strip, level, float)                                                                                             \
    P(strip_mono, level, float)

#endif /* CATENAE_FIT_CAPI_CATENAE_MANIFEST_H */
