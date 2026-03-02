# Data Model

## InstrumentConfig

```cpp
struct InstrumentConfig {
    char name[32];               // Instrument name
    uint8_t midi_channel;        // MIDI channel (0-15)
    uint8_t bus_id;              // Dedicated I²C bus
    uint8_t actuator_ids[64];    // Associated actuator IDs
    uint8_t midi_notes[64];      // MIDI note for each actuator (0xFF = unmapped)
    uint8_t actuator_count;      // Number of actuators
    uint16_t default_latency_ms; // Default latency
    bool auto_calibration;       // Auto-calibration enabled
    bool enabled;                // Instrument active
};
```

* Up to **8 simultaneous instruments** on the same ESP32
* Each instrument has its own MIDI channel, I²C bus, CCs and calibration profile
* The global scheduler manages all instruments in parallel

## ActuatorConfig

```cpp
struct ActuatorConfig {
    uint8_t id;
    ActuatorType type;           // ACT_SERVO or ACT_SOLENOID
    uint8_t bus_id;              // I²C bus (0 or 1)
    uint8_t pca_address;         // PCA9685 address (0x40-0x43)
    uint8_t pca_channel;         // PCA channel (0-15)
    uint8_t behavior;            // Behavior mode

    // Servo
    uint16_t angle_initial;      // Rest angle (°)
    uint16_t amplitude;          // Movement amplitude (°)
    uint16_t speed_ms;           // Movement duration (ms)
    uint16_t angle_b;            // Angle B (alternate mode)
    bool hit_reverse;            // Strike direction

    // Solenoid
    uint16_t pulse_min_ms;       // Min strike duration (ms) — velocity 0
    uint16_t pulse_ms;           // Max strike duration (ms) — velocity 127
    uint16_t pwm_initial;        // Attack PWM (0-4095)
    uint16_t pwm_hold;           // Hold PWM (0-4095)
    uint16_t ramp_ms;            // Attack→hold ramp (ms)

    uint16_t latency_ms;         // Measured mechanical latency
    bool enabled;

    ActuatorState state;         // Runtime state (not saved)
};
```

### Servo Behaviors (4 modes)

| Mode | Enum | Movement |
|------|------|----------|
| **Strike** | `SERVO_FRAPPE` | A → A+X → return |
| **Alternate** | `SERVO_ALTERNE` | A → B → A → B |
| **Strum** | `SERVO_GRATTER` | A → ±X alternating on each note |
| **Key** | `SERVO_TOUCHE` | A → A±δ until note off |

### Solenoid Behaviors (2 modes)

| Mode | Enum | Operation |
|------|------|-----------|
| **Strike** | `SOL_FRAPPE` | 5–50 ms pulse based on velocity |
| **Hit-and-Hold** | `SOL_HIT_AND_HOLD` | Initial PWM → ramp → hold until note off |

## MIDI Routing Structures

```cpp
struct NoteMapping {             // MIDI note → actuator mapping
    uint8_t midi_note;           // MIDI note (0-127)
    uint8_t actuator_id;         // Target actuator ID
    uint8_t behavior_override;   // Specific behavior (0xFF = actuator default)
    bool enabled;
};

struct CCMapping {               // MIDI CC → actuator parameter mapping
    uint8_t cc_number;           // CC number (0-127)
    uint8_t actuator_id;         // Target actuator ID
    CCTarget target;             // CC_TARGET_POSITION / AMPLITUDE / SPEED / PWM_HOLD
    uint16_t range_min;          // Min output value
    uint16_t range_max;          // Max output value
    bool enabled;
};

struct VelocityCurvePoint {      // Velocity curve (5 points)
    uint8_t input;               // Input MIDI velocity (0-127)
    uint8_t output;              // Mapped output velocity (0-127)
};
```

## Safety and Protection

| Protection | Detail |
|-----------|--------|
| **Kill switch** | Disables OE pins on both buses — immediate hardware cutoff |
| **Current limit (Safety)** | Real-time estimation (servo 250 mA active, solenoid 500 mA), global max 5000 mA |
| **Energy budget (Power)** | Global budget 6000 mA, servo bus 3000 mA, solenoid bus 4000 mA |
| **Degradation** | Automatic event rejection at 80% budget threshold |
| **Polyphony** | Global limit (default 12) and per-instrument (default 4), smart velocity-based rejection |
| **Duty cycle** | Max 80% per actuator |
| **Frequency** | Max 50 Hz triggering rate per actuator |
| **Watchdog** | 5 s timeout per actuator — automatically cuts off if stuck |
| **Logs** | 128-entry circular buffer, 5 levels (DEBUG → CRITICAL), 7 categories |

## Persistence

Complete configuration in JSON on LittleFS (ArduinoJson v7, config version: 7):
* Instruments, actuators, MIDI routing (notes + CC + velocity curves)
* WiFi (SSID, password, hostname), MIDI transports, jitter buffer
* Energy budget, polyphony, safety limits
* Save / rollback / reset via REST API or UI
