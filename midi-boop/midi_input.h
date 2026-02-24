#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode Midi B∞p — MIDI Input Layer + Jitter Buffer
// ============================================================================
//
// Gère 3 sources MIDI : Serial (DIN), UDP WiFi, RTP-MIDI (AppleMIDI)
// Inclut un jitter buffer configurable pour lisser le timing réseau
// Tourne sur Core 0, appelé dans loop()
//

class MidiInput {
public:
    MidiInput();

    // Initialise les sources MIDI configurées + WiFi si nécessaire
    bool begin(const WiFiConfig& wifi_config);

    // Appelé dans loop() — lit toutes les sources, remplit le jitter buffer
    void update();

    // Retire le prochain événement prêt du jitter buffer
    // Retourne true si un événement est disponible
    bool readEvent(MidiEvent& event);

    // --- Configuration sources ---

    void enableSerial(bool enable);
    void enableUDP(bool enable);
    void enableRTP(bool enable);

    bool isSerialEnabled() const;
    bool isUDPEnabled() const;
    bool isRTPEnabled() const;

    // --- Jitter buffer ---

    void setJitterDelay(uint16_t delay_ms);
    uint16_t getJitterDelay() const;

    // --- WiFi ---

    bool connectWiFi(const WiFiConfig& config);
    bool isWiFiConnected() const;

    // --- Statistiques ---

    uint32_t getReceivedCount() const;
    uint32_t getBufferedCount() const;
    uint32_t getDroppedCount() const;

private:
    // --- Sources actives ---
    bool _serial_enabled;
    bool _udp_enabled;
    bool _rtp_enabled;

    // --- WiFi / réseau ---
    WiFiUDP _udp;
    bool _wifi_connected;

    // --- AppleMIDI ---
    // L'instance AppleMIDI est gérée via des fonctions globales
    // car la bibliothèque utilise des templates et callbacks globaux
    bool _rtp_initialized;

    // --- Jitter Buffer ---
    struct JitterEntry {
        MidiEvent event;
        uint32_t release_time_us;  // Quand libérer cet événement
        bool valid;
    };
    JitterEntry _jitter_buffer[JITTER_BUFFER_SIZE];
    uint8_t _jitter_count;       // Nombre d'entrées valides
    uint16_t _jitter_delay_ms;   // Délai appliqué (configurable)

    // --- Statistiques ---
    uint32_t _received_count;
    uint32_t _dropped_count;

    // --- Parseur MIDI série ---
    enum ParserState : uint8_t {
        PARSER_IDLE,
        PARSER_STATUS,       // Status byte reçu, attend data1
        PARSER_DATA1         // data1 reçu, attend data2
    };
    ParserState _parser_state;
    uint8_t _parser_status;   // Dernier status byte (running status)
    uint8_t _parser_data1;    // Premier data byte

    // --- Méthodes internes ---

    // Lit les données MIDI depuis Serial2
    void readSerial();

    // Lit les paquets MIDI UDP
    void readUDP();

    // Met à jour la session AppleMIDI
    void readRTP();

    // Parse un byte MIDI série (machine à états avec running status)
    void parseSerialByte(uint8_t byte);

    // Parse un paquet UDP contenant des messages MIDI bruts
    void parseUDPPacket(const uint8_t* data, size_t len);

    // Insère un événement dans le jitter buffer
    void bufferEvent(const MidiEvent& event);

    // Crée un MidiEvent à partir des données parsées
    void emitMidiEvent(MidiMessageType type, uint8_t channel,
                       uint8_t data1, uint8_t data2, MidiSource source);

    // Retourne le nombre d'octets de données attendus pour un type de message
    uint8_t expectedDataBytes(uint8_t status);
};

#endif // MIDI_INPUT_H
