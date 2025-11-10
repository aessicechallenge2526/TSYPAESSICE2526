/**
 * EPS Predictive FDIR - Model Configuration
 * Generated: 2025-11-10 12:11:35
 * Panel: +X
 * 
 * Target: STM32 MCU
 */

#ifndef EPS_MODEL_CONFIG_H
#define EPS_MODEL_CONFIG_H

#include <stdint.h>

// Model configuration
#define POWER_N_FEATURES  10
#define VOLTAGE_N_FEATURES 5
#define N_LAG_STEPS 5  // lags: 1,2,3,6,12

// Feature names (for reference)
// Power features: Power_lag1, Power_lag2, Power_lag3, Power_lag6, Power_lag12, Power_diff_lag1, Power_diff_lag2, Power_diff_lag3, Power_diff_lag6, Power_diff_lag12
// Voltage features: Volt_lag1, Volt_lag2, Volt_lag3, Volt_lag6, Volt_lag12

// Ring buffer sizes (max lag is 12)
#define RING_BUFFER_SIZE 13

// Prediction functions (implemented in generated C files)
double predict_power(double *features);
double predict_voltage(double *features);

// Feature extraction helpers
typedef struct {
    double power_buffer[RING_BUFFER_SIZE];
    double voltage_buffer[RING_BUFFER_SIZE];
    uint8_t buffer_index;
} EPS_FeatureBuffers;

void eps_init_buffers(EPS_FeatureBuffers *buffers);
void eps_update_buffers(EPS_FeatureBuffers *buffers, double power, double voltage);
void eps_extract_power_features(EPS_FeatureBuffers *buffers, double *features_out);
void eps_extract_voltage_features(EPS_FeatureBuffers *buffers, double *features_out);

#endif // EPS_MODEL_CONFIG_H
