# EPS Dual-Layer Protection Circuit Design

## Overview
Each of the 13 solar panels has an independent dual-layer hardware protection circuit controlled by the STM32F4 MCU.

---

## Circuit Architecture (Per Panel)

```
Solar Panel (+) ──────┬─── Layer 1 Comparator (2×P_nom, Always-On)
                      │
                      ├─── Layer 2 Comparator (1.2×P_nom, MCU-Gated)
                      │
                      └─── Current Sense (Shunt Resistor)
                            │
                            V
                      ┌─────────┐
                      │ OR Gate │
                      └────┬────┘
                           │
                      ┌────▼────────┐
                      │  P-Channel  │
                      │   MOSFET    │◄── MCU Override (Recovery)
                      └────┬────────┘
                           │
                           ├─── MOSFET Drain Sense (ADC)
                           │
                           V
                      To Bus/Battery
```

---

## Component Details

### 1. Layer 1 Comparator (Always-On, Catastrophic Protection)
**Purpose:** Fast hardware protection against catastrophic failures (short circuits, arc faults)

**Circuit:**
```
V_panel ──[Voltage Divider]──┬── Comparator (+)
                              │
                              ├── Reference: 2×P_nominal (fixed)
                              │
                              └── Output ──► OR Gate Input 1
```

**Configuration:**
- Threshold: `V_threshold = 2 × P_nominal / I_nominal`
- For 8.4W panel @ 17.5V: `V_threshold = 2×8.4/0.48 = 35V`
- Response time: < 1μs
- **Always enabled** - no MCU control

**Implementation:**
- IC: LM339 or TLV3502 (low-power comparator)
- Reference: Precision voltage divider + zener diode
- Output: Open-drain → pull-up resistor to 3.3V

---

### 2. Layer 2 Comparator (AI-Gated, Pre-Failure Detection)
**Purpose:** Early anomaly detection based on AI predictions

**Circuit:**
```
V_panel ──[Voltage Divider]──┬── Comparator (+)
                              │
                              ├── Reference: 1.2×P_nominal (fixed)
                              │
                              └── Output ──┬── AND Gate Input 1
                                           │
MCU GPIO (PA0-PB4) ─────────────────────► AND Gate Input 2
                                           │
                                           └─► OR Gate Input 2
```

**Configuration:**
- Threshold: `V_threshold = 1.2 × P_nominal / I_nominal`
- For 8.4W panel: `V_threshold = 1.2×8.4/0.48 = 21V`
- **MCU-controlled enable** via GPIO
- Response time: < 10μs (comparator + logic gate)

**Implementation:**
- IC: Same comparator IC as Layer 1
- Enable control: 74HC08 AND gate (MCU GPIO + comparator output)
- MCU GPIO HIGH → Layer 2 active
- MCU GPIO LOW → Layer 2 disabled

---

### 3. MOSFET Switch (Panel Isolation)
**Purpose:** Disconnect panel from bus when fault detected

**Circuit:**
```
                      Vcc (+)
                        │
                        ├──[10kΩ]── Gate
                        │
                 ┌──────┴──────┐
                 │  P-Channel  │
Panel (+) ───────│   MOSFET    │──── To Bus
                 │  (Si2301)   │
                 └─────────────┘
                        │
                   Gate Control
                        │
                 ┌──────┴──────┐
                 │  NOR Gate   │
                 │  (OR Gate   │
                 │   inverted) │
                 └──┬─────┬────┘
                    │     │
              Layer1  Layer2
              Output  Output
```

**Configuration:**
- MOSFET: P-channel logic-level (Si2301, AO3401, or similar)
- V_DS(max): 30V minimum
- I_D(continuous): 2A minimum
- R_DS(on): < 100mΩ

**Operation:**
- Normal: Gate = Vcc (MOSFET ON, panel connected)
- Trip: Gate = 0V (MOSFET OFF, panel isolated)
- OR gate output HIGH → MOSFET gate LOW → Panel isolated

---

### 4. MOSFET Override (Recovery Control)
**Purpose:** Ground-commanded re-enable bypassing comparators

**Circuit:**
```
MCU GPIO (PC0-PD4) ──┬── Override Enable
                     │
                     ├──[AND Gate]── Gate Driver
                     │
                OR Gate Output ────┘
```

**Implementation:**
- Override GPIO HIGH + OR gate LOW → MOSFET ON (forced recovery)
- Priority: Override > Comparators (for recovery testing)

---

### 5. Current Sensing (Power Calculation)
**Purpose:** Measure panel current for power/voltage monitoring

**Circuit:**
```
Panel (+) ──┬── [R_shunt = 0.1Ω] ──┬── To MOSFET
            │                       │
            └─── INA219 I²C ────────┘
                 (MCU reads I, V, P)
```

**Configuration:**
- Shunt resistor: 0.1Ω, 1W, 1% tolerance
- IC: INA219 or INA226 (I²C current/voltage monitor)
- MCU reads: `I_panel`, `V_panel`, `P_panel = V × I`
- Sampling: Every 5 seconds

**Alternative (ADC-only):**
```
V_shunt = I_panel × R_shunt → I_panel = V_shunt / 0.1Ω
ADC_IN (0-3.3V) ← Op-amp (gain = 10) ← V_shunt
```

---

### 6. MOSFET Drain Sensing (Trip Detection)
**Purpose:** MCU detects hardware trip by reading drain voltage

**Circuit:**
```
MOSFET Drain ──[Voltage Divider 10:1]── ADC_IN (ADC_CH0-CH12)
                                         │
                                         └── 0V (open) / 3.3V (closed)
```

**Configuration:**
- Voltage divider: R1=100kΩ, R2=10kΩ (10:1 ratio)
- ADC range: 0-3.3V (mapped from 0-33V drain voltage)
- Detection logic:
  - `drain_voltage < 1.0V` → MOSFET open (tripped)
  - `drain_voltage > 2.0V` → MOSFET closed (normal)

---

## STM32F4 Pin Assignments (13 Panels)

### GPIO Outputs - Layer 2 Comparator Enable
| Panel | GPIO Pin | Function |
|-------|----------|----------|
| 0     | PA0      | Layer2_Enable_Panel0 |
| 1     | PA1      | Layer2_Enable_Panel1 |
| 2     | PA2      | Layer2_Enable_Panel2 |
| 3     | PA3      | Layer2_Enable_Panel3 |
| 4     | PA4      | Layer2_Enable_Panel4 |
| 5     | PA5      | Layer2_Enable_Panel5 |
| 6     | PA6      | Layer2_Enable_Panel6 |
| 7     | PA7      | Layer2_Enable_Panel7 |
| 8     | PB0      | Layer2_Enable_Panel8 |
| 9     | PB1      | Layer2_Enable_Panel9 |
| 10    | PB2      | Layer2_Enable_Panel10 |
| 11    | PB3      | Layer2_Enable_Panel11 |
| 12    | PB4      | Layer2_Enable_Panel12 |

### GPIO Outputs - MOSFET Override Control
| Panel | GPIO Pin | Function |
|-------|----------|----------|
| 0     | PC0      | MOSFET_Override_Panel0 |
| 1     | PC1      | MOSFET_Override_Panel1 |
| 2     | PC2      | MOSFET_Override_Panel2 |
| 3     | PC3      | MOSFET_Override_Panel3 |
| 4     | PC4      | MOSFET_Override_Panel4 |
| 5     | PC5      | MOSFET_Override_Panel5 |
| 6     | PC6      | MOSFET_Override_Panel6 |
| 7     | PC7      | MOSFET_Override_Panel7 |
| 8     | PD0      | MOSFET_Override_Panel8 |
| 9     | PD1      | MOSFET_Override_Panel9 |
| 10    | PD2      | MOSFET_Override_Panel10 |
| 11    | PD3      | MOSFET_Override_Panel11 |
| 12    | PD4      | MOSFET_Override_Panel12 |

### ADC Inputs - MOSFET Drain Voltage Sensing
| Panel | ADC Channel | Physical Pin |
|-------|-------------|--------------|
| 0     | ADC1_IN0    | PA0 (ADC)    |
| 1     | ADC1_IN1    | PA1 (ADC)    |
| 2     | ADC1_IN2    | PA2 (ADC)    |
| 3     | ADC1_IN3    | PA3 (ADC)    |
| 4     | ADC1_IN4    | PA4 (ADC)    |
| 5     | ADC1_IN5    | PA5 (ADC)    |
| 6     | ADC1_IN6    | PA6 (ADC)    |
| 7     | ADC1_IN7    | PA7 (ADC)    |
| 8     | ADC1_IN8    | PB0 (ADC)    |
| 9     | ADC1_IN9    | PB1 (ADC)    |
| 10    | ADC1_IN10   | PC0 (ADC)    |
| 11    | ADC1_IN11   | PC1 (ADC)    |
| 12    | ADC1_IN12   | PC2 (ADC)    |

### I²C - Current/Voltage Sensors (Optional)
| Bus  | Pins     | Devices |
|------|----------|---------|
| I2C1 | PB6/PB7  | 13× INA219 (addresses 0x40-0x4C) |

---

## Power Budget

### Per Panel:
- Comparators: 2× 100μA = 200μA
- Logic gates: 50μA
- MOSFET leakage: 10μA
- **Total: ~260μA per panel**

### Full System (13 Panels):
- Protection circuits: 13× 260μA = 3.4mA
- STM32F4 active: ~80mA (168MHz, peripherals active)
- STM32F4 sleep: ~10mA (low-power mode between samples)
- **Total active: ~83mA**
- **Total sleep: ~13mA**

---

## Timing Analysis

### Response Times:
1. **Layer 1 trip**: < 1μs (comparator + logic gate + MOSFET switch)
2. **Layer 2 trip**: < 10μs (includes MCU enable signal propagation)
3. **MCU detection**: 5s (next sampling interval)
4. **Layer 2 enable decision**: 157μs (model inference)
5. **False alarm disable**: 30s (6× 5s samples)
6. **Timeout disable**: 5 minutes

### Critical Path (Anomaly Detection → Hardware Protection):
```
[t=0s]      Anomaly occurs
[t+5s]      MCU samples ADC
[t+5.0002s] Model inference complete (157μs)
[t+5.0002s] Trigger conditions evaluated (2/4 met)
[t+5.0002s] GPIO set HIGH (Layer 2 enabled)
[t+5.001s]  Layer 2 comparator armed (<1ms)
[t+5.001s]  Hardware monitors in real-time
[t+5.001s+] Trip occurs if threshold exceeded (<10μs)
```

---

## Testing & Validation

### 1. Layer 1 Test (Catastrophic Protection)
```c
// Inject high current (short circuit simulation)
// Expected: Immediate trip (<1μs), no MCU involvement
test_layer1_trip(panel_id);
verify_mosfet_open(panel_id);  // Should be true within 1ms
```

### 2. Layer 2 Test (AI-Gated Protection)
```c
// Inject moderate anomaly (1.2× P_nominal)
// Expected: MCU enables Layer 2 → Hardware trip
enable_layer2_comparator(panel_id);
inject_anomaly(panel_id, 1.3 * P_nominal);
verify_mosfet_open(panel_id);  // Should be true within 100ms
```

### 3. False Alarm Test
```c
// Inject brief anomaly, then clear
// Expected: Layer 2 enabled → 30s stable → disabled
inject_transient_anomaly(panel_id);
wait(30s);
verify_layer2_disabled(panel_id);  // Should auto-disable
```

### 4. Recovery Test
```c
// Ground command re-enable
// Expected: MOSFET closes, 2min monitoring, success
process_ground_command(panel_id, CMD_REENABLE);
verify_mosfet_closed(panel_id);
wait(120s);
verify_state(panel_id, COMP_DISABLED);  // Recovery success
```

---

## Bill of Materials (BOM) - Per Panel

| Component | Part Number | Qty | Unit Cost | Total |
|-----------|-------------|-----|-----------|-------|
| Comparator (dual) | LM339 | 1 | $0.50 | $0.50 |
| P-Channel MOSFET | Si2301 | 1 | $0.30 | $0.30 |
| Logic Gate (Quad AND) | 74HC08 | 0.25 | $0.20 | $0.05 |
| Logic Gate (Quad OR) | 74HC32 | 0.25 | $0.20 | $0.05 |
| Current Sensor (optional) | INA219 | 1 | $2.50 | $2.50 |
| Shunt Resistor 0.1Ω | - | 1 | $0.20 | $0.20 |
| Resistors/Caps/Misc | - | - | $0.50 | $0.50 |
| **Per-Panel Subtotal** | | | | **$4.10** |
| **13-Panel Total** | | | | **$53.30** |

---

## Competition Requirements Checklist

✅ **Dual-layer protection**: Layer 1 (always-on) + Layer 2 (AI-gated)  
✅ **Fast response**: <10μs hardware trip time  
✅ **13 independent panels**: Each with isolated protection  
✅ **Ground-commanded recovery**: Manual re-enable with monitoring  
✅ **False alarm mitigation**: 30s stability check, 5min timeout  
✅ **Telemetry**: Real-time status, statistics, alerts  
✅ **Low power**: 3.4mA for all protection circuits  
✅ **STM32F4 compatible**: 168MHz, 1MB Flash, 192KB RAM  

---

## Next Steps for Competition Submission

1. ✅ **Code implementation** - Complete (eps_protection_final.c, eps_main_deployment.c)
2. ✅ **Circuit design** - This document
3. ⏳ **Schematic diagram** - Create detailed Eagle/KiCAD schematic
4. ⏳ **PCB layout** - Route 13-panel protection board
5. ⏳ **Clean notebook** - Remove RAAVANA references, focus on NEPALISAT
6. ⏳ **PDF report** - 3-page technical summary
7. ⏳ **README update** - Deployment instructions

---

**Document Version:** 1.0  
**Date:** November 10, 2025  
**Author:** EPS FDIR Team
