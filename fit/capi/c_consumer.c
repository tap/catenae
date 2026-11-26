/* @file c_consumer.c
 * @brief The fit milestone's C ABI from C: storage declared with the manifest's macros, an
 * entry configured, reset and run. Compiled as C11 into the fit tests.
 *
 * Copyright 2026 Timothy Place and the catenae contributors
 */
#include "catenae_c.h"
#include "catenae_c_consumer.h"

int catenae_c_consumer_run(float level, const float* in, size_t in_values, float* out, size_t out_values,
                           size_t* written) {
    CATENAE_C_CONFIGURATION_STORAGE(strip, configuration);
    CATENAE_C_STATE_STORAGE(strip, state);
    CATENAE_C_WORKSPACE_STORAGE(strip, workspace);
    const size_t     configuration_size = catenae_c_strip_configuration_size();
    catenae_c_status status             = catenae_c_strip_configuration_copy(configuration, sizeof configuration);
    if (status == CATENAE_C_OK) {
        status = catenae_c_strip_set_level(configuration, configuration_size, level);
    }
    if (status == CATENAE_C_OK) {
        status = catenae_c_strip_init(configuration, configuration_size, state, sizeof state);
    }
    if (status == CATENAE_C_OK) {
        status = catenae_c_strip_process(configuration, configuration_size, state, sizeof state, workspace,
                                         sizeof workspace, in, in_values, out, out_values, written);
    }
    return status;
}
