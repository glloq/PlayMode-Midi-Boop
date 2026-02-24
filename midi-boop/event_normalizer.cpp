#include "event_normalizer.h"

// ============================================================================
// PlayMode Midi B∞p — Event Normalizer (implémentation)
// ============================================================================

EventNormalizer::EventNormalizer(Scheduler& scheduler, ConfigManager& configManager)
    : _scheduler(scheduler),
      _configManager(configManager),
      _routing_count(0),
      _processed_count(0),
      _routed_count(0),
      _unmapped_count(0) {
    memset(_routing, 0, sizeof(_routing));
    memset(_max_latency_ms, 0, sizeof(_max_latency_ms));
}

// ============================================================================
// Initialisation — Construit les tables de routage depuis le ConfigManager
// ============================================================================

void EventNormalizer::begin() {
    Serial.println("[NORM] Initialisation Event Normalizer...");

    InstrumentConfig* instruments = _configManager.getInstruments();
    uint8_t inst_count = _configManager.getInstrumentCount();
    _routing_count = inst_count;

    for (uint8_t i = 0; i < inst_count; i++) {
        _routing[i].instrument_index = i;
        _routing[i].note_map_count = 0;
        _routing[i].cc_map_count = 0;
        _routing[i].velocity_curve_count = 0;

        if (instruments[i].enabled) {
            // Construire un mapping par défaut (1:1 note → actuateur)
            buildDefaultMapping(i);

            // Calculer la latence maximale pour la compensation
            computeMaxLatency(i);

            Serial.printf("[NORM] Instrument %d (%s) : ch=%d, %d mappings, max_lat=%dms\n",
                          i, instruments[i].name, instruments[i].midi_channel,
                          _routing[i].note_map_count, _max_latency_ms[i]);
        }
    }

    Serial.printf("[NORM] %d instruments configurés\n", _routing_count);
}

// ============================================================================
// Traitement d'un événement MIDI
// ============================================================================

void EventNormalizer::processMidiEvent(const MidiEvent& event) {
    _processed_count++;

    // Trouver l'instrument par canal MIDI
    int8_t inst_idx = findInstrumentByChannel(event.channel);
    if (inst_idx < 0) {
        _unmapped_count++;
        return;
    }

    // Vérifier que l'instrument est actif
    InstrumentConfig* instruments = _configManager.getInstruments();
    if (!instruments[inst_idx].enabled) return;

    // Router selon le type de message
    switch (event.type) {
        case MIDI_MSG_NOTE_ON:
            handleNoteOn(inst_idx, event);
            break;
        case MIDI_MSG_NOTE_OFF:
            handleNoteOff(inst_idx, event);
            break;
        case MIDI_MSG_CC:
            handleCC(inst_idx, event);
            break;
        case MIDI_MSG_PROGRAM_CHANGE:
            handleProgramChange(inst_idx, event);
            break;
    }
}

// ============================================================================
// Handle Note On
// ============================================================================

void EventNormalizer::handleNoteOn(uint8_t instrument_index, const MidiEvent& event) {
    // Convention MIDI : velocity 0 = Note Off
    if (event.data2 == 0) {
        handleNoteOff(instrument_index, event);
        return;
    }

    // Trouver le mapping note → actionneur
    NoteMapping* mapping = findNoteMapping(instrument_index, event.data1);
    if (mapping == nullptr || !mapping->enabled) {
        _unmapped_count++;
        return;
    }

    // Appliquer la courbe de vélocité
    uint8_t mapped_velocity = applyVelocityCurve(instrument_index, event.data2);

    // Récupérer la latence de l'actionneur pour compensation
    ActuatorConfig* actuators = _configManager.getActuators();
    uint8_t act_count = _configManager.getActuatorCount();
    uint16_t actuator_latency = 0;

    for (uint8_t i = 0; i < act_count; i++) {
        if (actuators[i].id == mapping->actuator_id) {
            actuator_latency = actuators[i].latency_ms;
            break;
        }
    }

    // Calculer le timestamp avec compensation de latence
    uint32_t trigger_time = computeTriggerTime(instrument_index, actuator_latency);

    // Envoyer au scheduler
    if (pushSchedulerEvent(mapping->actuator_id, ACTION_NOTE_ON,
                           mapped_velocity, 0, trigger_time, 0)) {
        _routed_count++;
    }
}

// ============================================================================
// Handle Note Off
// ============================================================================

void EventNormalizer::handleNoteOff(uint8_t instrument_index, const MidiEvent& event) {
    // Trouver le mapping note → actionneur
    NoteMapping* mapping = findNoteMapping(instrument_index, event.data1);
    if (mapping == nullptr || !mapping->enabled) {
        return;
    }

    // Note Off : pas de compensation de latence (exécution immédiate)
    uint32_t trigger_time = (uint32_t)esp_timer_get_time();

    pushSchedulerEvent(mapping->actuator_id, ACTION_NOTE_OFF,
                       event.data2, 0, trigger_time, 0);
    _routed_count++;
}

// ============================================================================
// Handle Control Change
// ============================================================================

void EventNormalizer::handleCC(uint8_t instrument_index, const MidiEvent& event) {
    // Trouver le mapping CC
    CCMapping* mapping = findCCMapping(instrument_index, event.data1);
    if (mapping == nullptr || !mapping->enabled) {
        return;
    }

    // Mapper la valeur CC vers la plage cible
    uint16_t mapped_value = mapCCValue(event.data2, mapping->range_min, mapping->range_max);

    // Trouver l'actionneur cible
    ActuatorConfig* actuators = _configManager.getActuators();
    uint8_t act_count = _configManager.getActuatorCount();
    ActuatorConfig* target = nullptr;

    for (uint8_t i = 0; i < act_count; i++) {
        if (actuators[i].id == mapping->actuator_id) {
            target = &actuators[i];
            break;
        }
    }

    if (target == nullptr) return;

    uint32_t trigger_time = (uint32_t)esp_timer_get_time();

    switch (mapping->target) {
        case CC_TARGET_POSITION:
            // Position directe → SchedulerEvent
            pushSchedulerEvent(mapping->actuator_id, ACTION_POSITION_SET,
                               0, mapped_value, trigger_time, 1);
            break;

        case CC_TARGET_AMPLITUDE:
            // Modification directe du paramètre actuateur
            target->amplitude = mapped_value;
            break;

        case CC_TARGET_SPEED:
            // Modification directe du paramètre actuateur
            target->speed_ms = mapped_value;
            break;

        case CC_TARGET_PWM_HOLD:
            // PWM de maintien → SchedulerEvent
            pushSchedulerEvent(mapping->actuator_id, ACTION_PWM_SET,
                               0, mapped_value, trigger_time, 1);
            break;
    }

    _routed_count++;
}

// ============================================================================
// Handle Program Change
// ============================================================================

void EventNormalizer::handleProgramChange(uint8_t instrument_index, const MidiEvent& event) {
    // Program Change : modifier le comportement de tous les actionneurs de l'instrument
    InstrumentConfig* instruments = _configManager.getInstruments();
    ActuatorConfig* actuators = _configManager.getActuators();
    uint8_t act_count = _configManager.getActuatorCount();

    uint8_t new_behavior = event.data1;

    for (uint8_t i = 0; i < instruments[instrument_index].actuator_count; i++) {
        uint8_t act_id = instruments[instrument_index].actuator_ids[i];

        for (uint8_t j = 0; j < act_count; j++) {
            if (actuators[j].id == act_id) {
                // Limiter selon le type d'actionneur
                if (actuators[j].type == ACT_SERVO) {
                    if (new_behavior <= SERVO_TOUCHE) {
                        actuators[j].behavior = new_behavior;
                    }
                } else if (actuators[j].type == ACT_SOLENOID) {
                    if (new_behavior <= SOL_HIT_AND_HOLD) {
                        actuators[j].behavior = new_behavior;
                    }
                }
                break;
            }
        }
    }

    Serial.printf("[NORM] Program Change inst=%d : behavior=%d\n",
                  instrument_index, new_behavior);
    _routed_count++;
}

// ============================================================================
// Configuration du routage
// ============================================================================

bool EventNormalizer::addNoteMapping(uint8_t instrument_index, const NoteMapping& mapping) {
    if (instrument_index >= _routing_count) return false;

    MidiRoutingConfig& routing = _routing[instrument_index];
    if (routing.note_map_count >= MAX_NOTE_MAPPINGS) return false;

    // Vérifier qu'un mapping pour cette note n'existe pas déjà
    for (uint8_t i = 0; i < routing.note_map_count; i++) {
        if (routing.note_map[i].midi_note == mapping.midi_note) {
            // Remplacer le mapping existant
            routing.note_map[i] = mapping;
            return true;
        }
    }

    routing.note_map[routing.note_map_count] = mapping;
    routing.note_map_count++;
    return true;
}

bool EventNormalizer::removeNoteMapping(uint8_t instrument_index, uint8_t midi_note) {
    if (instrument_index >= _routing_count) return false;

    MidiRoutingConfig& routing = _routing[instrument_index];
    for (uint8_t i = 0; i < routing.note_map_count; i++) {
        if (routing.note_map[i].midi_note == midi_note) {
            // Décaler les éléments suivants
            for (uint8_t j = i; j < routing.note_map_count - 1; j++) {
                routing.note_map[j] = routing.note_map[j + 1];
            }
            routing.note_map_count--;
            return true;
        }
    }
    return false;
}

bool EventNormalizer::addCCMapping(uint8_t instrument_index, const CCMapping& mapping) {
    if (instrument_index >= _routing_count) return false;

    MidiRoutingConfig& routing = _routing[instrument_index];
    if (routing.cc_map_count >= MAX_CC_MAPPINGS) return false;

    // Vérifier qu'un mapping pour ce CC n'existe pas déjà
    for (uint8_t i = 0; i < routing.cc_map_count; i++) {
        if (routing.cc_map[i].cc_number == mapping.cc_number) {
            routing.cc_map[i] = mapping;
            return true;
        }
    }

    routing.cc_map[routing.cc_map_count] = mapping;
    routing.cc_map_count++;
    return true;
}

bool EventNormalizer::removeCCMapping(uint8_t instrument_index, uint8_t cc_number) {
    if (instrument_index >= _routing_count) return false;

    MidiRoutingConfig& routing = _routing[instrument_index];
    for (uint8_t i = 0; i < routing.cc_map_count; i++) {
        if (routing.cc_map[i].cc_number == cc_number) {
            for (uint8_t j = i; j < routing.cc_map_count - 1; j++) {
                routing.cc_map[j] = routing.cc_map[j + 1];
            }
            routing.cc_map_count--;
            return true;
        }
    }
    return false;
}

void EventNormalizer::setVelocityCurve(uint8_t instrument_index,
                                        const VelocityCurvePoint* points, uint8_t count) {
    if (instrument_index >= _routing_count) return;
    if (count > VELOCITY_CURVE_POINTS) count = VELOCITY_CURVE_POINTS;

    MidiRoutingConfig& routing = _routing[instrument_index];
    for (uint8_t i = 0; i < count; i++) {
        routing.velocity_curve[i] = points[i];
    }
    routing.velocity_curve_count = count;
}

void EventNormalizer::reloadConfig() {
    _routing_count = 0;
    begin();
}

// ============================================================================
// Statistiques
// ============================================================================

uint32_t EventNormalizer::getProcessedCount() const { return _processed_count; }
uint32_t EventNormalizer::getRoutedCount() const { return _routed_count; }
uint32_t EventNormalizer::getUnmappedCount() const { return _unmapped_count; }

// ============================================================================
// Méthodes internes
// ============================================================================

int8_t EventNormalizer::findInstrumentByChannel(uint8_t midi_channel) {
    InstrumentConfig* instruments = _configManager.getInstruments();
    uint8_t count = _configManager.getInstrumentCount();

    for (uint8_t i = 0; i < count; i++) {
        if (instruments[i].midi_channel == midi_channel && instruments[i].enabled) {
            return (int8_t)i;
        }
    }
    return -1;
}

NoteMapping* EventNormalizer::findNoteMapping(uint8_t instrument_index, uint8_t midi_note) {
    if (instrument_index >= _routing_count) return nullptr;

    MidiRoutingConfig& routing = _routing[instrument_index];
    for (uint8_t i = 0; i < routing.note_map_count; i++) {
        if (routing.note_map[i].midi_note == midi_note) {
            return &routing.note_map[i];
        }
    }
    return nullptr;
}

CCMapping* EventNormalizer::findCCMapping(uint8_t instrument_index, uint8_t cc_number) {
    if (instrument_index >= _routing_count) return nullptr;

    MidiRoutingConfig& routing = _routing[instrument_index];
    for (uint8_t i = 0; i < routing.cc_map_count; i++) {
        if (routing.cc_map[i].cc_number == cc_number) {
            return &routing.cc_map[i];
        }
    }
    return nullptr;
}

uint8_t EventNormalizer::applyVelocityCurve(uint8_t instrument_index, uint8_t velocity) {
    if (instrument_index >= _routing_count) return velocity;

    MidiRoutingConfig& routing = _routing[instrument_index];
    if (routing.velocity_curve_count == 0) return velocity;

    // Interpolation linéaire entre les points de la courbe
    VelocityCurvePoint* curve = routing.velocity_curve;
    uint8_t count = routing.velocity_curve_count;

    // En dessous du premier point
    if (velocity <= curve[0].input) return curve[0].output;

    // Au-dessus du dernier point
    if (velocity >= curve[count - 1].input) return curve[count - 1].output;

    // Trouver les deux points encadrants
    for (uint8_t i = 0; i < count - 1; i++) {
        if (velocity >= curve[i].input && velocity <= curve[i + 1].input) {
            // Interpolation linéaire
            uint16_t input_range = curve[i + 1].input - curve[i].input;
            if (input_range == 0) return curve[i].output;

            uint16_t output_range = curve[i + 1].output - curve[i].output;
            uint16_t offset = velocity - curve[i].input;
            return curve[i].output + (uint8_t)((offset * output_range) / input_range);
        }
    }

    return velocity;
}

uint32_t EventNormalizer::computeTriggerTime(uint8_t instrument_index,
                                              uint16_t actuator_latency_ms) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint16_t max_lat = _max_latency_ms[instrument_index];

    // Compensation de latence :
    // L'actionneur le plus lent (max_latency) déclenche maintenant.
    // Les actionneurs plus rapides attendent la différence.
    // Résultat : tous les sons arrivent au même moment physique.
    uint16_t compensation_ms = max_lat - min(actuator_latency_ms, max_lat);

    // Ajouter le délai de compensation
    return now_us + ((uint32_t)compensation_ms * 1000);
}

uint16_t EventNormalizer::mapCCValue(uint8_t cc_value, uint16_t range_min, uint16_t range_max) {
    return map(cc_value, 0, 127, range_min, range_max);
}

bool EventNormalizer::pushSchedulerEvent(uint8_t actuator_id, EventAction action,
                                          uint8_t velocity, uint16_t value,
                                          uint32_t trigger_time_us, uint8_t priority) {
    SchedulerEvent event;
    event.trigger_time_us = trigger_time_us;
    event.actuator_id = actuator_id;
    event.action = action;
    event.velocity = velocity;
    event.value = value;
    event.priority = priority;

    return _scheduler.pushEvent(event);
}

void EventNormalizer::computeMaxLatency(uint8_t instrument_index) {
    if (instrument_index >= MAX_INSTRUMENTS) return;

    InstrumentConfig* instruments = _configManager.getInstruments();
    ActuatorConfig* actuators = _configManager.getActuators();
    uint8_t act_count = _configManager.getActuatorCount();

    uint16_t max_lat = 0;

    for (uint8_t i = 0; i < instruments[instrument_index].actuator_count; i++) {
        uint8_t act_id = instruments[instrument_index].actuator_ids[i];

        for (uint8_t j = 0; j < act_count; j++) {
            if (actuators[j].id == act_id) {
                if (actuators[j].latency_ms > max_lat) {
                    max_lat = actuators[j].latency_ms;
                }
                break;
            }
        }
    }

    _max_latency_ms[instrument_index] = max_lat;
}

void EventNormalizer::buildDefaultMapping(uint8_t instrument_index) {
    if (instrument_index >= MAX_INSTRUMENTS) return;

    InstrumentConfig* instruments = _configManager.getInstruments();
    MidiRoutingConfig& routing = _routing[instrument_index];

    routing.note_map_count = 0;

    // Mapping par défaut : note N → actuateur_ids[N % actuator_count]
    // Commence à la note MIDI 60 (C4) par défaut
    uint8_t base_note = 60;
    uint8_t act_count = instruments[instrument_index].actuator_count;

    for (uint8_t i = 0; i < act_count && i < MAX_NOTE_MAPPINGS; i++) {
        NoteMapping& nm = routing.note_map[i];
        nm.midi_note = base_note + i;
        nm.actuator_id = instruments[instrument_index].actuator_ids[i];
        nm.behavior_override = 0xFF;  // Utiliser le défaut de l'actionneur
        nm.enabled = true;
        routing.note_map_count++;
    }
}
