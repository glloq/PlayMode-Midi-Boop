#include "midi_transport.h"
#include <AppleMIDI.h>

// ============================================================================
// PlayMode Midi B∞p — Couche Transport MIDI (implémentation)
// ============================================================================

// Session RTP-MIDI et interface MIDI (AppleMIDI v3.x).
//
// L'API AppleMIDI a changé en v3.x :
//   - setHandleNoteOn/Off/CC  ne sont PLUS sur AppleMIDISession
//   - Ils sont sur MidiInterface (objet "MIDI" créé par la macro ci-dessous)
//   - setHandleConnected/Disconnected restent sur la session (AppleRTP)
//
// APPLEMIDI_CREATE_INSTANCE crée deux globals :
//   AppleRTP → AppleMIDISession<WiFiUDP>  (session réseau, port 5004)
//   MIDI     → MidiInterface              (callbacks NoteOn/Off/CC + read())
APPLEMIDI_CREATE_INSTANCE(WiFiUDP, AppleRTP, "midi-boop", 5004);

static MidiTransport* g_transportInstance = nullptr;

// Callbacks AppleMIDI — connexion réseau
static void onAppleMidiConnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name) {
    Serial.printf("[RTP-MIDI] Connecté : %s\n", name);
}

static void onAppleMidiDisconnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc) {
    Serial.println("[RTP-MIDI] Déconnecté");
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
// Construction / Initialisation
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
    Serial.println("[MIDI-TR] Tous les transports arrêtés");
}

uint32_t MidiTransport::getSerialByteCount() const { return _serialBytes; }
uint32_t MidiTransport::getUdpPacketCount() const { return _udpPackets; }
uint32_t MidiTransport::getRtpPacketCount() const { return _rtpPackets; }

// ============================================================================
// Initialisation Serial MIDI (Serial2, 31250 baud)
// ============================================================================
bool MidiTransport::initSerial() {
    Serial2.begin(MIDI_SERIAL_BAUD, SERIAL_8N1, _config.serial_rx_pin, -1);
    _serialParser.reset();
    _serialActive = true;
    Serial.printf("[MIDI-TR] Serial MIDI actif (GPIO %d, %d baud)\n",
                  _config.serial_rx_pin, MIDI_SERIAL_BAUD);
    return true;
}

// ============================================================================
// Initialisation UDP MIDI
// ============================================================================
bool MidiTransport::initUDP() {
    if (_udp.begin(_config.udp_port)) {
        _udpParser.reset();
        _udpActive = true;
        Serial.printf("[MIDI-TR] UDP MIDI actif (port %d)\n", _config.udp_port);
        return true;
    }
    Serial.println("[MIDI-TR] Échec init UDP MIDI");
    return false;
}

// ============================================================================
// Initialisation RTP-MIDI (AppleMIDI v3.x)
// ============================================================================
bool MidiTransport::initRTP() {
    g_transportInstance = this;

    // Connexion / déconnexion → sur la session AppleMIDI
    AppleRTP.setHandleConnected(onAppleMidiConnected);
    AppleRTP.setHandleDisconnected(onAppleMidiDisconnected);

    // Messages MIDI → sur l'interface MidiInterface (API v3.x, non sur la session)
    MIDI.setHandleNoteOn(onAppleMidiNoteOn);
    MIDI.setHandleNoteOff(onAppleMidiNoteOff);
    MIDI.setHandleControlChange(onAppleMidiControlChange);
    MIDI.begin(MIDI_CHANNEL_OMNI);

    _rtpActive = true;
    Serial.println("[MIDI-TR] RTP-MIDI (AppleMIDI) actif (port 5004)");
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
    MIDI.read();
}

// ============================================================================
// Délivrer un message au jitter buffer
// ============================================================================
void MidiTransport::deliverMessage(MidiMessage& msg, MidiTransportSource source) {
    msg.source = source;
    if (!_jitterBuffer.insert(msg)) {
        Serial.println("[MIDI-TR] Jitter buffer plein, message perdu");
    }

    if (source == MIDI_SOURCE_RTP) {
        _rtpPackets++;
    }
}
