#include "midi_transport.h"
#include <AppleMIDI.h>

// ============================================================================
// PlayMode — MIDI Transport Layer (implementation)
// ============================================================================

// RTP-MIDI session and MIDI interface (AppleMIDI v3.x).
//
// The AppleMIDI API changed in v3.x:
//   - setHandleNoteOn/Off/CC are NO LONGER on AppleMIDISession
//   - They are on MidiInterface (object "MIDI" created by the macro below)
//   - setHandleConnected/Disconnected remain on the session (AppleRTP)
//
// APPLEMIDI_CREATE_INSTANCE creates two globals:
//   AppleRTP -> AppleMIDISession<WiFiUDP>  (network session, port 5004)
//   MIDI     -> MidiInterface              (callbacks NoteOn/Off/CC + read())
APPLEMIDI_CREATE_INSTANCE(WiFiUDP, AppleRTP, "play-mode", 5004);

static MidiTransport* g_transportInstance = nullptr;

// AppleMIDI callbacks — network connection
static void onAppleMidiConnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name) {
    Serial.printf("[RTP-MIDI] Connected: %s\n", name);
}

static void onAppleMidiDisconnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc) {
    Serial.println("[RTP-MIDI] Disconnected");
}

static void onAppleMidiNoteOn(byte channel, byte note, byte velocity) {
    if (g_transportInstance == nullptr) return;

    MidiMessage msg;
    msg.type = MIDI_NOTE_ON;
    msg.channel = channel;
    msg.data1 = note;
    msg.data2 = velocity;
    msg.timestamp_us = (uint32_t)esp_timer_get_time();
    msg.source = MIDI_SOURCE_RTP;

    g_transportInstance->deliverMessage(msg, MIDI_SOURCE_RTP);
}

static void onAppleMidiNoteOff(byte channel, byte note, byte velocity) {
    if (g_transportInstance == nullptr) return;

    MidiMessage msg;
    msg.type = MIDI_NOTE_OFF;
    msg.channel = channel;
    msg.data1 = note;
    msg.data2 = velocity;
    msg.timestamp_us = (uint32_t)esp_timer_get_time();
    msg.source = MIDI_SOURCE_RTP;

    g_transportInstance->deliverMessage(msg, MIDI_SOURCE_RTP);
}

static void onAppleMidiControlChange(byte channel, byte number, byte value) {
    if (g_transportInstance == nullptr) return;

    MidiMessage msg;
    msg.type = MIDI_CONTROL_CHANGE;
    msg.channel = channel;
    msg.data1 = number;
    msg.data2 = value;
    msg.timestamp_us = (uint32_t)esp_timer_get_time();
    msg.source = MIDI_SOURCE_RTP;

    g_transportInstance->deliverMessage(msg, MIDI_SOURCE_RTP);
}

// ============================================================================
// Construction / Initialization
// ============================================================================

MidiTransport::MidiTransport(JitterBuffer& jitterBuffer)
    : _jitterBuffer(jitterBuffer),
      _serialActive(false),
      _udpActive(false),
      _rtpActive(false),
      _serialBytes(0),
      _udpPackets(0),
      _rtpPackets(0) {
    memset(&_config, 0, sizeof(_config));
}

bool MidiTransport::begin(const MidiInputConfig& config) {
    _config = config;
    bool any_active = false;

    if (_config.serial_enabled) {
        if (initSerial()) any_active = true;
    }

    if (_config.udp_enabled) {
        if (initUDP()) any_active = true;
    }

    if (_config.rtp_enabled) {
        if (initRTP()) any_active = true;
    }

    return any_active;
}

void MidiTransport::poll() {
    if (_serialActive) pollSerial();
    if (_udpActive)    pollUDP();
    if (_rtpActive)    pollRTP();
}

void MidiTransport::stop() {
    if (_serialActive) {
        Serial2.end();
        _serialActive = false;
    }
    if (_udpActive) {
        _udp.stop();
        _udpActive = false;
    }
    if (_rtpActive) {
        _rtpActive = false;
        g_transportInstance = nullptr;
    }
    Serial.println("[MIDI-TR] All transports stopped");
}

uint32_t MidiTransport::getSerialByteCount() const { return _serialBytes; }
uint32_t MidiTransport::getUdpPacketCount() const { return _udpPackets; }
uint32_t MidiTransport::getRtpPacketCount() const { return _rtpPackets; }

// ============================================================================
// Serial MIDI Initialization (Serial2, 31250 baud)
// ============================================================================
bool MidiTransport::initSerial() {
    Serial2.begin(MIDI_SERIAL_BAUD, SERIAL_8N1, _config.serial_rx_pin, -1);
    _serialParser.reset();
    _serialActive = true;
    Serial.printf("[MIDI-TR] Serial MIDI active (GPIO %d, %d baud)\n",
                  _config.serial_rx_pin, MIDI_SERIAL_BAUD);
    return true;
}

// ============================================================================
// UDP MIDI Initialization
// ============================================================================
bool MidiTransport::initUDP() {
    if (_udp.begin(_config.udp_port)) {
        _udpParser.reset();
        _udpActive = true;
        Serial.printf("[MIDI-TR] UDP MIDI active (port %d)\n", _config.udp_port);
        return true;
    }
    Serial.println("[MIDI-TR] UDP MIDI init failed");
    return false;
}

// ============================================================================
// RTP-MIDI Initialization (AppleMIDI v3.x)
// ============================================================================
bool MidiTransport::initRTP() {
    g_transportInstance = this;

    // Connection / disconnection -> on the session via getTransport()
    // (AppleRTP is the MidiInterface in this version of the library;
    //  the underlying session is accessible via getTransport())
    AppleRTP.getTransport()->setHandleConnected(onAppleMidiConnected);
    AppleRTP.getTransport()->setHandleDisconnected(onAppleMidiDisconnected);

    // MIDI messages -> directly on the MidiInterface AppleRTP
    AppleRTP.setHandleNoteOn(onAppleMidiNoteOn);
    AppleRTP.setHandleNoteOff(onAppleMidiNoteOff);
    AppleRTP.setHandleControlChange(onAppleMidiControlChange);
    AppleRTP.begin(MIDI_CHANNEL_OMNI);

    _rtpActive = true;
    Serial.println("[MIDI-TR] RTP-MIDI (AppleMIDI) active (port 5004)");
    return true;
}

// ============================================================================
// Poll Serial MIDI
// ============================================================================
void MidiTransport::pollSerial() {
    while (Serial2.available()) {
        uint8_t byte = Serial2.read();
        _serialBytes++;

        if (_serialParser.feed(byte)) {
            MidiMessage msg = _serialParser.getMessage();
            deliverMessage(msg, MIDI_SOURCE_SERIAL);
        }
    }
}

// ============================================================================
// Poll UDP MIDI
// ============================================================================
void MidiTransport::pollUDP() {
    int packetSize = _udp.parsePacket();
    if (packetSize > 0) {
        _udpPackets++;
        uint8_t buf[256];
        int len = _udp.read(buf, min(packetSize, (int)sizeof(buf)));

        for (int i = 0; i < len; i++) {
            if (_udpParser.feed(buf[i])) {
                MidiMessage msg = _udpParser.getMessage();
                deliverMessage(msg, MIDI_SOURCE_UDP);
            }
        }
    }
}

// ============================================================================
// Poll RTP-MIDI (AppleMIDI v3.x)
// ============================================================================
void MidiTransport::pollRTP() {
    AppleRTP.read();
}

// ============================================================================
// Deliver a message to the jitter buffer
// ============================================================================
void MidiTransport::deliverMessage(MidiMessage& msg, MidiTransportSource source) {
    msg.source = source;
    if (!_jitterBuffer.insert(msg)) {
        Serial.println("[MIDI-TR] Jitter buffer full, message lost");
    }

    if (source == MIDI_SOURCE_RTP) {
        _rtpPackets++;
    }
}
