/**
 * EPS Predictive FDIR - Final Protection Logic Implementation
 * Complete system for 13-panel monitoring and protection
 */

#include "eps_protection_final.h"
#include <stdio.h>
#include <stdarg.h>

// ===== EXTERNAL DEPENDENCIES =====
// ADC handle (must be initialized in main.c)
extern ADC_HandleTypeDef hadc1;

// ===== GLOBAL STATE =====
PanelProtection_t panels[NUM_PANELS];

// ===== GPIO PIN MAPPINGS (EXAMPLE - Adjust for your hardware) =====
// These would be defined based on your actual STM32 pin assignments
// For now, using placeholder arrays

// Ground command buffer (set via UART/I2C from ground station)
static GroundCommand_t ground_commands[NUM_PANELS] = {CMD_NONE};

// ===== INITIALIZATION =====

void eps_protection_init(void) {
    // Initialize all panels with default values
    for (uint8_t i = 0; i < NUM_PANELS; i++) {
        panels[i].state = COMP_DISABLED;
        panels[i].last_enable_time = 0;
        panels[i].trip_time = 0;
        panels[i].last_log_time = 0;
        panels[i].stable_count = 0;
        panels[i].P_prev = 0.0f;
        panels[i].V_prev = 0.0f;
        panels[i].hardware_tripped = false;
        panels[i].ground_approved = false;
        panels[i].P_nominal = 8.4f;  // Default, override per panel
        panels[i].V_nominal = 17.5f; // Default, override per panel
        panels[i].enable_count = 0;
        panels[i].trip_count = 0;
        panels[i].false_alarm_count = 0;
        
        // Disable Layer 2 comparator initially (Layer 1 always on)
        disable_layer2_comparator(i);
    }
    
    log_event("EPS Protection System Initialized (%d panels)", NUM_PANELS);
}

void eps_protection_init_panel(uint8_t panel_id, float P_nom, float V_nom) {
    if (panel_id >= NUM_PANELS) return;
    
    panels[panel_id].P_nominal = P_nom;
    panels[panel_id].V_nominal = V_nom;
    
    log_event("Panel %d: P_nom=%.2fW, V_nom=%.2fV", panel_id, P_nom, V_nom);
}

// ===== MAIN PROTECTION LOGIC =====

void eps_protection_update(uint8_t panel_id,
                           float P_measured,
                           float V_measured,
                           float P_predicted,
                           float V_predicted) {
    
    if (panel_id >= NUM_PANELS) return;
    
    PanelProtection_t* panel = &panels[panel_id];
    
    // ===== COMPUTE DERIVATIVES =====
    float dP_dt = (P_measured - panel->P_prev) / 5.0f;  // Per second (5s sampling)
    float dV_dt = (V_measured - panel->V_prev) / 5.0f;
    
    // Update history
    panel->P_prev = P_measured;
    panel->V_prev = V_measured;
    
    // ===== COMPUTE RESIDUAL =====
    float residual_power = P_measured - P_predicted;
    
    // ===== CHECK 4 CONDITIONS =====
    
    // Condition 1: Power spike (unpredicted high power)
    bool power_spike = (P_predicted > panel->P_nominal * POWER_SPIKE_MULT);
    
    // Condition 2: Voltage drop (unexpected voltage decrease)
    bool voltage_drop = (V_measured < V_predicted - VOLTAGE_DROP_THRESH);
    
    // Condition 3: High dynamics (large rate of change)
    bool high_dynamics = (fabsf(dP_dt) > DP_DT_THRESH) && 
                        (fabsf(dV_dt) > DV_DT_THRESH);
    
    // Condition 4: Large residual (prediction error)
    bool large_residual = (fabsf(residual_power) > RESIDUAL_MULT * SIGMA_POWER);
    
    // Count conditions (need 2 of 4 to trigger)
    uint8_t condition_count = power_spike + voltage_drop + 
                             high_dynamics + large_residual;
    
    bool anomaly_detected = (condition_count >= 2);
    
    // ===== STATE MACHINE =====
    
    switch(panel->state) {
        
        case COMP_DISABLED: {
            // ===== NORMAL OPERATION =====
            // Layer 1 always monitoring, Layer 2 disabled
            
            if (anomaly_detected) {
                // SINGLE SAMPLE TRIGGER - Enable Layer 2 immediately
                enable_layer2_comparator(panel_id);
                
                panel->state = COMP_ENABLED;
                panel->last_enable_time = HAL_GetTick();
                panel->stable_count = 0;
                panel->enable_count++;
                
                log_event("Panel %d: Layer 2 ENABLED (%d/4 conditions met)",
                         panel_id, condition_count);
                log_conditions(power_spike, voltage_drop, high_dynamics, large_residual);
            }
            
            break;
        }
        
        case COMP_ENABLED: {
            // ===== LAYER 2 ACTIVE =====
            // Hardware monitoring, waiting for trip or stability
            
            // Check if hardware tripped (Layer 1 or Layer 2)
            bool mosfet_open = check_mosfet_status(panel_id);
            
            if (mosfet_open) {
                // === HARDWARE TRIP OCCURRED ===
                panel->state = COMP_TRIPPED;
                panel->hardware_tripped = true;
                panel->trip_time = HAL_GetTick();
                panel->trip_count++;
                
                log_event("Panel %d: HARDWARE TRIP (isolated)", panel_id);
                log_event("  P_measured=%.2fW, V_measured=%.2fV", P_measured, V_measured);
                log_event("  P_predicted=%.2fW, V_predicted=%.2fV", P_predicted, V_predicted);
                log_event("  Residual=%.2fW, dP/dt=%.3f, dV/dt=%.3f", 
                         residual_power, dP_dt, dV_dt);
                
                send_telemetry_alert(panel_id, P_measured, V_measured);
            }
            else if (!anomaly_detected) {
                // Anomaly cleared - check stability
                panel->stable_count++;
                
                if (panel->stable_count >= STABLE_REQUIRED) {
                    // === FALSE ALARM - DISABLE LAYER 2 ===
                    disable_layer2_comparator(panel_id);
                    
                    panel->state = COMP_DISABLED;
                    panel->stable_count = 0;
                    panel->false_alarm_count++;
                    
                    log_event("Panel %d: Layer 2 DISABLED (stable 30s, false alarm)",
                             panel_id);
                }
            } else {
                // Still anomalous, reset counter
                panel->stable_count = 0;
            }
            
            // === SAFETY TIMEOUT (5 minutes without trip) ===
            uint32_t time_enabled = HAL_GetTick() - panel->last_enable_time;
            if (time_enabled > ENABLE_TIMEOUT_MS && !panel->hardware_tripped) {
                disable_layer2_comparator(panel_id);
                
                panel->state = COMP_DISABLED;
                panel->false_alarm_count++;
                
                log_event("Panel %d: TIMEOUT (5min, no trip, false alarm)", panel_id);
            }
            
            break;
        }
        
        case COMP_TRIPPED: {
            // ===== PANEL ISOLATED =====
            // Waiting for ground station command to re-enable
            
            // Periodic logging (every 60 seconds)
            uint32_t now = HAL_GetTick();
            if ((now - panel->last_log_time) > 60000) {
                panel->last_log_time = now;
                
                float I_measured = (V_measured > 0.1f) ? (P_measured / V_measured) : 0.0f;
                
                log_event("Panel %d: Still isolated (awaiting ground command)", panel_id);
                log_event("  V=%.2fV, I=%.3fA, P=%.2fW", V_measured, I_measured, P_measured);
                
                send_telemetry(panel_id, V_measured, I_measured, P_measured);
            }
            
            // Check for ground station command
            if (check_ground_command(panel_id, CMD_REENABLE)) {
                panel->ground_approved = true;
                panel->state = COMP_RECOVERY;
                panel->stable_count = 0;
                
                log_event("Panel %d: Ground approved re-enable (monitoring 2min)",
                         panel_id);
                
                attempt_reenable_mosfet(panel_id);
                
                // Clear command
                ground_commands[panel_id] = CMD_NONE;
            }
            
            break;
        }
        
        case COMP_RECOVERY: {
            // ===== MONITORING AFTER RE-ENABLE =====
            // Check if panel stable or fault returns
            
            if (anomaly_detected) {
                // === RECOVERY FAILED ===
                panel->state = COMP_TRIPPED;
                panel->trip_time = HAL_GetTick();
                panel->stable_count = 0;
                
                disable_mosfet(panel_id);
                
                log_event("Panel %d: RECOVERY FAILED (anomaly returned)", panel_id);
                send_telemetry_alert(panel_id, P_measured, V_measured);
            }
            else {
                // Check stability
                panel->stable_count++;
                
                if (panel->stable_count >= RECOVERY_STABLE_REQ) {
                    // === RECOVERY SUCCESS ===
                    panel->state = COMP_DISABLED;
                    panel->stable_count = 0;
                    panel->ground_approved = false;
                    
                    disable_layer2_comparator(panel_id);
                    
                    log_event("Panel %d: RECOVERY SUCCESS (stable 2min)", panel_id);
                    send_telemetry_success(panel_id);
                }
            }
            
            break;
        }
    }
}

// ===== HARDWARE CONFIGURATION =====
// GPIO Pin Mappings for 13 Panels
// Each panel requires:
//   1. Layer 2 Comparator Enable (Digital Output)
//   2. MOSFET Status Sense (ADC Input - drain voltage)
//   3. MOSFET Control Override (Digital Output - for recovery)

// STM32F4 GPIO mappings (adjust for your hardware)
static const struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} LAYER2_ENABLE_PINS[NUM_PANELS] = {
    {GPIOA, GPIO_PIN_0},  {GPIOA, GPIO_PIN_1},  {GPIOA, GPIO_PIN_2},  {GPIOA, GPIO_PIN_3},
    {GPIOA, GPIO_PIN_4},  {GPIOA, GPIO_PIN_5},  {GPIOA, GPIO_PIN_6},  {GPIOA, GPIO_PIN_7},
    {GPIOB, GPIO_PIN_0},  {GPIOB, GPIO_PIN_1},  {GPIOB, GPIO_PIN_2},  {GPIOB, GPIO_PIN_3},
    {GPIOB, GPIO_PIN_4}
};

static const struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} MOSFET_OVERRIDE_PINS[NUM_PANELS] = {
    {GPIOC, GPIO_PIN_0},  {GPIOC, GPIO_PIN_1},  {GPIOC, GPIO_PIN_2},  {GPIOC, GPIO_PIN_3},
    {GPIOC, GPIO_PIN_4},  {GPIOC, GPIO_PIN_5},  {GPIOC, GPIO_PIN_6},  {GPIOC, GPIO_PIN_7},
    {GPIOD, GPIO_PIN_0},  {GPIOD, GPIO_PIN_1},  {GPIOD, GPIO_PIN_2},  {GPIOD, GPIO_PIN_3},
    {GPIOD, GPIO_PIN_4}
};

// ADC channels for MOSFET drain voltage sensing
static const uint32_t MOSFET_SENSE_ADC_CHANNELS[NUM_PANELS] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
    ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7,
    ADC_CHANNEL_8, ADC_CHANNEL_9, ADC_CHANNEL_10, ADC_CHANNEL_11,
    ADC_CHANNEL_12
};

// ===== HARDWARE INTERFACE IMPLEMENTATION =====

void enable_layer2_comparator(uint8_t panel_id) {
    if (panel_id >= NUM_PANELS) return;
    
    // Set GPIO HIGH to enable Layer 2 comparator (1.2×P_nominal threshold)
    // This connects the comparator output to the OR gate controlling MOSFET
    HAL_GPIO_WritePin(LAYER2_ENABLE_PINS[panel_id].port, 
                      LAYER2_ENABLE_PINS[panel_id].pin, 
                      GPIO_PIN_SET);
    
    // Small delay for comparator stabilization
    HAL_Delay(1);
}

void disable_layer2_comparator(uint8_t panel_id) {
    if (panel_id >= NUM_PANELS) return;
    
    // Set GPIO LOW to disable Layer 2 comparator
    // Only Layer 1 (2×P_nominal, always-on) remains active
    HAL_GPIO_WritePin(LAYER2_ENABLE_PINS[panel_id].port, 
                      LAYER2_ENABLE_PINS[panel_id].pin, 
                      GPIO_PIN_RESET);
}

bool check_mosfet_status(uint8_t panel_id) {
    if (panel_id >= NUM_PANELS) return false;
    
    // Read MOSFET drain voltage via ADC
    // Circuit: If MOSFET open (tripped), drain voltage ≈ 0V
    //          If MOSFET closed (normal), drain voltage ≈ bus voltage (scaled)
    
    // Select ADC channel
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = MOSFET_SENSE_ADC_CHANNELS[panel_id];
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    // Start conversion
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    
    // Convert to voltage (0-3.3V range, assuming voltage divider)
    float drain_voltage = (adc_value / 4095.0f) * 3.3f;
    
    // Threshold: If drain voltage < 1.0V, MOSFET is open (tripped)
    bool mosfet_open = (drain_voltage < 1.0f);
    
    return mosfet_open;
}

void attempt_reenable_mosfet(uint8_t panel_id) {
    if (panel_id >= NUM_PANELS) return;
    
    // Set MOSFET override GPIO HIGH to close MOSFET (bypass comparators)
    // This is used during ground-approved recovery testing
    HAL_GPIO_WritePin(MOSFET_OVERRIDE_PINS[panel_id].port,
                      MOSFET_OVERRIDE_PINS[panel_id].pin,
                      GPIO_PIN_SET);
    
    // Allow MOSFET to close (inrush current settling)
    HAL_Delay(10);
    
    log_event("Panel %d: MOSFET re-enabled (override active)", panel_id);
}

void disable_mosfet(uint8_t panel_id) {
    if (panel_id >= NUM_PANELS) return;
    
    // Set MOSFET override GPIO LOW to open MOSFET
    // This manually isolates the panel
    HAL_GPIO_WritePin(MOSFET_OVERRIDE_PINS[panel_id].port,
                      MOSFET_OVERRIDE_PINS[panel_id].pin,
                      GPIO_PIN_RESET);
    
    log_event("Panel %d: MOSFET manually disabled", panel_id);
}

// ===== GROUND COMMANDS =====

bool check_ground_command(uint8_t panel_id, GroundCommand_t cmd) {
    if (panel_id >= NUM_PANELS) return false;
    return (ground_commands[panel_id] == cmd);
}

void process_ground_command(uint8_t panel_id, GroundCommand_t cmd) {
    if (panel_id >= NUM_PANELS) return;
    
    ground_commands[panel_id] = cmd;
    
    log_event("Panel %d: Ground command received: %d", panel_id, cmd);
}

// ===== TELEMETRY =====

void log_event(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // In real system: Send to UART, log to SD card, add to telemetry buffer
    printf("[EPS_FDIR] %s\n", buffer);
}

void log_conditions(bool power_spike, bool voltage_drop, 
                   bool high_dynamics, bool large_residual) {
    log_event("  Conditions: Power_spike=%s, Voltage_drop=%s, High_dynamics=%s, Large_residual=%s",
             power_spike ? "YES" : "no",
             voltage_drop ? "YES" : "no",
             high_dynamics ? "YES" : "no",
             large_residual ? "YES" : "no");
}

void send_telemetry(uint8_t panel_id, float V, float I, float P) {
    // Send to telemetry downlink buffer
    // Format: panel_id, timestamp, V, I, P, state
    
    log_event("Telemetry: Panel %d: V=%.2fV, I=%.3fA, P=%.2fW, State=%s",
             panel_id, V, I, P, state_to_string(panels[panel_id].state));
}

void send_telemetry_alert(uint8_t panel_id, float P, float V) {
    // High-priority alert to ground station
    log_event("ALERT: Panel %d TRIPPED - P=%.2fW, V=%.2fV", panel_id, P, V);
}

void send_telemetry_success(uint8_t panel_id) {
    log_event("SUCCESS: Panel %d recovery complete", panel_id);
}

// ===== UTILITY FUNCTIONS =====

const char* state_to_string(ComparatorState_t state) {
    switch(state) {
        case COMP_DISABLED: return "DISABLED";
        case COMP_ENABLED: return "ENABLED";
        case COMP_TRIPPED: return "TRIPPED";
        case COMP_RECOVERY: return "RECOVERY";
        default: return "UNKNOWN";
    }
}

void get_panel_statistics(uint8_t panel_id, 
                         uint32_t* enable_count,
                         uint32_t* trip_count,
                         uint32_t* false_alarm_count) {
    if (panel_id >= NUM_PANELS) return;
    
    *enable_count = panels[panel_id].enable_count;
    *trip_count = panels[panel_id].trip_count;
    *false_alarm_count = panels[panel_id].false_alarm_count;
}

// ===== PLACEHOLDER FOR HAL_GetTick() =====
// In real STM32, this is provided by HAL
// For testing, we need a simple implementation
#ifndef HAL_GetTick
static uint32_t sim_tick = 0;
uint32_t HAL_GetTick(void) {
    return sim_tick;
}
void sim_advance_time(uint32_t ms) {
    sim_tick += ms;
}
#endif
