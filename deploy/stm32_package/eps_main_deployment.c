/**
 * EPS Predictive FDIR - Main Deployment Loop
 * 
 * Complete system for 13-panel satellite deployment
 * - Single RandomForest model (trained on NEPALISAT)
 * - Deployed across all 13 panels with online bias correction
 * - Per-panel fine-tuning via EWMA adaptive learning
 * 
 * Target: STM32F4 @ 168MHz
 * Sampling: 5 seconds per cycle
 */

#include "eps_protection_final.h"
#include "eps_bias_corrector.h"   // Online fine-tuning
#include "power_model.h"           // Generated C code from m2cgen (generic model)
#include <stdio.h>
#include <string.h>

// Forward declarations for model inference functions
extern double score(double* input);          // Power model (from power_model.c)
extern double score_voltage(double* input);  // Voltage model (from voltage_model.c)

// ===== HARDWARE CONFIGURATION =====

// ADC channels for voltage sensing (one per panel)
#define ADC_CHANNELS_VOLTAGE {ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, \
                              ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7, \
                              ADC_CHANNEL_8, ADC_CHANNEL_9, ADC_CHANNEL_10, ADC_CHANNEL_11, \
                              ADC_CHANNEL_12}

// ADC channels for current sensing (one per panel)
#define ADC_CHANNELS_CURRENT {ADC_CHANNEL_13, ADC_CHANNEL_14, ADC_CHANNEL_15, ADC_CHANNEL_0, \
                              ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4, \
                              ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7, ADC_CHANNEL_8, \
                              ADC_CHANNEL_9}

// ===== GLOBAL STATE =====

// Panel-specific nominal values (adjust based on your satellite configuration)
const float PANEL_P_NOMINAL[NUM_PANELS] = {
    8.4f, 8.4f, 8.4f, 8.4f, 8.4f, 8.4f, 8.4f,  // Panels 0-6
    8.4f, 8.4f, 8.4f, 8.4f, 8.4f, 8.4f         // Panels 7-12
};

const float PANEL_V_NOMINAL[NUM_PANELS] = {
    17.5f, 17.5f, 17.5f, 17.5f, 17.5f, 17.5f, 17.5f,  // Panels 0-6
    17.5f, 17.5f, 17.5f, 17.5f, 17.5f, 17.5f         // Panels 7-12
};

// Feature buffers for each panel (lag windows)
// Power model needs: Power_lag1, Power_lag2, Power_lag3, Power_lag6, Power_lag12
//                    Power_diff_lag1, ..., Power_diff_lag12 (10 features)
// Voltage model needs: Volt_lag1, Volt_lag2, Volt_lag3, Volt_lag6, Volt_lag12 (5 features)

#define POWER_LAG_SIZE 12
#define VOLTAGE_LAG_SIZE 12

typedef struct {
    float power_history[POWER_LAG_SIZE + 1];   // +1 for current sample
    float voltage_history[VOLTAGE_LAG_SIZE + 1];
    uint8_t history_index;  // Circular buffer index
    bool initialized;       // Need 12 samples before prediction
    BiasCorrector bias_corrector;  // Online fine-tuning per panel
} PanelFeatureBuffer_t;

PanelFeatureBuffer_t panel_buffers[NUM_PANELS];

// ===== INITIALIZATION =====

void eps_main_init(void) {
    // Initialize protection system
    eps_protection_init();
    
    // Configure panel-specific parameters
    for (uint8_t i = 0; i < NUM_PANELS; i++) {
        eps_protection_init_panel(i, PANEL_P_NOMINAL[i], PANEL_V_NOMINAL[i]);
        
        // Initialize feature buffers
        memset(&panel_buffers[i], 0, sizeof(PanelFeatureBuffer_t));
        panel_buffers[i].history_index = 0;
        panel_buffers[i].initialized = false;
        
        // Initialize bias corrector for online fine-tuning
        // alpha=0.01 -> slow adaptation, warmup=50 samples = 250s
        bias_init(&panel_buffers[i].bias_corrector, 0.01f, 50);
    }
    
    // Initialize ADC
    // HAL_ADC_Init(...);
    
    log_event("EPS Main Loop Initialized - 13 panels ready");
}

// ===== ADC READING =====

float read_panel_voltage(uint8_t panel_id) {
    // Read voltage from ADC
    // Scale: ADC (0-4095) -> Voltage (0-25V)
    
    // Example:
    // uint32_t adc_val = HAL_ADC_GetValue(&hadc1);
    // float voltage = (adc_val / 4095.0f) * 25.0f;
    
    // Placeholder for simulation
    return PANEL_V_NOMINAL[panel_id];  // Replace with real ADC read
}

float read_panel_current(uint8_t panel_id) {
    // Read current from ADC via shunt resistor
    // Scale: ADC (0-4095) -> Current (0-2A)
    
    // Example:
    // uint32_t adc_val = HAL_ADC_GetValue(&hadc2);
    // float voltage_shunt = (adc_val / 4095.0f) * 3.3f;
    // float current = voltage_shunt / R_shunt;  // R_shunt = 0.1 ohm
    
    // Placeholder for simulation
    float nominal_current = PANEL_P_NOMINAL[panel_id] / PANEL_V_NOMINAL[panel_id];
    return nominal_current;  // Replace with real ADC read
}

// ===== FEATURE ENGINEERING =====

void update_panel_history(uint8_t panel_id, float power, float voltage) {
    PanelFeatureBuffer_t* buf = &panel_buffers[panel_id];
    
    // Store in circular buffer
    buf->power_history[buf->history_index] = power;
    buf->voltage_history[buf->history_index] = voltage;
    
    buf->history_index = (buf->history_index + 1) % (POWER_LAG_SIZE + 1);
    
    // Check if we have enough history (need 12 lags + current = 13 samples minimum)
    static uint8_t sample_counts[NUM_PANELS] = {0};
    if (!buf->initialized) {
        sample_counts[panel_id]++;
        if (sample_counts[panel_id] >= POWER_LAG_SIZE) {
            buf->initialized = true;
            log_event("Panel %d: Feature buffer initialized (%d samples)", 
                     panel_id, POWER_LAG_SIZE);
        }
    }
}

float get_lag_value(float* history, uint8_t current_idx, uint8_t lag, uint8_t buffer_size) {
    // Get value from 'lag' timesteps ago
    int idx = (int)current_idx - lag;
    if (idx < 0) idx += buffer_size;
    return history[idx];
}

bool build_power_features(uint8_t panel_id, double* features) {
    // Build 10 features for power prediction:
    // Power_lag1, Power_lag2, Power_lag3, Power_lag6, Power_lag12
    // Power_diff_lag1, Power_diff_lag2, Power_diff_lag3, Power_diff_lag6, Power_diff_lag12
    
    PanelFeatureBuffer_t* buf = &panel_buffers[panel_id];
    if (!buf->initialized) return false;
    
    uint8_t curr = buf->history_index;
    uint8_t size = POWER_LAG_SIZE + 1;
    
    // Get lag values
    float P_lag1 = get_lag_value(buf->power_history, curr, 1, size);
    float P_lag2 = get_lag_value(buf->power_history, curr, 2, size);
    float P_lag3 = get_lag_value(buf->power_history, curr, 3, size);
    float P_lag6 = get_lag_value(buf->power_history, curr, 6, size);
    float P_lag12 = get_lag_value(buf->power_history, curr, 12, size);
    
    // Compute derivatives (Power_diff = Power[t] - Power[t-1])
    float P_lag0 = get_lag_value(buf->power_history, curr, 0, size);
    float dP_lag1 = P_lag1 - P_lag2;
    float dP_lag2 = P_lag2 - P_lag3;
    float dP_lag3 = P_lag3 - get_lag_value(buf->power_history, curr, 4, size);
    float dP_lag6 = P_lag6 - get_lag_value(buf->power_history, curr, 7, size);
    float dP_lag12 = P_lag12 - get_lag_value(buf->power_history, curr, 13, size);
    
    // Fill feature array (order must match training!)
    features[0] = P_lag1;
    features[1] = P_lag2;
    features[2] = P_lag3;
    features[3] = P_lag6;
    features[4] = P_lag12;
    features[5] = dP_lag1;
    features[6] = dP_lag2;
    features[7] = dP_lag3;
    features[8] = dP_lag6;
    features[9] = dP_lag12;
    
    return true;
}

bool build_voltage_features(uint8_t panel_id, double* features) {
    // Build 5 features for voltage prediction:
    // Volt_lag1, Volt_lag2, Volt_lag3, Volt_lag6, Volt_lag12
    
    PanelFeatureBuffer_t* buf = &panel_buffers[panel_id];
    if (!buf->initialized) return false;
    
    uint8_t curr = buf->history_index;
    uint8_t size = VOLTAGE_LAG_SIZE + 1;
    
    features[0] = get_lag_value(buf->voltage_history, curr, 1, size);
    features[1] = get_lag_value(buf->voltage_history, curr, 2, size);
    features[2] = get_lag_value(buf->voltage_history, curr, 3, size);
    features[3] = get_lag_value(buf->voltage_history, curr, 6, size);
    features[4] = get_lag_value(buf->voltage_history, curr, 12, size);
    
    return true;
}

// ===== MODEL INFERENCE =====

// Generic RandomForest model (trained on NEPALISAT, deployed to all panels)
// Online bias correction handles per-panel adaptation

float predict_power(uint8_t panel_id, double* features) {
    // Call generated C inference function (generic model)
    return (float)score(features);  // From power_model.c (m2cgen generated)
}

float predict_voltage(uint8_t panel_id, double* features) {
    // Call generated C inference function (generic model)
    return (float)score_voltage(features);  // From voltage_model.c (m2cgen generated)
}

// ===== MAIN LOOP =====

void eps_main_loop_iteration(void) {
    // This function is called every 5 seconds
    
    for (uint8_t panel_id = 0; panel_id < NUM_PANELS; panel_id++) {
        
        // ===== 1. READ SENSORS =====
        float V_measured = read_panel_voltage(panel_id);
        float I_measured = read_panel_current(panel_id);
        float P_measured = V_measured * I_measured;
        
        // ===== 2. UPDATE HISTORY =====
        update_panel_history(panel_id, P_measured, V_measured);
        
        // ===== 3. CHECK IF READY FOR PREDICTION =====
        if (!panel_buffers[panel_id].initialized) {
            // Still collecting initial samples
            continue;
        }
        
        // ===== 4. BUILD FEATURES =====
        double power_features[10];
        double voltage_features[5];
        
        if (!build_power_features(panel_id, power_features)) continue;
        if (!build_voltage_features(panel_id, voltage_features)) continue;
        
        // ===== 5. RUN INFERENCE =====
        uint32_t start_time = HAL_GetTick();
        
        // Generic model inference (same model for all panels)
        float P_predicted_raw = predict_power(panel_id, power_features);
        float V_predicted_raw = predict_voltage(panel_id, voltage_features);
        
        // Apply online bias correction (per-panel fine-tuning)
        float P_predicted = P_predicted_raw;
        float V_predicted = V_predicted_raw;
        BiasCorrector* bc = &panel_buffers[panel_id].bias_corrector;
        bias_correct(bc, &P_predicted, &V_predicted);
        
        uint32_t inference_time_us = (HAL_GetTick() - start_time) * 1000;
        
        // ===== 6. RUN PROTECTION LOGIC =====
        eps_protection_update(panel_id, P_measured, V_measured, 
                            P_predicted, V_predicted);
        
        // ===== 7. UPDATE BIAS CORRECTOR (online learning) =====
        // Use actual measurements to fine-tune predictions for this panel
        bias_update(bc, P_measured, P_predicted_raw, 
                   V_measured, V_predicted_raw);
        
        // ===== 8. PERIODIC LOGGING (every 60 seconds = 12 iterations) =====
        static uint8_t log_counter = 0;
        if (log_counter % 12 == 0) {
            float bias_p = bc->bias_power;
            float bias_v = bc->bias_voltage;
            bool adapted = bias_is_ready(bc);
            
            log_event("Panel %d: P=%.2fW (pred %.2fW, bias %.3fW%s), V=%.2fV (pred %.2fV, bias %.3fV%s), infer=%luμs",
                     panel_id, 
                     P_measured, P_predicted, bias_p, adapted ? " ✓" : "",
                     V_measured, V_predicted, bias_v, adapted ? " ✓" : "",
                     inference_time_us);
        }
    }
    
    static uint8_t log_counter = 0;
    log_counter++;
}

// ===== ENTRY POINT =====

int main(void) {
    // HAL initialization
    // HAL_Init();
    // SystemClock_Config();
    
    // Initialize EPS system
    eps_main_init();
    
    log_event("=== EPS PREDICTIVE FDIR STARTED ===");
    log_event("Configuration: 13 panels, 5s sampling, dual-layer protection");
    log_event("Model: Generic RandomForest (NEPALISAT) + per-panel bias correction");
    log_event("Bias correction: alpha=0.01, warmup=50 samples (250s)");
    
    // Main loop (5-second sampling)
    while (1) {
        eps_main_loop_iteration();
        
        // Wait 5 seconds
        // HAL_Delay(5000);
        
        // For simulation, break after one iteration
        break;
    }
    
    return 0;
}

// ===== COMMAND INTERFACE =====

// Example ground station command handler
void handle_ground_command(uint8_t panel_id, const char* command) {
    if (strcmp(command, "REENABLE") == 0) {
        process_ground_command(panel_id, CMD_REENABLE);
        log_event("Ground command: RE-ENABLE panel %d", panel_id);
    }
    else if (strcmp(command, "STATUS") == 0) {
        uint32_t enable_count, trip_count, false_alarm_count;
        get_panel_statistics(panel_id, &enable_count, &trip_count, &false_alarm_count);
        
        log_event("Panel %d stats: Enable=%lu, Trip=%lu, FalseAlarm=%lu",
                 panel_id, enable_count, trip_count, false_alarm_count);
    }
    else {
        log_event("Unknown command: %s", command);
    }
}
