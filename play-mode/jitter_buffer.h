#ifndef JITTER_BUFFER_H
#define JITTER_BUFFER_H

#include <Arduino.h>
#include "config.h"
#include "midi_types.h"

// ============================================================================
// PlayMode — Jitter Buffer MIDI (Phase 3)
// ============================================================================
//
// Circular FIFO buffer that holds MIDI messages for a configurable
// duration (0-100 ms) to absorb network jitter.
// No dynamic allocation -- fixed size JITTER_BUFFER_SIZE.
//

class JitterBuffer {
public:
    JitterBuffer();

    // Sets the buffer depth in milliseconds
    void setDepth(uint16_t depth_ms);

    // Returns the current depth (ms)
    uint16_t getDepth() const;

    // Inserts a message into the buffer
    // Returns false if the buffer is full
    bool insert(const MidiMessage& msg);

    // Removes the oldest message if its retention duration has elapsed
    // Returns true if a message was extracted
    bool pop(MidiMessage& msg);

    // Number of messages in the buffer
    uint8_t count() const;

    // Clears the buffer
    void clear();

private:
    MidiMessage _buffer[JITTER_BUFFER_SIZE];
    uint8_t _head;          // Write index (next insert)
    uint8_t _tail;          // Read index (next pop)
    uint8_t _count;         // Number of elements in the buffer
    uint16_t _depth_ms;     // Depth in milliseconds
};

#endif // JITTER_BUFFER_H
