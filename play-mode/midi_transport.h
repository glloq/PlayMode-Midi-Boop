#ifndef MIDI_TRANSPORT_H
#define MIDI_TRANSPORT_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"
#include "midi_types.h"
#include "midi_parser.h"
#include "jitter_buffer.h"

// ============================================================================
// PlayMode — MIDI Transport Layer (Phase 3)
// ============================================================================
//
// Manages the three MIDI input sources:
//   - Serial MIDI (31250 baud via Serial2)
//   - UDP MIDI (raw bytes over WiFi)
//   - RTP-MIDI (AppleMIDI via lathoub/Arduino-AppleMIDI-Library)
//
// Each transport has its own MIDI parser.
// Parsed messages are inserted into the JitterBuffer.
//

class MidiTransport {
public:
    MidiTransport(JitterBuffer& jitterBuffer);

    // Initializes the configured transports.
    // Must be called after WiFi connection (for network transports).
    bool begin(const MidiInputConfig& config);

    // Reads data from all active transports.
    // Call on each iteration of loop().
    void poll();

    // Stops all transports
    void stop();

    // Stats
    uint32_t getSerialByteCount() const;
    uint32_t getUdpPacketCount() const;
    uint32_t getRtpPacketCount() const;

    // Delivers a parsed message to the jitter buffer (public for AppleMIDI callbacks)
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

    // Per-transport individual poll
    void pollSerial();
    void pollUDP();
    void pollRTP();

    // Per-transport initialization
    bool initSerial();
    bool initUDP();
    bool initRTP();
};

#endif // MIDI_TRANSPORT_H
