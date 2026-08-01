#include "jitter_buffer.h"

// ============================================================================
// PlayMode — MIDI Jitter Buffer (implementation)
// ============================================================================
//
// AUDIT NOTE: this is a fixed *delay* FIFO, not a reordering buffer. It holds
// each message for `depth_ms` after arrival (absorbing arrival-time jitter) but
// does NOT sort by timestamp, so it cannot correct genuinely out-of-order
// delivery. The UI presents it as "network delay" to reflect this. A true
// reorderer would keep the messages sorted by timestamp and pop the earliest
// once it is due.

JitterBuffer::JitterBuffer()
    : _head(0),
      _tail(0),
      _count(0),
      _depth_ms(MIDI_JITTER_BUFFER_MS) {
    memset(_buffer, 0, sizeof(_buffer));
}

void JitterBuffer::setDepth(uint16_t depth_ms) {
    if (depth_ms > MIDI_JITTER_BUFFER_MAX) {
        depth_ms = MIDI_JITTER_BUFFER_MAX;
    }
    _depth_ms = depth_ms;
}

uint16_t JitterBuffer::getDepth() const {
    return _depth_ms;
}

bool JitterBuffer::insert(const MidiMessage& msg) {
    if (_count >= JITTER_BUFFER_SIZE) {
        // AUDIT FIX (safety): never silently drop a note release. Losing a Note
        // Off leaves a TOUCHE servo / hit-and-hold solenoid energized until the
        // watchdog trips. When the buffer is full and the incoming message is a
        // release (Note Off, or Note On with velocity 0 = Note Off per MIDI
        // convention), evict the OLDEST queued message to make room so the
        // release is always admitted. Non-release messages are still dropped
        // when full (a lost Note On only means a missed strike, not a stuck
        // output).
        bool is_release = (msg.type == MIDI_NOTE_OFF) ||
                          (msg.type == MIDI_NOTE_ON && msg.data2 == 0);
        if (!is_release) return false;
        _tail = (_tail + 1) % JITTER_BUFFER_SIZE;   // drop oldest
        _count--;
    }

    _buffer[_head] = msg;
    _head = (_head + 1) % JITTER_BUFFER_SIZE;
    _count++;
    return true;
}

bool JitterBuffer::pop(MidiMessage& msg) {
    if (_count == 0) {
        return false;
    }

    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint32_t hold_us = (uint32_t)_depth_ms * 1000;

    // AUDIT FIX: signed subtraction correctly handles the uint32_t wrap that
    // happens every ~71 min on `esp_timer_get_time()` truncated to 32 bits.
    // Negative ages (timestamp clearly in the future) are treated as 0.
    int32_t age_us = (int32_t)(now_us - _buffer[_tail].timestamp_us);
    if (age_us >= (int32_t)hold_us) {
        msg = _buffer[_tail];
        _tail = (_tail + 1) % JITTER_BUFFER_SIZE;
        _count--;
        return true;
    }

    return false;
}

uint8_t JitterBuffer::count() const {
    return _count;
}

void JitterBuffer::clear() {
    _head = 0;
    _tail = 0;
    _count = 0;
}
