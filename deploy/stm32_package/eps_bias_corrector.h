/**
 * Online Bias Correction for EPS Predictions
 * Lightweight EWMA-based drift compensation
 * RAM: ~32 bytes
 */

#ifndef EPS_BIAS_CORRECTOR_H
#define EPS_BIAS_CORRECTOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float bias_power;      // Current power bias estimate
    float bias_voltage;    // Current voltage bias estimate
    uint32_t n_samples;    // Number of samples processed
    float alpha;           // EWMA decay factor (e.g., 0.01)
    uint32_t warmup;       // Warmup period (e.g., 50)
} BiasCorrector;

// Initialize bias corrector
static inline void bias_init(BiasCorrector* bc, float alpha, uint32_t warmup) {
    bc->bias_power = 0.0f;
    bc->bias_voltage = 0.0f;
    bc->n_samples = 0;
    bc->alpha = alpha;
    bc->warmup = warmup;
}

// Update bias estimates (call after each observation)
static inline void bias_update(BiasCorrector* bc, 
                                float y_true_power, float y_pred_power,
                                float y_true_voltage, float y_pred_voltage) {
    float residual_p = y_true_power - y_pred_power;
    float residual_v = y_true_voltage - y_pred_voltage;

    if (bc->n_samples < bc->warmup) {
        // Warmup: simple cumulative average
        bc->bias_power = (bc->bias_power * bc->n_samples + residual_p) / (bc->n_samples + 1);
        bc->bias_voltage = (bc->bias_voltage * bc->n_samples + residual_v) / (bc->n_samples + 1);
    } else {
        // After warmup: EWMA
        bc->bias_power = bc->alpha * residual_p + (1.0f - bc->alpha) * bc->bias_power;
        bc->bias_voltage = bc->alpha * residual_v + (1.0f - bc->alpha) * bc->bias_voltage;
    }
    bc->n_samples++;
}

// Apply correction to predictions
static inline void bias_correct(BiasCorrector* bc, 
                                 float* y_pred_power, 
                                 float* y_pred_voltage) {
    if (bc->n_samples >= bc->warmup) {
        *y_pred_power += bc->bias_power;
        *y_pred_voltage += bc->bias_voltage;
    }
}

// Check if corrector is ready (past warmup)
static inline bool bias_is_ready(BiasCorrector* bc) {
    return bc->n_samples >= bc->warmup;
}

#endif // EPS_BIAS_CORRECTOR_H
