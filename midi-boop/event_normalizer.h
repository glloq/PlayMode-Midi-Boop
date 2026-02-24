#ifndef EVENT_NORMALIZER_H
#define EVENT_NORMALIZER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "scheduler.h"
#include "config_manager.h"

// ============================================================================
// PlayMode Midi B∞p — Event Normalizer (MIDI → SchedulerEvent)
// ============================================================================
//
// Reçoit des MidiEvent, résout le routage par canal/instrument,
// applique le mapping notes/CC, les courbes de vélocité,
// la compensation de latence, et produit des SchedulerEvent.
//

class EventNormalizer {
public:
    EventNormalizer(Scheduler& scheduler, ConfigManager& configManager);

    // Initialise les tables de routage depuis la configuration
    void begin();

    // Traite un événement MIDI et génère les SchedulerEvents correspondants
    void processMidiEvent(const MidiEvent& event);

    // --- Configuration du routage ---

    // Ajoute un mapping note pour un instrument
    bool addNoteMapping(uint8_t instrument_index, const NoteMapping& mapping);

    // Supprime un mapping note
    bool removeNoteMapping(uint8_t instrument_index, uint8_t midi_note);

    // Ajoute un mapping CC pour un instrument
    bool addCCMapping(uint8_t instrument_index, const CCMapping& mapping);

    // Supprime un mapping CC
    bool removeCCMapping(uint8_t instrument_index, uint8_t cc_number);

    // Définit la courbe de vélocité pour un instrument
    void setVelocityCurve(uint8_t instrument_index,
                          const VelocityCurvePoint* points, uint8_t count);

    // Recharge la config de routage (après modification ConfigManager)
    void reloadConfig();

    // --- Statistiques ---

    uint32_t getProcessedCount() const;
    uint32_t getRoutedCount() const;
    uint32_t getUnmappedCount() const;

private:
    Scheduler& _scheduler;
    ConfigManager& _configManager;

    // Tables de routage par instrument
    MidiRoutingConfig _routing[MAX_INSTRUMENTS];
    uint8_t _routing_count;

    // Latence maximale par instrument (pour compensation)
    uint16_t _max_latency_ms[MAX_INSTRUMENTS];

    // Statistiques
    uint32_t _processed_count;
    uint32_t _routed_count;
    uint32_t _unmapped_count;

    // --- Méthodes internes ---

    // Trouve l'index instrument par canal MIDI (-1 si non trouvé)
    int8_t findInstrumentByChannel(uint8_t midi_channel);

    // Trouve le mapping note pour un instrument (nullptr si non trouvé)
    NoteMapping* findNoteMapping(uint8_t instrument_index, uint8_t midi_note);

    // Trouve le mapping CC pour un instrument (nullptr si non trouvé)
    CCMapping* findCCMapping(uint8_t instrument_index, uint8_t cc_number);

    // Traite un Note On
    void handleNoteOn(uint8_t instrument_index, const MidiEvent& event);

    // Traite un Note Off
    void handleNoteOff(uint8_t instrument_index, const MidiEvent& event);

    // Traite un Control Change
    void handleCC(uint8_t instrument_index, const MidiEvent& event);

    // Traite un Program Change
    void handleProgramChange(uint8_t instrument_index, const MidiEvent& event);

    // Applique la courbe de vélocité (interpolation linéaire entre points)
    uint8_t applyVelocityCurve(uint8_t instrument_index, uint8_t velocity);

    // Calcule le timestamp avec compensation de latence
    uint32_t computeTriggerTime(uint8_t instrument_index, uint16_t actuator_latency_ms);

    // Mappe une valeur CC (0-127) vers la plage cible
    uint16_t mapCCValue(uint8_t cc_value, uint16_t range_min, uint16_t range_max);

    // Crée et envoie un SchedulerEvent au scheduler
    bool pushSchedulerEvent(uint8_t actuator_id, EventAction action,
                            uint8_t velocity, uint16_t value,
                            uint32_t trigger_time_us, uint8_t priority);

    // Calcule la latence maximale parmi les actionneurs d'un instrument
    void computeMaxLatency(uint8_t instrument_index);

    // Construit le mapping par défaut (1:1 note → actuateur)
    void buildDefaultMapping(uint8_t instrument_index);
};

#endif // EVENT_NORMALIZER_H
