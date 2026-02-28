#ifndef MIDI_TRANSPORT_H
#define MIDI_TRANSPORT_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"
#include "midi_types.h"
#include "midi_parser.h"
#include "jitter_buffer.h"

// ============================================================================
// PlayMode — Couche Transport MIDI (Phase 3)
// ============================================================================
//
// Gère les trois sources d'entrée MIDI :
//   - Serial MIDI (31250 baud via Serial2)
//   - UDP MIDI (raw bytes over WiFi)
//   - RTP-MIDI (AppleMIDI via lathoub/Arduino-AppleMIDI-Library)
//
// Chaque transport a son propre parseur MIDI.
// Les messages parsés sont insérés dans le JitterBuffer.
//

class MidiTransport {
public:
    MidiTransport(JitterBuffer& jitterBuffer);

    // Initialise les transports configurés.
    // Doit être appelé après la connexion WiFi (pour les transports réseau).
    bool begin(const MidiInputConfig& config);

    // Lit les données de tous les transports actifs.
    // Appeler à chaque itération de loop().
    void poll();

    // Arrête tous les transports
    void stop();

    // Stats
    uint32_t getSerialByteCount() const;
    uint32_t getUdpPacketCount() const;
    uint32_t getRtpPacketCount() const;

    // Délivre un message parsé au jitter buffer (public pour callbacks AppleMIDI)
    void deliverMessage(MidiMessage& msg, MidiTransportSource source);

private:
    JitterBuffer& _jitterBuffer;
    MidiInputConfig _config;

    // Serial MIDI
    MidiParser _serialParser;
    bool _serialActive;

    // UDP MIDI
    WiFiUDP _udp;
    MidiParser _udpParser;
    bool _udpActive;

    // RTP-MIDI (AppleMIDI)
    bool _rtpActive;

    // Stats
    uint32_t _serialBytes;
    uint32_t _udpPackets;
    uint32_t _rtpPackets;

    // Poll individuel par transport
    void pollSerial();
    void pollUDP();
    void pollRTP();

    // Initialisation par transport
    bool initSerial();
    bool initUDP();
    bool initRTP();
};

#endif // MIDI_TRANSPORT_H
