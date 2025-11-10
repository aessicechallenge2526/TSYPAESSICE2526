/**
 * Feature extraction implementation for STM32
 */

#include "eps_model_config.h"
#include <string.h>

void eps_init_buffers(EPS_FeatureBuffers *buffers) {
    memset(buffers->power_buffer, 0, sizeof(buffers->power_buffer));
    memset(buffers->voltage_buffer, 0, sizeof(buffers->voltage_buffer));
    buffers->buffer_index = 0;
}

void eps_update_buffers(EPS_FeatureBuffers *buffers, double power, double voltage) {
    buffers->buffer_index = (buffers->buffer_index + 1) % RING_BUFFER_SIZE;
    buffers->power_buffer[buffers->buffer_index] = power;
    buffers->voltage_buffer[buffers->buffer_index] = voltage;
}

static inline double get_lag(double *buffer, uint8_t current_idx, uint8_t lag) {
    int idx = (current_idx - lag + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
    return buffer[idx];
}

void eps_extract_power_features(EPS_FeatureBuffers *buffers, double *features_out) {
    // Power lags: 1,2,3,6,12
    uint8_t idx = buffers->buffer_index;
    features_out[0] = get_lag(buffers->power_buffer, idx, 1);
    features_out[1] = get_lag(buffers->power_buffer, idx, 2);
    features_out[2] = get_lag(buffers->power_buffer, idx, 3);
    features_out[3] = get_lag(buffers->power_buffer, idx, 6);
    features_out[4] = get_lag(buffers->power_buffer, idx, 12);

    // Power_diff lags: compute derivatives then lag them
    double p_diff = buffers->power_buffer[idx] - get_lag(buffers->power_buffer, idx, 1);
    features_out[5] = p_diff;  // Power_diff_lag1 (approximate)
    features_out[6] = get_lag(buffers->power_buffer, idx, 2) - get_lag(buffers->power_buffer, idx, 3);
    features_out[7] = get_lag(buffers->power_buffer, idx, 3) - get_lag(buffers->power_buffer, idx, 4);
    features_out[8] = get_lag(buffers->power_buffer, idx, 6) - get_lag(buffers->power_buffer, idx, 7);
    features_out[9] = get_lag(buffers->power_buffer, idx, 12) - ((idx >= 13) ? get_lag(buffers->power_buffer, idx, 13) : 0);
}

void eps_extract_voltage_features(EPS_FeatureBuffers *buffers, double *features_out) {
    // Voltage lags: 1,2,3,6,12
    uint8_t idx = buffers->buffer_index;
    features_out[0] = get_lag(buffers->voltage_buffer, idx, 1);
    features_out[1] = get_lag(buffers->voltage_buffer, idx, 2);
    features_out[2] = get_lag(buffers->voltage_buffer, idx, 3);
    features_out[3] = get_lag(buffers->voltage_buffer, idx, 6);
    features_out[4] = get_lag(buffers->voltage_buffer, idx, 12);
}
