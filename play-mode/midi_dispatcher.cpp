#include "midi_dispatcher.h"
#include "power_manager.h"

// ============================================================================
// PlayMode — MIDI Dispatcher (implementation)
// ============================================================================

MidiDispatcher::MidiDispatcher(Scheduler& scheduler, ConfigManager& config)
    : _scheduler(scheduler),
      _config(config),
      _powerManager(nullptr),
      _dispatched_count(0),
      _dropped_count(0),
      _power_rejected_count(0),
      _ws_log_head(0),
      _ws_log_count(0) {
    memset(_channel_to_instrument, -1, sizeof(_channel_to_instrument));
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
    memset(_channel_to_instrument, -1, sizeof(_channel_to_instrument));
    memset(_max_latency_ms, 0, sizeof(_max_latency_ms));
    memset(_routing_cache, 0, sizeof(_routing_cache));

    InstrumentConfig* instruments = _config.getInstruments();
    uint8_t count = _config.getInstrumentCount();

    for (uint8_t i = 0; i < count; i++) {
        if (!instruments[i].enabled) continue;

        uint8_t ch = instruments[i].midi_channel;
        if (ch < 16) {
            _channel_to_instrument[ch] = (int8_t)i;
        }

        // Compute the max latency for this instrument
        _max_latency_ms[i] = computeMaxLatency(instruments[i]);

        // Cache the pointer to the routing config
        _routing_cache[i] = _config.getRoutingForInstrument(i);
    }

    Serial.printf("[MIDI-DISP] Config reloaded: %d instruments mapped\n", count);
}

uint32_t MidiDispatcher::getDispatchedCount() const {
    return _dispatched_count;
}

uint32_t MidiDispatcher::getDroppedCount() const {
    return _dropped_count;
}

uint32_t MidiDispatcher::getPowerRejectedCount() const {
    return _power_rejected_count;
}

void MidiDispatcher::setPowerManager(PowerManager* pm) {
    _powerManager = pm;
}

// ============================================================================
// Note On handling
// ============================================================================
void MidiDispatcher::handleNoteOn(const MidiMessage& msg) {
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

    uint8_t actuator_id = inst.actuator_ids[act_slot];

    // Latency compensation
    // AUDIT FIX: use signed arithmetic to avoid uint16_t underflow
    // if actuator_latency > _max_latency_ms (invalid config), compensation = 0.
    ActuatorConfig* act_config = findActuatorConfig(actuator_id);
    uint16_t actuator_latency = act_config ? act_config->latency_ms : inst.default_latency_ms;
    int32_t compensation_signed = ((int32_t)_max_latency_ms[inst_idx] - (int32_t)actuator_latency) * 1000;
    uint32_t compensation_us = (compensation_signed > 0) ? (uint32_t)compensation_signed : 0;

    // Apply the velocity curve
    uint8_t velocity = applyVelocityCurve(inst_idx, msg.data2);

    // Phase 5: check energy budget before activation
    if (_powerManager && act_config) {
        if (!_powerManager->canActivate(*act_config, inst_idx, velocity)) {
            _power_rejected_count++;
            return;
        }
    }

    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time() + compensation_us;
    evt.actuator_id = actuator_id;
    evt.action = ACTION_NOTE_ON;
    evt.velocity = velocity;
    evt.priority = 0;

    if (_scheduler.pushEvent(evt)) {
        _dispatched_count++;
        // Notify the PowerManager of activation
        if (_powerManager && act_config) {
            _powerManager->notifyActivation(*act_config, inst_idx, velocity);
        }
    } else {
        _dropped_count++;
    }
}

// ============================================================================
// Note Off handling
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

    uint8_t actuator_id = inst.actuator_ids[act_slot];

    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time();
    evt.actuator_id = actuator_id;
    evt.action = ACTION_NOTE_OFF;
    evt.velocity = 0;
    evt.priority = 0;

    if (_scheduler.pushEvent(evt)) {
        _dispatched_count++;
        // Notify the PowerManager of deactivation
        if (_powerManager) {
            ActuatorConfig* act_config = findActuatorConfig(actuator_id);
            if (act_config) {
                _powerManager->notifyDeactivation(*act_config, inst_idx);
            }
        }
    } else {
        _dropped_count++;
    }
}

// ============================================================================
// Control Change handling -- Phase 4
// ============================================================================
void MidiDispatcher::handleControlChange(const MidiMessage& msg) {
    int8_t inst_idx = _channel_to_instrument[msg.channel];
    if (inst_idx < 0) {
        _dropped_count++;
        return;
    }

    MidiRoutingConfig* routing = _routing_cache[inst_idx];
    if (routing == nullptr) {
        _dropped_count++;
        return;
    }

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

    if (!dispatched_any) {
        _dropped_count++;
    }
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
// Actuator lookup by MIDI note in an instrument
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
// Max latency computation for an instrument
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
