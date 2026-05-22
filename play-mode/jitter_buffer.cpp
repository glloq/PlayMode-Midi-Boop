#include "jitter_buffer.h"

// ============================================================================
// PlayMode — MIDI Jitter Buffer (implementation)
// ============================================================================

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
        // Buffer full -- the most recent message is lost
        return false;
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
