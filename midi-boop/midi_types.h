#ifndef MIDI_TYPES_H
#define MIDI_TYPES_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PlayMode Midi B∞p — Types MIDI partagés (Phase 3)
// ============================================================================

// --- Types de messages MIDI ---
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

// --- Sources de transport MIDI ---
enum MidiTransportSource : uint8_t {
    MIDI_SOURCE_SERIAL = 0,
    MIDI_SOURCE_UDP    = 1,
    MIDI_SOURCE_RTP    = 2
};

// --- Message MIDI parsé ---
struct MidiMessage {
    MidiMessageType type;       // Type de message (status byte masqué)
    uint8_t channel;            // Canal MIDI (0-15)
    uint8_t data1;              // Note number ou CC number
    uint8_t data2;              // Velocity ou CC value
    uint32_t timestamp_us;      // Timestamp d'arrivée (esp_timer_get_time)
    MidiTransportSource source; // Source du message
};

// --- Configuration WiFi ---
struct WiFiConfig {
    char ssid[33];              // SSID réseau (max 32 chars + null)
    char password[65];          // Mot de passe (max 64 chars + null)
    char hostname[32];          // Nom d'hôte mDNS
    bool enabled;               // WiFi activé
    bool ap_fallback;           // Démarrer en AP si STA échoue
};

// --- Configuration entrée MIDI ---
struct MidiInputConfig {
    bool serial_enabled;        // Entrée Serial MIDI active
    bool udp_enabled;           // Entrée UDP MIDI active
    bool rtp_enabled;           // Entrée RTP-MIDI (AppleMIDI) active
    uint16_t udp_port;          // Port UDP écoute
    uint16_t rtp_port;          // Port RTP-MIDI
    uint16_t jitter_buffer_ms;  // Profondeur jitter buffer (ms)
    uint8_t serial_rx_pin;      // GPIO pour Serial MIDI RX
};

#endif // MIDI_TYPES_H
