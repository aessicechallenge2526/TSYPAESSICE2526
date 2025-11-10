/**
 * EPS Predictive FDIR - Final Protection Logic
 * Dual-Layer Hardware Protection for 13 Solar Panels
 * 
 * Layer 1: Always-on comparator (catastrophic protection)
 * Layer 2: AI-gated comparator (pre-failure detection)
 * 
 * Target: STM32F4 or higher
 * Generated: 2025-11-10
 */

#ifndef EPS_PROTECTION_FINAL_H
#define EPS_PROTECTION_FINAL_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// ===== CONFIGURATION =====
#define NUM_PANELS 13

// Thresholds (learned from data)
#define SIGMA_POWER 0.5f           // Power prediction standard deviation (W)
#define SIGMA_VOLTAGE 0.4f         // Voltage prediction standard deviation (V)

// Timing parameters
#define ENABLE_TIMEOUT_MS 300000   // 5 minutes - disable if no trip
#define STABLE_REQUIRED 6          // 6 samples = 30s stable before disable
#define RECOVERY_STABLE_REQ 24     // 24 samples = 2min stable after re-enable

// Condition thresholds
#define POWER_SPIKE_MULT 1.2f      // P_predicted > 1.2 × P_nominal
#define VOLTAGE_DROP_THRESH 0.5f   // V_measured < V_predicted - 0.5V
#define DP_DT_THRESH 0.5f          // |dP/dt| > 0.5 W/s
#define DV_DT_THRESH 0.3f          // |dV/dt| > 0.3 V/s
#define RESIDUAL_MULT 3.0f         // |residual| > 3σ

// ===== STATE MACHINE =====
typedef enum {
    COMP_DISABLED = 0,    // Normal operation, MCU monitoring only
    COMP_ENABLED = 1,     // Layer 2 active (AI-gated), hardware monitoring
    COMP_TRIPPED = 2,     // Hardware isolated panel, awaiting ground command
    COMP_RECOVERY = 3     // Ground-approved re-enable, monitoring stability
} ComparatorState_t;

// ===== PER-PANEL STATE =====
typedef struct {
    // State
    ComparatorState_t state;
    
    // Timing
    uint32_t last_enable_time;     // When Layer 2 was enabled
    uint32_t trip_time;            // When hardware trip occurred
    uint32_t last_log_time;        // For periodic logging
    
    // Counters
    uint8_t stable_count;          // Consecutive stable samples
    
    // History (for derivatives)
    float P_prev;                  // Previous power measurement
    float V_prev;                  // Previous voltage measurement
    
    // Flags
    bool hardware_tripped;         // Has hardware isolated this panel?
    bool ground_approved;          // Ground station approved recovery?
    
    // Per-panel thresholds (from training data)
    float P_nominal;               // Nominal power (W)
    float V_nominal;               // Nominal voltage (V)
    
    // Statistics (for telemetry)
    uint32_t enable_count;         // How many times Layer 2 enabled
    uint32_t trip_count;           // How many hardware trips
    uint32_t false_alarm_count;    // False positives (timeout/stable)
    
} PanelProtection_t;

// ===== GLOBAL STATE =====
extern PanelProtection_t panels[NUM_PANELS];

// ===== INITIALIZATION =====
void eps_protection_init(void);
void eps_protection_init_panel(uint8_t panel_id, float P_nom, float V_nom);

// ===== MAIN PROTECTION LOGIC =====
void eps_protection_update(uint8_t panel_id,
                           float P_measured,
                           float V_measured,
                           float P_predicted,
                           float V_predicted);

// ===== HARDWARE INTERFACE =====
void enable_layer2_comparator(uint8_t panel_id);
void disable_layer2_comparator(uint8_t panel_id);
bool check_mosfet_status(uint8_t panel_id);
void attempt_reenable_mosfet(uint8_t panel_id);
void disable_mosfet(uint8_t panel_id);

// ===== GROUND COMMANDS =====
typedef enum {
    CMD_NONE = 0,
    CMD_REENABLE = 1,
    CMD_PERMANENT_DISABLE = 2,
    CMD_RESET_STATS = 3
} GroundCommand_t;

bool check_ground_command(uint8_t panel_id, GroundCommand_t cmd);
void process_ground_command(uint8_t panel_id, GroundCommand_t cmd);

// ===== TELEMETRY =====
void log_event(const char* format, ...);
void log_conditions(bool power_spike, bool voltage_drop, 
                   bool high_dynamics, bool large_residual);
void send_telemetry(uint8_t panel_id, float V, float I, float P);
void send_telemetry_alert(uint8_t panel_id, float P, float V);
void send_telemetry_success(uint8_t panel_id);

// ===== UTILITY FUNCTIONS =====
const char* state_to_string(ComparatorState_t state);
void get_panel_statistics(uint8_t panel_id, 
                         uint32_t* enable_count,
                         uint32_t* trip_count,
                         uint32_t* false_alarm_count);

#endif // EPS_PROTECTION_FINAL_H
