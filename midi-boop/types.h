#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"
#include "midi_types.h"

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

// ============================================================================
// Mapping MIDI et Sécurité
// ============================================================================

// --- Mapping note MIDI → actionneur ---
struct NoteMapping {
    uint8_t midi_note;            // Note MIDI (0-127)
    uint8_t actuator_id;          // ID actionneur cible
    uint8_t behavior_override;    // Comportement spécifique (0xFF = défaut actuator)
    bool enabled;
};

// --- Cible d'un mapping CC ---
enum CCTarget : uint8_t {
    CC_TARGET_POSITION     = 0,   // Position directe du servo (0-180)
    CC_TARGET_AMPLITUDE    = 1,   // Amplitude de frappe
    CC_TARGET_SPEED        = 2,   // Vitesse de mouvement
    CC_TARGET_PWM_HOLD     = 3    // PWM de maintien solénoïde
};

// --- Mapping CC MIDI → paramètre actionneur ---
struct CCMapping {
    uint8_t cc_number;            // Numéro CC (0-127)
    uint8_t actuator_id;          // ID actionneur cible
    CCTarget target;              // Paramètre ciblé
    uint16_t range_min;           // Valeur min de sortie
    uint16_t range_max;           // Valeur max de sortie
    bool enabled;
};

// --- Point de courbe de vélocité ---
struct VelocityCurvePoint {
    uint8_t input;                // Vélocité MIDI entrée (0-127)
    uint8_t output;               // Vélocité mappée sortie (0-127)
};

// --- Configuration de routage MIDI par instrument ---
struct MidiRoutingConfig {
    uint8_t instrument_index;     // Index dans le tableau instruments
    NoteMapping note_map[MAX_NOTE_MAPPINGS]; // Mappings note actifs (sparse)
    uint8_t note_map_count;       // Nombre de mappings actifs
    CCMapping cc_map[MAX_CC_MAPPINGS];
    uint8_t cc_map_count;
    VelocityCurvePoint velocity_curve[VELOCITY_CURVE_POINTS];
    uint8_t velocity_curve_count;
};

// --- État de sécurité par actionneur ---
struct ActuatorSafetyState {
    uint32_t window_start_us;     // Début fenêtre de comptage (pour freq/duty)
    uint16_t trigger_count_window; // Nombre de déclenchements dans la fenêtre
    uint32_t active_time_us;      // Temps actif cumulé dans la fenêtre
    uint32_t last_activity_us;    // Dernier moment d'activité (pour watchdog)
    bool watchdog_triggered;      // Watchdog déclenché
    bool rate_limited;            // Fréquence limitée
    bool duty_limited;            // Duty cycle limité
    uint16_t estimated_current_ma; // Courant estimé actuel (mA)
};

// --- État global de sécurité ---
struct SafetyState {
    uint32_t total_estimated_current_ma;  // Courant total estimé
    uint8_t active_actuator_count;        // Nombre actionneurs actifs
    bool kill_switch_active;              // Kill switch déclenché
    bool degradation_active;              // Dégradation gracieuse active
    bool over_current;                    // Dépassement courant total
};

// ============================================================================
// Phase 5 — Power Manager
// ============================================================================

// --- Budget énergétique (configuration) ---
struct PowerBudget {
    uint32_t global_max_ma;                              // Budget global total (mA)
    uint32_t servo_bus_max_ma;                           // Budget bus servos (bus 0) (mA)
    uint32_t solenoid_bus_max_ma;                        // Budget bus solénoïdes (bus 1) (mA)
    uint8_t  global_max_polyphony;                       // Polyphonie max globale
    uint8_t  instrument_max_polyphony[MAX_INSTRUMENTS];  // Polyphonie max par instrument
    bool     smart_rejection;                            // Rejet intelligent (priorité vélocité)
};

// --- Statistiques de consommation (runtime) ---
struct PowerStats {
    uint32_t total_estimated_ma;                          // Courant total estimé (mA)
    uint32_t servo_bus_ma;                                // Courant bus servo estimé (mA)
    uint32_t solenoid_bus_ma;                             // Courant bus solénoïde estimé (mA)
    uint8_t  global_active_count;                         // Actionneurs actifs au total
    uint8_t  instrument_active_count[MAX_INSTRUMENTS];    // Actionneurs actifs par instrument
    uint32_t total_rejected;                              // Événements rejetés depuis démarrage
    uint8_t  budget_used_percent;                         // % budget global utilisé
    bool     degradation_active;                          // Dégradation gracieuse active
    bool     budget_exceeded;                             // Budget dépassé (soft limit)
};

#endif // TYPES_H
