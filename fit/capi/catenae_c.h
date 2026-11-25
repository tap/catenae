/* @file catenae_c.h
 * @brief The fit milestone's C ABI (PLAN.md section 7.5): declarations and storage, expanded
 * from catenae_manifest.h.
 *
 * Copyright 2026 Timothy Place and the catenae contributors
 *
 * For each entry `name` of the manifest:
 *
 *   catenae_c_<name>_configuration_size(), _configuration_align()
 *   catenae_c_<name>_configuration_copy(dst, dst_size)   the entry's configuration, constructed
 *                                                         in the caller's storage
 *   catenae_c_<name>_state_size(), _state_align(), _workspace_size(), _workspace_align()
 *   catenae_c_<name>_init(configuration, configuration_size, state, state_size)
 *                                                         reset, after checking every size and
 *                                                         the alignment: C names do not carry the
 *                                                         inline namespace, so this is where a
 *                                                         header built for another target, check
 *                                                         level or back end is caught
 *   catenae_c_<name>_process(configuration, configuration_size, state, state_size,
 *                            workspace, workspace_size, in, in_values, out, out_values,
 *                            written)                     the chain over interleaved float
 *   catenae_c_<name>_set_<parameter>(configuration, configuration_size, value)
 *
 * Every function that takes storage returns a status: a wrong size from a C or Python caller is
 * a status, never the library's check handler.
 */
#ifndef CATENAE_FIT_CAPI_CATENAE_C_H
#define CATENAE_FIT_CAPI_CATENAE_C_H

#include <stddef.h>

#include "catenae_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a call returns. */
typedef int catenae_c_status;
#define CATENAE_C_OK 0               /* done */
#define CATENAE_C_WRONG_SIZE 1       /* a size is not the entry's on this target and build */
#define CATENAE_C_MISALIGNED 2       /* storage is not aligned as the entry needs */
#define CATENAE_C_NOT_WHOLE_FRAMES 3 /* the input is not a whole number of frames */
#define CATENAE_C_OUTPUT_TOO_SMALL 4 /* the output cannot hold what the call writes */
#define CATENAE_C_NULL 5             /* a pointer is null */

/* Storage a caller declares for an entry, with the manifest's capacity and alignment. */
#ifdef __cplusplus
#define CATENAE_C_ALIGNAS(a) alignas(a)
#else
#define CATENAE_C_ALIGNAS(a) _Alignas(a)
#endif

#define CATENAE_C_CAPACITIES(name, in_ch, out_ch, ccap, calign, scap, salign, wcap, walign, share)                     \
    enum {                                                                                                             \
        catenae_c_##name##_in_channels             = in_ch,                                                            \
        catenae_c_##name##_out_channels            = out_ch,                                                           \
        catenae_c_##name##_configuration_capacity  = ccap,                                                             \
        catenae_c_##name##_configuration_alignment = calign,                                                           \
        catenae_c_##name##_state_capacity          = scap,                                                             \
        catenae_c_##name##_state_alignment         = salign,                                                           \
        catenae_c_##name##_workspace_capacity      = wcap,                                                             \
        catenae_c_##name##_workspace_alignment     = walign                                                            \
    };
CATENAE_C_MANIFEST(CATENAE_C_CAPACITIES)
#undef CATENAE_C_CAPACITIES

#define CATENAE_C_CONFIGURATION_STORAGE(name, var)                                                                     \
    CATENAE_C_ALIGNAS(catenae_c_##name##_configuration_alignment)                                                      \
    unsigned char var[catenae_c_##name##_configuration_capacity]
#define CATENAE_C_STATE_STORAGE(name, var)                                                                             \
    CATENAE_C_ALIGNAS(catenae_c_##name##_state_alignment) unsigned char var[catenae_c_##name##_state_capacity]
#define CATENAE_C_WORKSPACE_STORAGE(name, var)                                                                         \
    CATENAE_C_ALIGNAS(catenae_c_##name##_workspace_alignment) unsigned char var[catenae_c_##name##_workspace_capacity]

#define CATENAE_C_DECLARE(name, in_ch, out_ch, ccap, calign, scap, salign, wcap, walign, share)                        \
    size_t           catenae_c_##name##_configuration_size(void);                                                      \
    size_t           catenae_c_##name##_configuration_align(void);                                                     \
    catenae_c_status catenae_c_##name##_configuration_copy(void* dst, size_t dst_size);                                \
    size_t           catenae_c_##name##_state_size(void);                                                              \
    size_t           catenae_c_##name##_state_align(void);                                                             \
    size_t           catenae_c_##name##_workspace_size(void);                                                          \
    size_t           catenae_c_##name##_workspace_align(void);                                                         \
    catenae_c_status catenae_c_##name##_init(const void* configuration, size_t configuration_size, void* state,        \
                                             size_t state_size);                                                       \
    catenae_c_status catenae_c_##name##_process(                                                                       \
        const void* configuration, size_t configuration_size, void* state, size_t state_size, void* workspace,         \
        size_t workspace_size, const float* in, size_t in_values, float* out, size_t out_values, size_t* written);
CATENAE_C_MANIFEST(CATENAE_C_DECLARE)
#undef CATENAE_C_DECLARE

#define CATENAE_C_DECLARE_SETTER(name, parameter, value_type)                                                          \
    catenae_c_status catenae_c_##name##_set_##parameter(void* configuration, size_t configuration_size,                \
                                                        value_type value);
CATENAE_C_PARAMETERS(CATENAE_C_DECLARE_SETTER)
#undef CATENAE_C_DECLARE_SETTER

/* The manifest's own count: entries, and the functions they export. */
size_t catenae_c_entries(void);
size_t catenae_c_functions(void);

#ifdef __cplusplus
}
#endif

#endif /* CATENAE_FIT_CAPI_CATENAE_C_H */
