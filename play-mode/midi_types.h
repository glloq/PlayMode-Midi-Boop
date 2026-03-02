#ifndef MIDI_TYPES_H
#define MIDI_TYPES_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PlayMode — Shared MIDI types (Phase 3)
// ============================================================================

// --- MIDI message types ---
enum MidiMessageType : uint8_t {
    MIDI_NOTE_OFF         = 0x80,
    MIDI_NOTE_ON          = 0x90,
    MIDI_POLY_AFTERTOUCH  = 0xA0,
    MIDI_CONTROL_CHANGE   = 0xB0,
    MIDI_PROGRAM_CHANGE   = 0xC0,
    MIDI_CHANNEL_PRESSURE = 0xD0,
    MIDI_PITCH_BEND       = 0xE0,
    MIDI_SYSTEM           = 0xF0
};

// --- MIDI transport sources ---
enum MidiTransportSource : uint8_t {
    MIDI_SOURCE_SERIAL = 0,
    MIDI_SOURCE_UDP    = 1,
    MIDI_SOURCE_RTP    = 2
};

// --- Parsed MIDI message ---
struct MidiMessage {
    MidiMessageType type;       // Message type (masked status byte)
    uint8_t channel;            // MIDI channel (0-15)
    uint8_t data1;              // Note number or CC number
    uint8_t data2;              // Velocity or CC value
    uint32_t timestamp_us;      // Arrival timestamp (esp_timer_get_time)
    MidiTransportSource source; // Message source
};

// --- WiFi configuration ---
struct WiFiConfig {
    char ssid[33];              // Network SSID (max 32 chars + null)
    char password[65];          // Password (max 64 chars + null)
    char hostname[32];          // mDNS hostname
    bool enabled;               // WiFi enabled
    bool ap_fallback;           // Start in AP mode if STA fails
};

// --- MIDI input configuration ---
struct MidiInputConfig {
    bool serial_enabled;        // Serial MIDI input active
    bool udp_enabled;           // UDP MIDI input active
    bool rtp_enabled;           // RTP-MIDI (AppleMIDI) input active
    uint16_t udp_port;          // UDP listening port
    uint16_t rtp_port;          // RTP-MIDI port
    uint16_t jitter_buffer_ms;  // Jitter buffer depth (ms)
    uint8_t serial_rx_pin;      // GPIO for Serial MIDI RX
};

#endif // MIDI_TYPES_H
