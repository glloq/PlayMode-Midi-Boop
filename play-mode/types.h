#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"
#include "midi_types.h"

// ============================================================================
// PlayMode — Shared data structures
// ============================================================================

// --- Actuator types ---
enum ActuatorType : uint8_t {
    ACT_SERVO    = 0,
    ACT_SOLENOID = 1
};

// --- Servo behaviors ---
enum ServoBehavior : uint8_t {
    SERVO_FRAPPE   = 0,   // A → A+X → return
    SERVO_ALTERNE  = 1,   // A → B → A → B
    SERVO_GRATTER  = 2,   // A → ±X alternating on each note
    SERVO_TOUCHE   = 3    // A → A±δ on note off
};

// --- Solenoid behaviors ---
enum SolenoidBehavior : uint8_t {
    SOL_FRAPPE       = 0,  // Activation 5-50 ms based on velocity
    SOL_HIT_AND_HOLD = 1   // Initial PWM → ramp down → hold until note off
};

// --- Scheduler actions ---
enum EventAction : uint8_t {
    ACTION_NOTE_ON        = 0,
    ACTION_NOTE_OFF       = 1,
    ACTION_POSITION_SET   = 2,  // Direct positioning (servo return, etc.)
    ACTION_PWM_SET        = 3   // Direct PWM command (solenoid hold)
};

// --- Actuator internal state ---
struct ActuatorState {
    volatile bool active;        // Note in progress (volatile: written Core 1, read Core 0)
    uint16_t current_position;   // Current PWM position (0-4095)
    bool alternate_state;        // Alternate state (A or B)
    bool scratch_direction;      // Scratch direction (+ or -)
    uint32_t last_trigger_us;    // Last trigger time (µs)
    uint8_t trigger_count;       // Duty cycle counter
};

// --- Actuator configuration ---
struct ActuatorConfig {
    uint8_t id;
    ActuatorType type;
    uint8_t bus_id;              // 0 or 1
    uint8_t pca_address;         // PCA9685 I²C address (e.g.: 0x40)
    uint8_t pca_channel;         // PCA channel 0-15
    uint8_t behavior;            // ServoBehavior or SolenoidBehavior

    // Servo parameters
    uint16_t angle_initial;      // Rest angle (degrees)
    uint16_t amplitude;          // Movement amplitude (degrees)
    uint16_t speed_ms;           // Movement duration (ms)
    uint16_t angle_b;            // Angle B for alternate mode
    bool     hit_reverse;        // Strike direction: false = clockwise (+), true = counter-clockwise (-)

    // Solenoid parameters
    uint16_t pulse_min_ms;       // Min strike duration (ms) — velocity 0
    uint16_t pulse_ms;           // Max strike duration (ms) — velocity 127
    uint16_t pwm_initial;        // Hit-and-hold initial PWM (0-4095)
    uint16_t pwm_hold;           // Hit-and-hold hold PWM (0-4095)
    uint16_t ramp_ms;            // Ramp duration initial → hold (ms)

    // Common
    uint16_t latency_ms;         // Measured mechanical latency (ms)
    bool enabled;

    // Runtime state (not saved)
    ActuatorState state;
};

// --- Scheduler event ---
struct SchedulerEvent {
    uint32_t trigger_time_us;    // Trigger timestamp µs (esp_timer_get_time)
    uint8_t actuator_id;         // Target actuator ID
    EventAction action;          // Action type
    uint8_t velocity;            // MIDI velocity 0-127
    uint16_t value;              // Contextual value (angle, PWM, duration)
    uint8_t priority;            // Priority (0 = highest)
};

// --- Instrument configuration ---
struct InstrumentConfig {
    char name[32];               // Instrument name
    uint8_t midi_channel;        // Assigned MIDI channel (0-15)
    uint8_t bus_id;              // Dedicated I²C bus
    uint8_t actuator_ids[MAX_ACTUATORS_PER_INSTRUMENT]; // Associated actuator IDs
    uint8_t midi_notes[MAX_ACTUATORS_PER_INSTRUMENT];   // MIDI note for each actuator (0xFF = unmapped)
    uint8_t actuator_count;      // Number of actuators
    uint16_t default_latency_ms; // Default latency
    bool auto_calibration;       // Auto calibration enabled
    bool enabled;                // Instrument active
};

// --- I²C bus configuration ---
struct BusConfig {
    uint8_t id;                  // 0 or 1
    uint8_t sda_pin;
    uint8_t scl_pin;
    uint8_t oe_pin;              // Output Enable GPIO
    uint32_t i2c_frequency;      // I²C frequency (Hz)
    uint16_t pwm_frequency;      // PCA PWM frequency (Hz)
    bool enabled;
    uint8_t pca_addresses[PCA_MAX_PER_BUS]; // Detected PCA addresses
    uint8_t pca_count;           // Number of detected PCAs
};

// --- Comparator for priority queue (sorted by timestamp) ---
struct EventComparator {
    bool operator()(const SchedulerEvent& a, const SchedulerEvent& b) {
        // Smallest timestamp = highest priority
        if (a.trigger_time_us != b.trigger_time_us) {
            return a.trigger_time_us > b.trigger_time_us;
        }
        return a.priority > b.priority;
    }
};

// ============================================================================
// MIDI Mapping and Safety
// ============================================================================

// --- MIDI note → actuator mapping ---
struct NoteMapping {
    uint8_t midi_note;            // MIDI note (0-127)
    uint8_t actuator_id;          // Target actuator ID
    uint8_t behavior_override;    // Specific behavior (0xFF = actuator default)
    bool enabled;
};

// --- CC mapping target ---
enum CCTarget : uint8_t {
    CC_TARGET_POSITION     = 0,   // Direct servo position (0-180)
    CC_TARGET_AMPLITUDE    = 1,   // Strike amplitude
    CC_TARGET_SPEED        = 2,   // Movement speed
    CC_TARGET_PWM_HOLD     = 3    // Solenoid hold PWM
};

// --- MIDI CC → actuator parameter mapping ---
struct CCMapping {
    uint8_t cc_number;            // CC number (0-127)
    uint8_t actuator_id;          // Target actuator ID
    CCTarget target;              // Targeted parameter
    uint16_t range_min;           // Min output value
    uint16_t range_max;           // Max output value
    bool enabled;
};

// --- Velocity curve point ---
struct VelocityCurvePoint {
    uint8_t input;                // Input MIDI velocity (0-127)
    uint8_t output;               // Mapped output velocity (0-127)
};

// --- Per-instrument MIDI routing configuration ---
struct MidiRoutingConfig {
    uint8_t instrument_index;     // Index in the instruments array
    NoteMapping note_map[MAX_NOTE_MAPPINGS]; // Active note mappings (sparse)
    uint8_t note_map_count;       // Number of active mappings
    CCMapping cc_map[MAX_CC_MAPPINGS];
    uint8_t cc_map_count;
    VelocityCurvePoint velocity_curve[VELOCITY_CURVE_POINTS];
    uint8_t velocity_curve_count;
};

// --- Per-actuator safety state ---
struct ActuatorSafetyState {
    uint32_t window_start_us;     // Counting window start (for freq/duty)
    uint16_t trigger_count_window; // Number of triggers in the window
    uint32_t active_time_us;      // Cumulative active time in the window
    uint32_t last_activity_us;    // Last activity time (for watchdog)
    bool watchdog_triggered;      // Watchdog triggered
    bool rate_limited;            // Rate limited
    bool duty_limited;            // Duty cycle limited
    uint16_t estimated_current_ma; // Current estimated current (mA)
};

// --- Global safety state ---
struct SafetyState {
    uint32_t total_estimated_current_ma;  // Total estimated current
    uint8_t active_actuator_count;        // Number of active actuators
    bool kill_switch_active;              // Kill switch triggered
    bool degradation_active;              // Graceful degradation active
    bool over_current;                    // Total current exceeded
};

// ============================================================================
// Phase 5 — Power Manager
// ============================================================================

// --- Power budget (configuration) ---
struct PowerBudget {
    uint32_t global_max_ma;                              // Total global budget (mA)
    uint32_t servo_bus_max_ma;                           // Servo bus budget (bus 0) (mA)
    uint32_t solenoid_bus_max_ma;                        // Solenoid bus budget (bus 1) (mA)
    uint8_t  global_max_polyphony;                       // Global max polyphony
    uint8_t  instrument_max_polyphony[MAX_INSTRUMENTS];  // Max polyphony per instrument
    bool     smart_rejection;                            // Smart rejection (velocity priority)
};

// --- Power consumption statistics (runtime) ---
struct PowerStats {
    uint32_t total_estimated_ma;                          // Total estimated current (mA)
    uint32_t servo_bus_ma;                                // Estimated servo bus current (mA)
    uint32_t solenoid_bus_ma;                             // Estimated solenoid bus current (mA)
    uint8_t  global_active_count;                         // Total active actuators
    uint8_t  instrument_active_count[MAX_INSTRUMENTS];    // Active actuators per instrument
    uint32_t total_rejected;                              // Events rejected since startup
    uint8_t  budget_used_percent;                         // Global budget % used
    bool     degradation_active;                          // Graceful degradation active
    bool     budget_exceeded;                             // Budget exceeded (soft limit)
};

#endif // TYPES_H
