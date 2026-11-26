/* @file catenae_c_consumer.h
 * @brief The C consumer of the fit milestone's C ABI (c_consumer.c), as its test calls it.
 *
 * Copyright 2026 Timothy Place and the catenae contributors
 */
#ifndef CATENAE_FIT_CAPI_CATENAE_C_CONSUMER_H
#define CATENAE_FIT_CAPI_CATENAE_C_CONSUMER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configures the strip entry at `level`, resets it, runs `in` through it: the status of the
 * first call that failed, or CATENAE_C_OK. */
int catenae_c_consumer_run(float level, const float* in, size_t in_values, float* out, size_t out_values,
                           size_t* written);

#ifdef __cplusplus
}
#endif

#endif /* CATENAE_FIT_CAPI_CATENAE_C_CONSUMER_H */
