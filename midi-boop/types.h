#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PlayMode Midi B∞p — Structures de données partagées
// ============================================================================

// --- Types d'actionneurs ---
enum ActuatorType : uint8_t {
    ACT_SERVO    = 0,
    ACT_SOLENOID = 1
};

// --- Comportements servo ---
enum ServoBehavior : uint8_t {
    SERVO_FRAPPE   = 0,   // A → A+X → retour
    SERVO_ALTERNE  = 1,   // A → B → A → B
    SERVO_GRATTER  = 2,   // A → ±X alterné à chaque note
    SERVO_TOUCHE   = 3    // A → A±δ sur note off
};

// --- Comportements solénoïde ---
enum SolenoidBehavior : uint8_t {
    SOL_FRAPPE       = 0,  // Activation 5-50 ms selon vélocité
    SOL_HIT_AND_HOLD = 1   // PWM initial → réduction → maintien jusqu'à note off
};

// --- Actions du scheduler ---
enum EventAction : uint8_t {
    ACTION_NOTE_ON        = 0,
    ACTION_NOTE_OFF       = 1,
    ACTION_POSITION_SET   = 2,  // Positionnement direct (retour servo, etc.)
    ACTION_PWM_SET        = 3   // Commande PWM directe (solénoïde hold)
};

// --- État interne d'un actionneur ---
struct ActuatorState {
    bool active;                 // Note en cours
    uint16_t current_position;   // Position PWM courante (0-4095)
    bool alternate_state;        // État alterné (A ou B)
    bool scratch_direction;      // Direction gratter (+ ou -)
    uint32_t last_trigger_us;    // Dernier déclenchement (µs)
    uint8_t trigger_count;       // Compteur pour duty cycle
};

// --- Configuration d'un actionneur ---
struct ActuatorConfig {
    uint8_t id;
    ActuatorType type;
    uint8_t bus_id;              // 0 ou 1
    uint8_t pca_address;         // Adresse I²C PCA9685 (ex: 0x40)
    uint8_t pca_channel;         // Canal PCA 0-15
    uint8_t behavior;            // ServoBehavior ou SolenoidBehavior

    // Paramètres servo
    uint16_t angle_initial;      // Angle de repos (degrés)
    uint16_t amplitude;          // Amplitude de mouvement (degrés)
    uint16_t speed_ms;           // Durée du mouvement (ms)
    uint16_t angle_b;            // Angle B pour mode alterné

    // Paramètres solénoïde
    uint16_t pulse_ms;           // Durée frappe (ms)
    uint16_t pwm_initial;        // PWM initial hit-and-hold (0-4095)
    uint16_t pwm_hold;           // PWM maintien hit-and-hold (0-4095)
    uint16_t ramp_ms;            // Durée rampe initial → hold (ms)

    // Commun
    uint16_t latency_ms;         // Latence mécanique mesurée (ms)
    bool enabled;

    // État runtime (non sauvegardé)
    ActuatorState state;
};

// --- Événement scheduler ---
struct SchedulerEvent {
    uint32_t trigger_time_us;    // Timestamp µs de déclenchement (esp_timer_get_time)
    uint8_t actuator_id;         // ID de l'actionneur cible
    EventAction action;          // Type d'action
    uint8_t velocity;            // Vélocité MIDI 0-127
    uint16_t value;              // Valeur contextuelle (angle, PWM, durée)
    uint8_t priority;            // Priorité (0 = plus haute)
};

// --- Configuration d'un instrument ---
struct InstrumentConfig {
    char name[32];               // Nom de l'instrument
    uint8_t midi_channel;        // Canal MIDI assigné (0-15)
    uint8_t bus_id;              // Bus I²C dédié
    uint8_t actuator_ids[PCA_CHANNELS]; // IDs des actionneurs associés
    uint8_t midi_notes[PCA_CHANNELS];   // Note MIDI pour chaque actionneur (0xFF = non mappé)
    uint8_t actuator_count;      // Nombre d'actionneurs
    uint16_t default_latency_ms; // Latence par défaut
    bool auto_calibration;       // Calibration auto activée
    bool enabled;                // Instrument actif
};

// --- Configuration d'un bus I²C ---
struct BusConfig {
    uint8_t id;                  // 0 ou 1
    uint8_t sda_pin;
    uint8_t scl_pin;
    uint8_t oe_pin;              // Output Enable GPIO
    uint32_t i2c_frequency;      // Fréquence I²C (Hz)
    uint16_t pwm_frequency;      // Fréquence PWM PCA (Hz)
    bool enabled;
    uint8_t pca_addresses[PCA_MAX_PER_BUS]; // Adresses PCA détectées
    uint8_t pca_count;           // Nombre de PCA détectés
};

// --- Comparateur pour priority queue (tri par timestamp) ---
struct EventComparator {
    bool operator()(const SchedulerEvent& a, const SchedulerEvent& b) {
        // Plus petit timestamp = plus haute priorité
        if (a.trigger_time_us != b.trigger_time_us) {
            return a.trigger_time_us > b.trigger_time_us;
        }
        return a.priority > b.priority;
    }
};

#endif // TYPES_H
