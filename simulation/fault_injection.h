#ifndef FAULT_INJECTION_H
#define FAULT_INJECTION_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FAULT_NONE = 0,
    FAULT_SHADE,           // Gradual power drop
    FAULT_OPEN_CIRCUIT,    // Voltage rises, current ~0
    FAULT_SHORT_CIRCUIT,   // Voltage collapses, current spikes
    FAULT_SENSOR_NOISE     // Spiky sensor readings
} FaultType;

// Scenario configuration per panel
typedef struct {
    uint8_t panel_id;
    FaultType type;
    uint32_t start_step;   // iteration when fault begins
    uint32_t duration;     // steps fault persists (0 = persistent)
    float severity;        // 0..1 scale, interpretation depends on type
} FaultScenario;

#endif // FAULT_INJECTION_H
