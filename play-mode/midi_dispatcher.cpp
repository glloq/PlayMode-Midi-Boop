#include "midi_dispatcher.h"

// ============================================================================
// PlayMode — MIDI Dispatcher (implementation)
// ============================================================================

MidiDispatcher::MidiDispatcher(Scheduler& scheduler, ConfigManager& config)
    : _scheduler(scheduler),
      _config(config),
      _dispatched_count(0),
      _dropped_count(0),
      _ws_log_head(0),
      _ws_log_count(0) {
    memset(_max_latency_ms, 0, sizeof(_max_latency_ms));
    memset(_routing_cache, 0, sizeof(_routing_cache));
    memset(_ws_log, 0, sizeof(_ws_log));
}

void MidiDispatcher::dispatch(const MidiMessage& msg) {
    uint32_t dispatched_before = _dispatched_count;

    switch (msg.type) {
        case MIDI_NOTE_ON:
            // Note On with velocity 0 = Note Off (MIDI convention)
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
            break;
    }

    // AUDIT FIX: log the message for WebSocket relay (real-time MIDI log)
    bool routed = (_dispatched_count > dispatched_before);
    pushWsLog(msg, routed);
}

void MidiDispatcher::refreshConfig() {
    // Reset lookup tables
    memset(_max_latency_ms, 0, sizeof(_max_latency_ms));
    memset(_routing_cache, 0, sizeof(_routing_cache));

    uint8_t count = _config.getInstrumentCount();

    for (uint8_t i = 0; i < count && i < MAX_INSTRUMENTS; i++) {
        // Cache the pointer to the routing config (single source of truth for
        // note -> actuator mapping).
        _routing_cache[i] = _config.getRoutingForInstrument(i);

        // Compute the max latency for this instrument from its note_map.
        _max_latency_ms[i] = computeMaxLatency(i);
    }

    Serial.printf("[MIDI-DISP] Config reloaded: %d instruments mapped\n", count);
}

void MidiDispatcher::allNotesOff() {
    ActuatorConfig* actuators = _config.getActuators();
    uint8_t count = _config.getActuatorCount();
    uint8_t released = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (!actuators[i].state.active) continue;
        SchedulerEvent evt = {};
        evt.trigger_time_us = (uint32_t)esp_timer_get_time();
        evt.actuator_id = actuators[i].id;
        evt.action = ACTION_NOTE_OFF;
        evt.velocity = 0;
        evt.priority = 0;
        evt.behavior_override = 0xFF;
        evt.instrument_index  = 0xFF;
        if (_scheduler.pushEvent(evt)) released++;
    }
    if (released > 0) {
        Serial.printf("[MIDI-DISP] All notes off — released %d active actuator(s)\n", released);
    }
}

// ============================================================================
// AUDIT FIX (P0.3): channel matching — exact channel OR Omni. Internal
// channels are always 0..15; Omni is the distinct MIDI_CHANNEL_OMNI_INTERNAL
// sentinel.
// ============================================================================
bool MidiDispatcher::channelMatches(const InstrumentConfig& inst, uint8_t channel) const {
    if (inst.midi_channel == MIDI_CHANNEL_OMNI_INTERNAL) return true;
    return inst.midi_channel == channel;
}

uint32_t MidiDispatcher::getDispatchedCount() const {
    return _dispatched_count;
}

uint32_t MidiDispatcher::getDroppedCount() const {
    return _dropped_count;
}

// ============================================================================
// Note On handling — AUDIT FIX (P0.3): bounds-check the channel and dispatch to
// every instrument that listens on it (exact channel or Omni).
// ============================================================================
void MidiDispatcher::handleNoteOn(const MidiMessage& msg) {
    if (msg.channel >= MIDI_CHANNEL_COUNT) { _dropped_count++; return; }

    InstrumentConfig* instruments = _config.getInstruments();
    uint8_t count = _config.getInstrumentCount();
    bool matched = false;

    for (uint8_t i = 0; i < count && i < MAX_INSTRUMENTS; i++) {
        if (!instruments[i].enabled) continue;
        if (!channelMatches(instruments[i], msg.channel)) continue;
        if (dispatchNoteOnToInstrument(i, msg)) matched = true;
    }

    if (!matched) _dropped_count++;
}

bool MidiDispatcher::dispatchNoteOnToInstrument(uint8_t inst_idx, const MidiMessage& msg) {
    MidiRoutingConfig* routing = _routing_cache[inst_idx];
    const NoteMapping* mapping = findNoteMapping(routing, msg.data1);
    if (mapping == nullptr) return false;
    uint8_t actuator_id = mapping->actuator_id;

    InstrumentConfig& inst = _config.getInstruments()[inst_idx];

    // Latency compensation
    // AUDIT FIX: use signed arithmetic to avoid uint16_t underflow
    // if actuator_latency > _max_latency_ms (invalid config), compensation = 0.
    ActuatorConfig* act_config = findActuatorConfig(actuator_id);
    uint16_t actuator_latency = act_config ? act_config->latency_ms : inst.default_latency_ms;
    int32_t compensation_signed = ((int32_t)_max_latency_ms[inst_idx] - (int32_t)actuator_latency) * 1000;
    uint32_t compensation_us = (compensation_signed > 0) ? (uint32_t)compensation_signed : 0;

    // Apply the velocity curve
    uint8_t velocity = applyVelocityCurve(inst_idx, msg.data2);

    // AUDIT FIX (core): the energy-budget admission decision is now made by the
    // scheduler on Core 1 (ResourceManager is single-core owned). The dispatcher
    // only produces the activation request.

    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time() + compensation_us;
    evt.actuator_id = actuator_id;
    evt.action = ACTION_NOTE_ON;
    evt.velocity = velocity;
    evt.priority = 0;
    // AUDIT FIX (P1.3): carry the instrument so the scheduler can bill the
    // ResourceManager after real execution. (P1.5) carry the per-note behaviour
    // override so the engine can apply it.
    evt.instrument_index = inst_idx;
    evt.behavior_override = mapping->behavior_override;

    if (_scheduler.pushEvent(evt)) {
        _dispatched_count++;
        return true;
    }
    _dropped_count++;
    return false;
}

// ============================================================================
// Note Off handling
// ============================================================================
void MidiDispatcher::handleNoteOff(const MidiMessage& msg) {
    if (msg.channel >= MIDI_CHANNEL_COUNT) { _dropped_count++; return; }

    InstrumentConfig* instruments = _config.getInstruments();
    uint8_t count = _config.getInstrumentCount();
    bool matched = false;

    for (uint8_t i = 0; i < count && i < MAX_INSTRUMENTS; i++) {
        if (!instruments[i].enabled) continue;
        if (!channelMatches(instruments[i], msg.channel)) continue;
        if (dispatchNoteOffToInstrument(i, msg)) matched = true;
    }

    if (!matched) _dropped_count++;
}

bool MidiDispatcher::dispatchNoteOffToInstrument(uint8_t inst_idx, const MidiMessage& msg) {
    MidiRoutingConfig* routing = _routing_cache[inst_idx];
    const NoteMapping* mapping = findNoteMapping(routing, msg.data1);
    if (mapping == nullptr) return false;

    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time();
    evt.actuator_id = mapping->actuator_id;
    evt.action = ACTION_NOTE_OFF;
    evt.velocity = 0;
    evt.priority = 0;
    evt.instrument_index = inst_idx;
    evt.behavior_override = mapping->behavior_override;

    // AUDIT FIX (P1.3): the ResourceManager is billed by the scheduler after the
    // NOTE_OFF really executes, not here at enqueue time.
    if (_scheduler.pushEvent(evt)) {
        _dispatched_count++;
        return true;
    }
    _dropped_count++;
    return false;
}

// ============================================================================
// Control Change handling -- Phase 4
// ============================================================================
void MidiDispatcher::handleControlChange(const MidiMessage& msg) {
    if (msg.channel >= MIDI_CHANNEL_COUNT) { _dropped_count++; return; }

    InstrumentConfig* instruments = _config.getInstruments();
    uint8_t count = _config.getInstrumentCount();
    bool matched = false;

    for (uint8_t i = 0; i < count && i < MAX_INSTRUMENTS; i++) {
        if (!instruments[i].enabled) continue;
        if (!channelMatches(instruments[i], msg.channel)) continue;
        if (dispatchCCToInstrument(i, msg)) matched = true;
    }

    if (!matched) _dropped_count++;
}

bool MidiDispatcher::dispatchCCToInstrument(uint8_t inst_idx, const MidiMessage& msg) {
    MidiRoutingConfig* routing = _routing_cache[inst_idx];
    if (routing == nullptr) return false;

    bool dispatched_any = false;
    for (uint8_t i = 0; i < routing->cc_map_count; i++) {
        CCMapping& cc = routing->cc_map[i];
        if (!cc.enabled) continue;
        if (cc.cc_number != msg.data1) continue;

        ActuatorConfig* act = findActuatorConfig(cc.actuator_id);
        if (act == nullptr || !act->enabled) continue;

        uint16_t mapped_value = mapCCValue(msg.data2, cc.range_min, cc.range_max);

        switch (cc.target) {
            case CC_TARGET_POSITION: {
                // Direct servo positioning via scheduler
                SchedulerEvent evt = {};
                evt.trigger_time_us = (uint32_t)esp_timer_get_time();
                evt.actuator_id = cc.actuator_id;
                evt.action = ACTION_POSITION_SET;
                evt.velocity = msg.data2;
                evt.value = mapped_value;
                evt.priority = 1;
                // Direct CC position: use the actuator's own behaviour.
                evt.behavior_override = 0xFF;
                evt.instrument_index  = 0xFF;

                if (_scheduler.pushEvent(evt)) {
                    _dispatched_count++;
                    dispatched_any = true;
                }
                break;
            }

            // AUDIT NOTE: these uint16_t writes from Core 0 are read by
            // Core 1 (actuator_engine). On Xtensa LX6, aligned 16-bit stores
            // are atomic; coherence is guaranteed by the next scheduler tick (<=1 ms).
            case CC_TARGET_AMPLITUDE:
                act->amplitude = mapped_value;
                dispatched_any = true;
                break;

            case CC_TARGET_SPEED:
                act->speed_ms = mapped_value;
                dispatched_any = true;
                break;

            case CC_TARGET_PWM_HOLD:
                act->pwm_hold = mapped_value;
                dispatched_any = true;
                break;
        }
    }

    return dispatched_any;
}

// ============================================================================
// Velocity curve -- Phase 4
// ============================================================================
uint8_t MidiDispatcher::applyVelocityCurve(uint8_t instrument_index, uint8_t velocity) {
    if (instrument_index >= MAX_INSTRUMENTS) return velocity;

    MidiRoutingConfig* routing = _routing_cache[instrument_index];
    if (routing == nullptr || routing->velocity_curve_count == 0) {
        return velocity;
    }

    VelocityCurvePoint* curve = routing->velocity_curve;
    uint8_t count = routing->velocity_curve_count;

    // Below the first point
    if (velocity <= curve[0].input) {
        return curve[0].output;
    }

    // Above the last point
    if (velocity >= curve[count - 1].input) {
        return curve[count - 1].output;
    }

    // Linear interpolation between segments
    for (uint8_t i = 0; i < count - 1; i++) {
        if (velocity >= curve[i].input && velocity <= curve[i + 1].input) {
            uint8_t in_range = curve[i + 1].input - curve[i].input;
            if (in_range == 0) return curve[i].output;

            uint8_t out_range = (curve[i + 1].output > curve[i].output)
                ? curve[i + 1].output - curve[i].output
                : curve[i].output - curve[i + 1].output;
            bool ascending = curve[i + 1].output >= curve[i].output;

            uint16_t offset = (uint16_t)(velocity - curve[i].input) * out_range / in_range;
            if (ascending) {
                return curve[i].output + (uint8_t)offset;
            } else {
                return curve[i].output - (uint8_t)offset;
            }
        }
    }

    return velocity;
}

// ============================================================================
// AUDIT FIX (P0.2): note -> actuator resolution via the routing note_map,
// the single source of truth written by the web UI (/api/routing). Returns the
// mapped actuator ID or -1.
// ============================================================================
const NoteMapping* MidiDispatcher::findNoteMapping(const MidiRoutingConfig* routing, uint8_t note) {
    if (routing == nullptr) return nullptr;
    for (uint8_t i = 0; i < routing->note_map_count && i < MAX_NOTE_MAPPINGS; i++) {
        const NoteMapping& m = routing->note_map[i];
        if (m.enabled && m.midi_note == note) {
            return &m;
        }
    }
    return nullptr;
}

// ============================================================================
// Actuator config lookup by ID
// ============================================================================
ActuatorConfig* MidiDispatcher::findActuatorConfig(uint8_t actuator_id) {
    ActuatorConfig* actuators = _config.getActuators();
    uint8_t count = _config.getActuatorCount();

    for (uint8_t i = 0; i < count; i++) {
        if (actuators[i].id == actuator_id) {
            return &actuators[i];
        }
    }
    return nullptr;
}

// ============================================================================
// CC value mapping (0-127) to min/max range
// ============================================================================
uint16_t MidiDispatcher::mapCCValue(uint8_t cc_value, uint16_t range_min, uint16_t range_max) {
    if (range_min == range_max) return range_min;

    uint16_t lo = (range_min < range_max) ? range_min : range_max;
    uint16_t hi = (range_min < range_max) ? range_max : range_min;

    return lo + (uint16_t)((uint32_t)cc_value * (hi - lo) / 127);
}

// ============================================================================
// Max latency computation — AUDIT FIX (P0.2): iterate the routing note_map
// (the actuators actually reachable via MIDI) rather than the vestigial
// instrument actuator_ids array.
// ============================================================================
uint16_t MidiDispatcher::computeMaxLatency(uint8_t inst_idx) {
    if (inst_idx >= MAX_INSTRUMENTS) return 0;

    InstrumentConfig& inst = _config.getInstruments()[inst_idx];
    uint16_t max_lat = inst.default_latency_ms;

    MidiRoutingConfig* routing = _routing_cache[inst_idx];
    if (routing == nullptr) return max_lat;

    ActuatorConfig* actuators = _config.getActuators();
    uint8_t act_count = _config.getActuatorCount();

    for (uint8_t i = 0; i < routing->note_map_count && i < MAX_NOTE_MAPPINGS; i++) {
        if (!routing->note_map[i].enabled) continue;
        uint8_t target_id = routing->note_map[i].actuator_id;
        for (uint8_t j = 0; j < act_count; j++) {
            if (actuators[j].id == target_id && actuators[j].latency_ms > max_lat) {
                max_lat = actuators[j].latency_ms;
            }
        }
    }

    return max_lat;
}

// ============================================================================
// AUDIT FIX: MIDI ring buffer for WebSocket relay
// ============================================================================

void MidiDispatcher::pushWsLog(const MidiMessage& msg, bool routed) {
    WsLogEntry& entry = _ws_log[_ws_log_head];
    entry.msg = msg;
    entry.routed = routed;
    _ws_log_head = (_ws_log_head + 1) % MIDI_WS_LOG_SIZE;
    if (_ws_log_count < MIDI_WS_LOG_SIZE) _ws_log_count++;
}

uint8_t MidiDispatcher::getWsLogCount() const {
    return _ws_log_count;
}

uint8_t MidiDispatcher::drainWsLog(WsLogEntry* out, uint8_t max_count) {
    if (_ws_log_count == 0) return 0;

    uint8_t start = (_ws_log_head + MIDI_WS_LOG_SIZE - _ws_log_count) % MIDI_WS_LOG_SIZE;
    uint8_t n = (_ws_log_count < max_count) ? _ws_log_count : max_count;

    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (start + i) % MIDI_WS_LOG_SIZE;
        out[i] = _ws_log[idx];
    }

    _ws_log_count = 0;
    _ws_log_head = 0;
    return n;
}
