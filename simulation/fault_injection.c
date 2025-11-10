#include "fault_injection.h"
#include <math.h>

// Simple deterministic pseudo-random sequence for reproducibility
static uint32_t rng_state = 0x12345678;
static float frand(void){
    rng_state = 1664525u * rng_state + 1013904223u;
    return (rng_state >> 8) * (1.0f/16777216.0f); // ~0..1
}

// Apply fault transformation to nominal (P,V) pair
// Inputs/Outputs are in-place so caller sees modified values
void apply_fault(FaultScenario* sc, uint32_t step, float* P, float* V, float* I) {
    if (!sc) return;
    if (step < sc->start_step) return;
    if (sc->duration != 0 && step >= sc->start_step + sc->duration) return;

    float severity = sc->severity;
    switch(sc->type) {
        case FAULT_SHADE: {
            // Gradual linear decay of power only; voltage modestly affected
            float factor = 1.0f - severity * fminf(1.0f, (step - sc->start_step) / (float)fmaxf(1, sc->duration));
            *P *= factor;
            *I = (*P) / fmaxf(0.1f, *V); // recompute current
            break;
        }
        case FAULT_OPEN_CIRCUIT: {
            // Current collapses, voltage may float slightly above nominal due to no load
            *I *= (0.05f + 0.02f * severity);
            *P = (*V) * (*I);
            *V *= (1.0f + 0.05f * severity);
            break;
        }
        case FAULT_SHORT_CIRCUIT: {
            // Voltage collapses, current spikes momentarily
            *V *= (0.15f + 0.2f * (1.0f - severity));
            float I_spike = (*I) * (2.5f + 2.0f * severity);
            *P = (*V) * I_spike;
            *I = I_spike;
            break;
        }
        case FAULT_SENSOR_NOISE: {
            // Inject high-frequency noise with amplitude scaled by severity
            float noiseP = (frand()*2.0f - 1.0f) * severity * 0.3f * (*P + 1e-3f);
            float noiseV = (frand()*2.0f - 1.0f) * severity * 0.05f * (*V + 1e-3f);
            *P += noiseP;
            *V += noiseV;
            *I = (*P) / fmaxf(0.1f, *V);
            break;
        }
        default: break;
    }
}
