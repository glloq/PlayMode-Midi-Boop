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
// PlayMode — MIDI Dispatcher (Phase 3+4+5)
// ============================================================================
//
// Routes MIDI messages to actuators via the scheduler.
// Lookup: MIDI channel -> instrument -> note -> actuator.
// Applies latency compensation to synchronize actuators.
// Phase 4: CC dispatch, velocity curves.
// Phase 5: PowerManager check before NOTE_ON.
//

class MidiDispatcher {
public:
    MidiDispatcher(Scheduler& scheduler, ConfigManager& config);

    // Dispatches a MIDI message to the scheduler
    void dispatch(const MidiMessage& msg);

    // Rebuilds lookup tables from config
    void refreshConfig();

    // Number of successfully dispatched messages
    uint32_t getDispatchedCount() const;

    // Number of ignored messages (unmapped note, unknown channel)
    uint32_t getDroppedCount() const;

    // Number of messages rejected by PowerManager (budget/polyphony)
    uint32_t getPowerRejectedCount() const;

    // Registers the PowerManager (optional -- can be null)
    void setPowerManager(PowerManager* pm);

    // --- AUDIT FIX: ring buffer to relay MIDI messages via WebSocket ---
    // Max buffer size (recent messages). Suited for WS broadcast at 200 ms.
    static const uint8_t MIDI_WS_LOG_SIZE = 16;

    // WS log entry (message + routing flag)
    struct WsLogEntry {
        MidiMessage msg;
        bool routed;
    };

    // Returns the number of pending messages in the WS log
    uint8_t getWsLogCount() const;

    // Copies pending entries into `out` (max `max_count`), returns the
    // number actually copied, and clears the buffer.
    uint8_t drainWsLog(WsLogEntry* out, uint8_t max_count);

private:
    Scheduler& _scheduler;
    ConfigManager& _config;
    PowerManager* _powerManager;
    uint32_t _dispatched_count;
    uint32_t _dropped_count;
    uint32_t _power_rejected_count;

    // Ring buffer WS log
    WsLogEntry _ws_log[MIDI_WS_LOG_SIZE];
    uint8_t _ws_log_head;
    uint8_t _ws_log_count;
    void pushWsLog(const MidiMessage& msg, bool routed);

    // Max latency per instrument (for compensation)
    uint16_t _max_latency_ms[MAX_INSTRUMENTS];

    // Cache of pointers to routing configs by instrument index
    MidiRoutingConfig* _routing_cache[MAX_INSTRUMENTS];

    // Handles a Note On
    void handleNoteOn(const MidiMessage& msg);

    // Handles a Note Off
    void handleNoteOff(const MidiMessage& msg);

    // Handles a Control Change
    void handleControlChange(const MidiMessage& msg);

    // AUDIT FIX (P0.2/P0.3): dispatch to a single matching instrument. Returns
    // true if at least one actuator event was scheduled.
    bool dispatchNoteOnToInstrument(uint8_t inst_idx, const MidiMessage& msg);
    bool dispatchNoteOffToInstrument(uint8_t inst_idx, const MidiMessage& msg);
    bool dispatchCCToInstrument(uint8_t inst_idx, const MidiMessage& msg);

    // AUDIT FIX (P0.3): does this instrument listen on the given internal
    // channel (0..15)? True for an exact match or when the instrument is Omni.
    bool channelMatches(const InstrumentConfig& inst, uint8_t channel) const;

    // Applies the velocity curve of an instrument
    uint8_t applyVelocityCurve(uint8_t instrument_index, uint8_t velocity);

    // AUDIT FIX (P0.2/P1.5): resolve a MIDI note to its routing note_map entry
    // (the single source of truth). Returns the mapping (actuator + behaviour
    // override) or nullptr when the note is unmapped.
    const NoteMapping* findNoteMapping(const MidiRoutingConfig* routing, uint8_t note);

    // Finds the config of an actuator by ID
    ActuatorConfig* findActuatorConfig(uint8_t actuator_id);

    // Maps a CC value (0-127) to an output range
    uint16_t mapCCValue(uint8_t cc_value, uint16_t range_min, uint16_t range_max);

    // Computes the max latency among the actuators referenced by an
    // instrument's routing note_map.
    uint16_t computeMaxLatency(uint8_t inst_idx);
};

#endif // MIDI_DISPATCHER_H
