#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <Arduino.h>
#include "midi_types.h"

// ============================================================================
// PlayMode — Byte-by-byte MIDI Parser (Phase 3)
// ============================================================================
//
// Stateful MIDI parser that consumes one byte at a time.
// Supports running status and ignores SysEx messages.
// Used by each transport (Serial, UDP) independently.
//

class MidiParser {
public:
    MidiParser();

    // Resets the parser state
    void reset();

    // Feeds the parser with a byte.
    // Returns true when a complete message is ready.
    bool feed(uint8_t byte);

    // Retrieves the last parsed message.
    // Valid only after feed() has returned true.
    MidiMessage getMessage() const;

private:
    MidiMessage _message;        // Message being constructed
    uint8_t _running_status;     // Last status byte (running status)
    uint8_t _data_index;         // Expected data byte index (0 or 1)
    uint8_t _expected_length;    // Number of expected data bytes (1 or 2)
    bool _in_sysex;              // Currently receiving SysEx (ignored)
    bool _ready;                 // Message ready to be read

    // Determines the number of data bytes for a status byte
    uint8_t dataLengthForStatus(uint8_t status);

    // Processes a status byte
    void handleStatusByte(uint8_t byte);

    // Processes a data byte
    bool handleDataByte(uint8_t byte);
};

#endif // MIDI_PARSER_H
