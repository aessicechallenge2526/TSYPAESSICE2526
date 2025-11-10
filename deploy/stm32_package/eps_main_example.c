/**
 * EPS Predictive FDIR - Complete STM32 Integration Example
 * 
 * This example shows how to integrate all components:
 * - Feature extraction with ring buffers
 * - Model inference (power & voltage)
 * - Online bias correction
 * - Adaptive threshold tracking
 * - Logic block with hysteresis
 * 
 * Target: STM32F4 or higher (512KB+ Flash, 128KB+ RAM)
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "eps_model_config.h"
#include "eps_bias_corrector.h"
#include "eps_p2_quantile.h"

// External model functions (from generated C code)
extern double predict_power(double *features);
extern double predict_voltage(double *features);

// Logic block state machine
typedef struct {
    bool armed_power;
    bool armed_voltage;
    uint8_t consecutive_power;
    uint8_t consecutive_voltage;
    uint32_t trip_count_power;
    uint32_t trip_count_voltage;
} EPS_LogicState;

// Global state (persist in FRAM/EEPROM for reboot survival)
static EPS_FeatureBuffers buffers;
static BiasCorrector bias_corrector;
static P2Quantile p2_power;
static P2Quantile p2_voltage;
static EPS_LogicState logic_state;

// Configuration (compile-time or loaded from config file)
#define GATE_N 3                    // Consecutive samples before trip
#define ARM_THRESHOLD_MULTIPLIER 1.0f   // Use P2 quantile as-is
#define DISARM_THRESHOLD_MULTIPLIER 0.7f // Hysteresis

// Initialize all components (call once at startup)
void eps_fdir_init(void) {
    // Initialize feature buffers
    eps_init_buffers(&buffers);
    
    // Initialize bias corrector
    // alpha=0.01 means ~100 samples memory (~8 minutes at 5s sampling)
    bias_init(&bias_corrector, 0.01f, 50);
    
    // Initialize adaptive threshold trackers
    p2_init(&p2_power, 0.99f);    // Track 99th percentile
    p2_init(&p2_voltage, 0.99f);
    
    // Initialize logic state
    logic_state.armed_power = false;
    logic_state.armed_voltage = false;
    logic_state.consecutive_power = 0;
    logic_state.consecutive_voltage = 0;
    logic_state.trip_count_power = 0;
    logic_state.trip_count_voltage = 0;
}

// Main FDIR step (call every 5 seconds)
void eps_fdir_step(float power_reading, float voltage_reading) {
    
    // ===== 1. UPDATE RING BUFFERS =====
    eps_update_buffers(&buffers, power_reading, voltage_reading);
    
    // ===== 2. EXTRACT FEATURES =====
    double power_features[POWER_N_FEATURES];
    double voltage_features[VOLTAGE_N_FEATURES];
    
    eps_extract_power_features(&buffers, power_features);
    eps_extract_voltage_features(&buffers, voltage_features);
    
    // ===== 3. PREDICT (RAW MODEL OUTPUT) =====
    float y_pred_power = (float)predict_power(power_features);
    float y_pred_voltage = (float)predict_voltage(voltage_features);
    
    // ===== 4. APPLY BIAS CORRECTION =====
    if (bias_is_ready(&bias_corrector)) {
        bias_correct(&bias_corrector, &y_pred_power, &y_pred_voltage);
    }
    
    // ===== 5. COMPUTE RESIDUALS =====
    float residual_power = fabsf(power_reading - y_pred_power);
    float residual_voltage = fabsf(voltage_reading - y_pred_voltage);
    
    // ===== 6. UPDATE ADAPTIVE THRESHOLDS =====
    p2_update(&p2_power, residual_power);
    p2_update(&p2_voltage, residual_voltage);
    
    float threshold_arm_power = p2_get_quantile(&p2_power) * ARM_THRESHOLD_MULTIPLIER;
    float threshold_disarm_power = threshold_arm_power * DISARM_THRESHOLD_MULTIPLIER;
    
    float threshold_arm_voltage = p2_get_quantile(&p2_voltage) * ARM_THRESHOLD_MULTIPLIER;
    float threshold_disarm_voltage = threshold_arm_voltage * DISARM_THRESHOLD_MULTIPLIER;
    
    // ===== 7. LOGIC BLOCK WITH HYSTERESIS =====
    
    // Power channel
    if (!logic_state.armed_power && residual_power > threshold_arm_power) {
        logic_state.armed_power = true;
        logic_state.consecutive_power = 1;
    } else if (logic_state.armed_power) {
        if (residual_power > threshold_arm_power) {
            logic_state.consecutive_power++;
        } else if (residual_power < threshold_disarm_power) {
            logic_state.armed_power = false;
            logic_state.consecutive_power = 0;
        }
    }
    
    // Voltage channel (same logic)
    if (!logic_state.armed_voltage && residual_voltage > threshold_arm_voltage) {
        logic_state.armed_voltage = true;
        logic_state.consecutive_voltage = 1;
    } else if (logic_state.armed_voltage) {
        if (residual_voltage > threshold_arm_voltage) {
            logic_state.consecutive_voltage++;
        } else if (residual_voltage < threshold_disarm_voltage) {
            logic_state.armed_voltage = false;
            logic_state.consecutive_voltage = 0;
        }
    }
    
    // ===== 8. TRIP DECISION (CONSECUTIVE GATE) =====
    bool trip_power = (logic_state.consecutive_power >= GATE_N);
    bool trip_voltage = (logic_state.consecutive_voltage >= GATE_N);
    
    if (trip_power) {
        logic_state.trip_count_power++;
        // TRIGGER HARDWARE ACTION: Set GPIO to disable power panel
        // gpio_set_output(POWER_RELAY_PIN, GPIO_HIGH);
        
        // Log event
        // log_anomaly("POWER_TRIP", power_reading, y_pred_power, residual_power);
        
        // Reset gate (or latch - choose based on requirements)
        logic_state.armed_power = false;
        logic_state.consecutive_power = 0;
    }
    
    if (trip_voltage) {
        logic_state.trip_count_voltage++;
        // TRIGGER HARDWARE ACTION: Set comparator threshold
        // dac_set_voltage(COMPARATOR_DAC, voltage_reading * 0.8f);
        
        // Log event
        // log_anomaly("VOLTAGE_TRIP", voltage_reading, y_pred_voltage, residual_voltage);
        
        // Reset gate
        logic_state.armed_voltage = false;
        logic_state.consecutive_voltage = 0;
    }
    
    // ===== 9. UPDATE BIAS CORRECTOR (AFTER OBSERVATION) =====
    bias_update(&bias_corrector, 
                power_reading, y_pred_power,
                voltage_reading, y_pred_voltage);
    
    // ===== 10. TELEMETRY LOGGING (OPTIONAL) =====
    // Log to SD card or telemetry buffer for downlink:
    // - timestamp
    // - power_reading, voltage_reading
    // - y_pred_power, y_pred_voltage
    // - residual_power, residual_voltage
    // - threshold_arm_power, threshold_arm_voltage
    // - logic_state flags
}

// Periodic save to non-volatile memory (call every 10 minutes)
void eps_fdir_save_state(void) {
    // Save to FRAM/EEPROM:
    // - bias_corrector state (32 bytes)
    // - p2_power state (80 bytes)
    // - p2_voltage state (80 bytes)
    // - logic_state (16 bytes)
    // Total: ~208 bytes
    
    // Example (pseudo-code):
    // eeprom_write(ADDR_BIAS, &bias_corrector, sizeof(BiasCorrector));
    // eeprom_write(ADDR_P2_POWER, &p2_power, sizeof(P2Quantile));
    // eeprom_write(ADDR_P2_VOLTAGE, &p2_voltage, sizeof(P2Quantile));
    // eeprom_write(ADDR_LOGIC, &logic_state, sizeof(EPS_LogicState));
}

// Restore state from non-volatile memory (call at startup after init)
void eps_fdir_restore_state(void) {
    // eeprom_read(ADDR_BIAS, &bias_corrector, sizeof(BiasCorrector));
    // eeprom_read(ADDR_P2_POWER, &p2_power, sizeof(P2Quantile));
    // eeprom_read(ADDR_P2_VOLTAGE, &p2_voltage, sizeof(P2Quantile));
    // eeprom_read(ADDR_LOGIC, &logic_state, sizeof(EPS_LogicState));
}

// Main function example
int main(void) {
    // Hardware initialization (clocks, ADC, GPIO, etc.)
    // ...
    
    // EPS FDIR initialization
    eps_fdir_init();
    
    // Try to restore previous state (survives reboots)
    eps_fdir_restore_state();
    
    // Main loop
    uint32_t last_sample_time = 0;
    uint32_t last_save_time = 0;
    
    while (1) {
        uint32_t now = get_system_time_ms();
        
        // Sample every 5 seconds
        if (now - last_sample_time >= 5000) {
            last_sample_time = now;
            
            // Read sensors (ADC)
            float power = read_power_adc();      // Convert ADC to micro-watts
            float voltage = read_voltage_adc();  // Convert ADC to milli-volts
            
            // Run FDIR step
            eps_fdir_step(power, voltage);
        }
        
        // Save state every 10 minutes
        if (now - last_save_time >= 600000) {
            last_save_time = now;
            eps_fdir_save_state();
        }
        
        // Other tasks...
    }
    
    return 0;
}
