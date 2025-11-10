/**
 * P2 Online Quantile Estimator (Jain & Chlamtac, 1985)
 * Tracks 99th percentile for adaptive thresholds
 * RAM: ~80 bytes per quantile
 */

#ifndef EPS_P2_QUANTILE_H
#define EPS_P2_QUANTILE_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

typedef struct {
    float q[5];           // Marker heights
    float n_markers[5];   // Ideal marker positions
    uint32_t n_actual[5]; // Actual marker positions
    uint32_t n;           // Total samples
    float p;              // Target quantile (e.g., 0.99)
    bool initialized;
    float init_buffer[5];
    uint8_t init_count;
} P2Quantile;

// Initialize P2 estimator
static inline void p2_init(P2Quantile* p2, float quantile) {
    p2->p = quantile;
    p2->n = 0;
    p2->initialized = false;
    p2->init_count = 0;
}

// Comparison function for sorting (insertion sort for 5 elements)
static void p2_sort_buffer(float* buf, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
        float key = buf[i];
        int8_t j = i - 1;
        while (j >= 0 && buf[j] > key) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = key;
    }
}

// Update quantile estimate
static inline void p2_update(P2Quantile* p2, float value) {
    // Initialization phase
    if (p2->init_count < 5) {
        p2->init_buffer[p2->init_count++] = value;
        if (p2->init_count == 5) {
            p2_sort_buffer(p2->init_buffer, 5);
            for (uint8_t i = 0; i < 5; i++) {
                p2->q[i] = p2->init_buffer[i];
                p2->n_actual[i] = i + 1;
            }
            p2->n_markers[0] = 1.0f;
            p2->n_markers[1] = 1.0f + 2.0f * p2->p;
            p2->n_markers[2] = 1.0f + 4.0f * p2->p;
            p2->n_markers[3] = 3.0f + 2.0f * p2->p;
            p2->n_markers[4] = 5.0f;
            p2->n = 5;
            p2->initialized = true;
        }
        return;
    }

    // Find cell k
    uint8_t k = 0;
    if (value < p2->q[0]) {
        p2->q[0] = value;
        k = 0;
    } else if (value >= p2->q[4]) {
        p2->q[4] = value;
        k = 3;
    } else {
        for (uint8_t i = 1; i < 5; i++) {
            if (value < p2->q[i]) {
                k = i - 1;
                break;
            }
        }
    }

    // Increment positions
    for (uint8_t i = k + 1; i < 5; i++) {
        p2->n_actual[i]++;
    }

    // Update ideal positions
    p2->n++;
    p2->n_markers[1] = 1.0f + 2.0f * p2->p * (p2->n - 1);
    p2->n_markers[2] = 1.0f + 4.0f * p2->p * (p2->n - 1);
    p2->n_markers[3] = 3.0f + 2.0f * p2->p * (p2->n - 1);
    p2->n_markers[4] = (float)p2->n;

    // Adjust heights (simplified P2 algorithm)
    for (uint8_t i = 1; i < 4; i++) {
        float d = p2->n_markers[i] - (float)p2->n_actual[i];
        if ((d >= 1.0f && (p2->n_actual[i+1] - p2->n_actual[i]) > 1) ||
            (d <= -1.0f && (p2->n_actual[i-1] - p2->n_actual[i]) < -1)) {

            int8_t d_sign = (d >= 0.0f) ? 1 : -1;

            // Parabolic formula
            float q_new = p2->q[i] + (float)d_sign / (float)(p2->n_actual[i+1] - p2->n_actual[i-1]) * (
                ((float)(p2->n_actual[i] - p2->n_actual[i-1] + d_sign)) * 
                (p2->q[i+1] - p2->q[i]) / (float)(p2->n_actual[i+1] - p2->n_actual[i]) +
                ((float)(p2->n_actual[i+1] - p2->n_actual[i] - d_sign)) * 
                (p2->q[i] - p2->q[i-1]) / (float)(p2->n_actual[i] - p2->n_actual[i-1])
            );

            // Check bounds
            if (p2->q[i-1] < q_new && q_new < p2->q[i+1]) {
                p2->q[i] = q_new;
            } else {
                // Linear fallback
                p2->q[i] = p2->q[i] + (float)d_sign * (p2->q[i+d_sign] - p2->q[i]) / 
                          (float)(p2->n_actual[i+d_sign] - p2->n_actual[i]);
            }
            p2->n_actual[i] += d_sign;
        }
    }
}

// Get current quantile estimate
static inline float p2_get_quantile(P2Quantile* p2) {
    return p2->initialized ? p2->q[2] : 0.0f;
}

// Check if estimator is initialized
static inline bool p2_is_ready(P2Quantile* p2) {
    return p2->initialized;
}

#endif // EPS_P2_QUANTILE_H
