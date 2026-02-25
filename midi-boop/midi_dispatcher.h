#ifndef MIDI_DISPATCHER_H
#define MIDI_DISPATCHER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "midi_types.h"
#include "scheduler.h"
#include "config_manager.h"

// Forward declaration
class PowerManager;

// ============================================================================
// PlayMode Midi B∞p — MIDI Dispatcher (Phase 3+4+5)
// ============================================================================
//
// Route les messages MIDI vers les actionneurs via le scheduler.
// Lookup : canal MIDI → instrument → note → actionneur.
// Applique la compensation de latence pour synchroniser les actionneurs.
// Phase 4 : dispatch CC, courbes de vélocité.
// Phase 5 : vérification PowerManager avant NOTE_ON.
//

class MidiDispatcher {
public:
    MidiDispatcher(Scheduler& scheduler, ConfigManager& config);

    // Dispatch un message MIDI vers le scheduler
    void dispatch(const MidiMessage& msg);

    // Reconstruit les tables de lookup depuis la config
    void refreshConfig();

    // Nombre de messages dispatchés avec succès
    uint32_t getDispatchedCount() const;

    // Nombre de messages ignorés (note non mappée, canal inconnu)
    uint32_t getDroppedCount() const;

    // Nombre de messages rejetés par le PowerManager (budget/polyphonie)
    uint32_t getPowerRejectedCount() const;

    // Enregistre le PowerManager (optionnel — peut être null)
    void setPowerManager(PowerManager* pm);

private:
    Scheduler& _scheduler;
    ConfigManager& _config;
    PowerManager* _powerManager;
    uint32_t _dispatched_count;
    uint32_t _dropped_count;
    uint32_t _power_rejected_count;

    // Table de lookup rapide : [canal MIDI] → index instrument (-1 = non mappé)
    int8_t _channel_to_instrument[16];

    // Latence max par instrument (pour compensation)
    uint16_t _max_latency_ms[MAX_INSTRUMENTS];

    // Cache des pointeurs vers routing configs par instrument index
    MidiRoutingConfig* _routing_cache[MAX_INSTRUMENTS];

    // Traite un Note On
    void handleNoteOn(const MidiMessage& msg);

    // Traite un Note Off
    void handleNoteOff(const MidiMessage& msg);

    // Traite un Control Change
    void handleControlChange(const MidiMessage& msg);

    // Applique la courbe de vélocité d'un instrument
    uint8_t applyVelocityCurve(uint8_t instrument_index, uint8_t velocity);

    // Cherche l'actionneur correspondant à une note dans un instrument
    // Retourne l'index dans actuator_ids ou -1
    int8_t findActuatorForNote(const InstrumentConfig& inst, uint8_t note);

    // Retrouve la config d'un actionneur par ID
    ActuatorConfig* findActuatorConfig(uint8_t actuator_id);

    // Mappe une valeur CC (0-127) vers un range de sortie
    uint16_t mapCCValue(uint8_t cc_value, uint16_t range_min, uint16_t range_max);

    // Calcule la latence max parmi les actionneurs d'un instrument
    uint16_t computeMaxLatency(const InstrumentConfig& inst);
};

#endif // MIDI_DISPATCHER_H
