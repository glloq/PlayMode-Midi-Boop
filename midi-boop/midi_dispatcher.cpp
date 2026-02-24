#include "midi_dispatcher.h"

// ============================================================================
// PlayMode Midi B∞p — MIDI Dispatcher (implémentation)
// ============================================================================

MidiDispatcher::MidiDispatcher(Scheduler& scheduler, ConfigManager& config)
    : _scheduler(scheduler),
      _config(config),
      _dispatched_count(0),
      _dropped_count(0) {
    memset(_channel_to_instrument, -1, sizeof(_channel_to_instrument));
    memset(_max_latency_ms, 0, sizeof(_max_latency_ms));
}

void MidiDispatcher::dispatch(const MidiMessage& msg) {
    switch (msg.type) {
        case MIDI_NOTE_ON:
            // Note On avec vélocité 0 = Note Off (convention MIDI)
            if (msg.data2 == 0) {
                handleNoteOff(msg);
            } else {
                handleNoteOn(msg);
            }
            break;

        case MIDI_NOTE_OFF:
            handleNoteOff(msg);
            break;

        case MIDI_CONTROL_CHANGE:
            handleControlChange(msg);
            break;

        default:
            // Ignorer les autres types de messages pour l'instant
            break;
    }
}

void MidiDispatcher::refreshConfig() {
    // Réinitialiser les tables de lookup
    memset(_channel_to_instrument, -1, sizeof(_channel_to_instrument));
    memset(_max_latency_ms, 0, sizeof(_max_latency_ms));

    InstrumentConfig* instruments = _config.getInstruments();
    uint8_t count = _config.getInstrumentCount();

    for (uint8_t i = 0; i < count; i++) {
        if (!instruments[i].enabled) continue;

        uint8_t ch = instruments[i].midi_channel;
        if (ch < 16) {
            _channel_to_instrument[ch] = (int8_t)i;
        }

        // Calculer la latence max pour cet instrument
        _max_latency_ms[i] = computeMaxLatency(instruments[i]);
    }

    Serial.printf("[MIDI-DISP] Config rechargée : %d instruments mappés\n", count);
}

uint32_t MidiDispatcher::getDispatchedCount() const {
    return _dispatched_count;
}

uint32_t MidiDispatcher::getDroppedCount() const {
    return _dropped_count;
}

// ============================================================================
// Gestion Note On
// ============================================================================
void MidiDispatcher::handleNoteOn(const MidiMessage& msg) {
    // Trouver l'instrument pour ce canal
    int8_t inst_idx = _channel_to_instrument[msg.channel];
    if (inst_idx < 0) {
        _dropped_count++;
        return;
    }

    InstrumentConfig& inst = _config.getInstruments()[inst_idx];

    // Trouver l'actionneur pour cette note
    int8_t act_slot = findActuatorForNote(inst, msg.data1);
    if (act_slot < 0) {
        _dropped_count++;
        return;
    }

    uint8_t actuator_id = inst.actuator_ids[act_slot];

    // Trouver la config de l'actionneur pour la compensation de latence
    ActuatorConfig* actuators = _config.getActuators();
    uint8_t act_count = _config.getActuatorCount();
    uint16_t actuator_latency = inst.default_latency_ms;

    for (uint8_t i = 0; i < act_count; i++) {
        if (actuators[i].id == actuator_id) {
            actuator_latency = actuators[i].latency_ms;
            break;
        }
    }

    // Compensation de latence :
    // L'actionneur le plus lent définit le timing de base.
    // Les actionneurs plus rapides sont retardés pour synchroniser.
    uint16_t compensation_us = (_max_latency_ms[inst_idx] - actuator_latency) * 1000;

    // Créer l'événement scheduler
    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time() + compensation_us;
    evt.actuator_id = actuator_id;
    evt.action = ACTION_NOTE_ON;
    evt.velocity = msg.data2;
    evt.priority = 0;

    if (_scheduler.pushEvent(evt)) {
        _dispatched_count++;
    } else {
        _dropped_count++;
    }
}

// ============================================================================
// Gestion Note Off
// ============================================================================
void MidiDispatcher::handleNoteOff(const MidiMessage& msg) {
    int8_t inst_idx = _channel_to_instrument[msg.channel];
    if (inst_idx < 0) {
        _dropped_count++;
        return;
    }

    InstrumentConfig& inst = _config.getInstruments()[inst_idx];

    int8_t act_slot = findActuatorForNote(inst, msg.data1);
    if (act_slot < 0) {
        _dropped_count++;
        return;
    }

    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time();
    evt.actuator_id = inst.actuator_ids[act_slot];
    evt.action = ACTION_NOTE_OFF;
    evt.velocity = 0;
    evt.priority = 0;

    if (_scheduler.pushEvent(evt)) {
        _dispatched_count++;
    } else {
        _dropped_count++;
    }
}

// ============================================================================
// Gestion Control Change (futur : mapping CC → position servo)
// ============================================================================
void MidiDispatcher::handleControlChange(const MidiMessage& msg) {
    // Pour l'instant, CC non traité — sera implémenté en Phase 4
    // (mapping CC → position servo via event normalizer)
    (void)msg;
}

// ============================================================================
// Recherche d'actionneur par note MIDI dans un instrument
// ============================================================================
int8_t MidiDispatcher::findActuatorForNote(const InstrumentConfig& inst, uint8_t note) {
    for (uint8_t i = 0; i < inst.actuator_count; i++) {
        if (inst.midi_notes[i] == note) {
            return (int8_t)i;
        }
    }
    return -1;
}

// ============================================================================
// Calcul de la latence max d'un instrument
// ============================================================================
uint16_t MidiDispatcher::computeMaxLatency(const InstrumentConfig& inst) {
    uint16_t max_lat = inst.default_latency_ms;

    ActuatorConfig* actuators = _config.getActuators();
    uint8_t act_count = _config.getActuatorCount();

    for (uint8_t i = 0; i < inst.actuator_count; i++) {
        uint8_t target_id = inst.actuator_ids[i];
        for (uint8_t j = 0; j < act_count; j++) {
            if (actuators[j].id == target_id && actuators[j].latency_ms > max_lat) {
                max_lat = actuators[j].latency_ms;
            }
        }
    }

    return max_lat;
}
