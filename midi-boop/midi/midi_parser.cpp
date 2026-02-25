#include "midi_parser.h"

// ============================================================================
// PlayMode Midi B∞p — Parseur MIDI (implémentation)
// ============================================================================

MidiParser::MidiParser()
    : _running_status(0),
      _data_index(0),
      _expected_length(0),
      _in_sysex(false),
      _ready(false) {
    memset(&_message, 0, sizeof(_message));
}

void MidiParser::reset() {
    _running_status = 0;
    _data_index = 0;
    _expected_length = 0;
    _in_sysex = false;
    _ready = false;
    memset(&_message, 0, sizeof(_message));
}

bool MidiParser::feed(uint8_t byte) {
    _ready = false;

    // --- Messages temps réel (0xF8-0xFF) : toujours traités, n'affectent pas le running status ---
    if (byte >= 0xF8) {
        // Ignorer les messages temps réel système (clock, start, stop, etc.)
        return false;
    }

    // --- Status byte (bit 7 = 1) ---
    if (byte & 0x80) {
        handleStatusByte(byte);
        return false;
    }

    // --- Data byte (bit 7 = 0) ---
    if (_in_sysex) {
        // Ignorer les data bytes SysEx
        return false;
    }

    return handleDataByte(byte);
}

MidiMessage MidiParser::getMessage() const {
    return _message;
}

// ============================================================================
// Traitement des status bytes
// ============================================================================
void MidiParser::handleStatusByte(uint8_t byte) {
    // SysEx start
    if (byte == 0xF0) {
        _in_sysex = true;
        _running_status = 0;
        return;
    }

    // SysEx end
    if (byte == 0xF7) {
        _in_sysex = false;
        return;
    }

    // Ignorer les messages système communs (0xF1-0xF6)
    if (byte >= 0xF1 && byte <= 0xF6) {
        // Tune Request (0xF6) n'a pas de data bytes
        // Les autres (MTC Quarter Frame, Song Position, Song Select) ont 1-2 data bytes
        // On les ignore tous pour simplifier
        _running_status = 0;
        return;
    }

    // Messages de canal (0x80-0xEF)
    _in_sysex = false;
    _running_status = byte;
    _data_index = 0;
    _expected_length = dataLengthForStatus(byte);

    // Extraire type et canal
    _message.type = (MidiMessageType)(byte & 0xF0);
    _message.channel = byte & 0x0F;
}

// ============================================================================
// Traitement des data bytes
// ============================================================================
bool MidiParser::handleDataByte(uint8_t byte) {
    // Pas de status actif → ignorer
    if (_running_status == 0) {
        return false;
    }

    // Running status : si on reçoit un data byte sans nouveau status,
    // réutiliser le dernier status
    if (_data_index == 0) {
        _message.type = (MidiMessageType)(_running_status & 0xF0);
        _message.channel = _running_status & 0x0F;
    }

    if (_data_index == 0) {
        _message.data1 = byte;
        _data_index = 1;

        // Messages à 1 seul data byte → complet
        if (_expected_length == 1) {
            _message.data2 = 0;
            _message.timestamp_us = (uint32_t)esp_timer_get_time();
            _data_index = 0;  // Prêt pour prochain message (running status)
            _ready = true;
            return true;
        }
    } else if (_data_index == 1) {
        _message.data2 = byte;
        _message.timestamp_us = (uint32_t)esp_timer_get_time();
        _data_index = 0;  // Prêt pour prochain message (running status)
        _ready = true;
        return true;
    }

    return false;
}

// ============================================================================
// Longueur data bytes par type de message
// ============================================================================
uint8_t MidiParser::dataLengthForStatus(uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: return 2;  // Note Off
        case 0x90: return 2;  // Note On
        case 0xA0: return 2;  // Polyphonic Aftertouch
        case 0xB0: return 2;  // Control Change
        case 0xC0: return 1;  // Program Change
        case 0xD0: return 1;  // Channel Pressure
        case 0xE0: return 2;  // Pitch Bend
        default:   return 0;
    }
}
