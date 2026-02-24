#include "midi_input.h"

// ============================================================================
// PlayMode Midi B∞p — MIDI Input Layer + Jitter Buffer (implémentation)
// ============================================================================

// --- AppleMIDI (bibliothèque lathoub) ---
// Inclusion conditionnelle : décommenter quand la bibliothèque est installée
// #define USE_APPLE_MIDI
#ifdef USE_APPLE_MIDI
#include <AppleMIDI.h>
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();
#endif

// Pointeur global vers l'instance MidiInput pour les callbacks AppleMIDI
static MidiInput* g_midi_input_instance = nullptr;

// ============================================================================
// Constructeur
// ============================================================================

MidiInput::MidiInput()
    : _serial_enabled(true),
      _udp_enabled(false),
      _rtp_enabled(false),
      _wifi_connected(false),
      _rtp_initialized(false),
      _jitter_count(0),
      _jitter_delay_ms(JITTER_BUFFER_MS),
      _received_count(0),
      _dropped_count(0),
      _parser_state(PARSER_IDLE),
      _parser_status(0),
      _parser_data1(0) {
    memset(_jitter_buffer, 0, sizeof(_jitter_buffer));
    g_midi_input_instance = this;
}

// ============================================================================
// Initialisation
// ============================================================================

bool MidiInput::begin(const WiFiConfig& wifi_config) {
    Serial.println("[MIDI] Initialisation MIDI Input...");

    // 1. Initialiser Serial2 pour MIDI DIN (toujours actif)
    Serial2.begin(MIDI_SERIAL_BAUD, SERIAL_8N1, MIDI_SERIAL_RX_PIN, -1);
    _serial_enabled = true;
    Serial.printf("[MIDI] Serial2 initialisé (RX pin %d, %d baud)\n",
                  MIDI_SERIAL_RX_PIN, MIDI_SERIAL_BAUD);

    // 2. Connexion WiFi si configuré
    if (wifi_config.enabled) {
        if (connectWiFi(wifi_config)) {
            // 3. Démarrer UDP listener
            if (_udp.begin(MIDI_UDP_PORT)) {
                _udp_enabled = true;
                Serial.printf("[MIDI] UDP listener démarré sur port %d\n", MIDI_UDP_PORT);
            } else {
                Serial.println("[MIDI] Erreur démarrage UDP");
            }

            // 4. Initialiser AppleMIDI / RTP-MIDI
#ifdef USE_APPLE_MIDI
            MIDI.begin();
            AppleMIDI.setName("MidiBoop");

            // Callbacks AppleMIDI
            AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t& ssrc,
                                            const char* name) {
                Serial.printf("[MIDI] RTP-MIDI connecté : %s\n", name);
            });

            AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t& ssrc) {
                Serial.println("[MIDI] RTP-MIDI déconnecté");
            });

            // Callbacks MIDI via AppleMIDI
            MIDI.setHandleNoteOn([](byte channel, byte note, byte velocity) {
                if (g_midi_input_instance) {
                    if (velocity == 0) {
                        g_midi_input_instance->emitMidiEvent(
                            MIDI_MSG_NOTE_OFF, channel - 1, note, 0, MIDI_SOURCE_RTP);
                    } else {
                        g_midi_input_instance->emitMidiEvent(
                            MIDI_MSG_NOTE_ON, channel - 1, note, velocity, MIDI_SOURCE_RTP);
                    }
                }
            });

            MIDI.setHandleNoteOff([](byte channel, byte note, byte velocity) {
                if (g_midi_input_instance) {
                    g_midi_input_instance->emitMidiEvent(
                        MIDI_MSG_NOTE_OFF, channel - 1, note, velocity, MIDI_SOURCE_RTP);
                }
            });

            MIDI.setHandleControlChange([](byte channel, byte number, byte value) {
                if (g_midi_input_instance) {
                    g_midi_input_instance->emitMidiEvent(
                        MIDI_MSG_CC, channel - 1, number, value, MIDI_SOURCE_RTP);
                }
            });

            MIDI.setHandleProgramChange([](byte channel, byte number) {
                if (g_midi_input_instance) {
                    g_midi_input_instance->emitMidiEvent(
                        MIDI_MSG_PROGRAM_CHANGE, channel - 1, number, 0, MIDI_SOURCE_RTP);
                }
            });

            _rtp_enabled = true;
            _rtp_initialized = true;
            Serial.printf("[MIDI] AppleMIDI initialisé (port %d)\n", MIDI_RTP_PORT);
#else
            Serial.println("[MIDI] AppleMIDI non compilé (USE_APPLE_MIDI non défini)");
#endif
        }
    } else {
        Serial.println("[MIDI] WiFi désactivé — sources réseau inactives");
    }

    Serial.printf("[MIDI] Sources actives : Serial=%d, UDP=%d, RTP=%d\n",
                  _serial_enabled, _udp_enabled, _rtp_enabled);
    Serial.printf("[MIDI] Jitter buffer : %dms (%d slots)\n",
                  _jitter_delay_ms, JITTER_BUFFER_SIZE);

    return true;
}

// ============================================================================
// Mise à jour — Appelé dans loop()
// ============================================================================

void MidiInput::update() {
    // Lire toutes les sources MIDI actives
    if (_serial_enabled) {
        readSerial();
    }

    if (_udp_enabled) {
        readUDP();
    }

    if (_rtp_enabled) {
        readRTP();
    }
}

// ============================================================================
// Lecture du jitter buffer — Retourne le prochain événement prêt
// ============================================================================

bool MidiInput::readEvent(MidiEvent& event) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    int best_idx = -1;
    uint32_t best_time = UINT32_MAX;

    // Trouver l'événement avec le plus petit release_time qui est prêt
    for (uint8_t i = 0; i < JITTER_BUFFER_SIZE; i++) {
        if (_jitter_buffer[i].valid &&
            _jitter_buffer[i].release_time_us <= now_us &&
            _jitter_buffer[i].release_time_us < best_time) {
            best_idx = i;
            best_time = _jitter_buffer[i].release_time_us;
        }
    }

    if (best_idx >= 0) {
        event = _jitter_buffer[best_idx].event;
        _jitter_buffer[best_idx].valid = false;
        _jitter_count--;
        return true;
    }

    return false;
}

// ============================================================================
// Configuration des sources
// ============================================================================

void MidiInput::enableSerial(bool enable) { _serial_enabled = enable; }
void MidiInput::enableUDP(bool enable) { _udp_enabled = enable; }
void MidiInput::enableRTP(bool enable) { _rtp_enabled = enable; }

bool MidiInput::isSerialEnabled() const { return _serial_enabled; }
bool MidiInput::isUDPEnabled() const { return _udp_enabled; }
bool MidiInput::isRTPEnabled() const { return _rtp_enabled; }

// ============================================================================
// Jitter buffer configuration
// ============================================================================

void MidiInput::setJitterDelay(uint16_t delay_ms) {
    if (delay_ms < JITTER_BUFFER_MIN_MS) delay_ms = JITTER_BUFFER_MIN_MS;
    if (delay_ms > JITTER_BUFFER_MAX_MS) delay_ms = JITTER_BUFFER_MAX_MS;
    _jitter_delay_ms = delay_ms;
    Serial.printf("[MIDI] Jitter delay = %dms\n", _jitter_delay_ms);
}

uint16_t MidiInput::getJitterDelay() const { return _jitter_delay_ms; }

// ============================================================================
// WiFi
// ============================================================================

bool MidiInput::connectWiFi(const WiFiConfig& config) {
    if (config.ap_mode) {
        // Mode Access Point
        Serial.printf("[MIDI] Démarrage WiFi AP : %s\n", config.ssid);
        WiFi.softAP(config.ssid, config.password);
        _wifi_connected = true;
        Serial.printf("[MIDI] AP démarré — IP : %s\n",
                      WiFi.softAPIP().toString().c_str());
    } else {
        // Mode Station
        Serial.printf("[MIDI] Connexion WiFi : %s\n", config.ssid);
        WiFi.begin(config.ssid, config.password);

        // Attendre la connexion (max 10 secondes)
        uint8_t attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            _wifi_connected = true;
            Serial.printf("[MIDI] WiFi connecté — IP : %s\n",
                          WiFi.localIP().toString().c_str());
        } else {
            _wifi_connected = false;
            Serial.println("[MIDI] Échec connexion WiFi");
            return false;
        }
    }
    return true;
}

bool MidiInput::isWiFiConnected() const { return _wifi_connected; }

// ============================================================================
// Statistiques
// ============================================================================

uint32_t MidiInput::getReceivedCount() const { return _received_count; }
uint32_t MidiInput::getBufferedCount() const { return _jitter_count; }
uint32_t MidiInput::getDroppedCount() const { return _dropped_count; }

// ============================================================================
// Lecture MIDI série (Serial2 / MIDI DIN)
// ============================================================================

void MidiInput::readSerial() {
    // Lire tous les octets disponibles
    while (Serial2.available()) {
        uint8_t byte = Serial2.read();
        parseSerialByte(byte);
    }
}

// ============================================================================
// Parser MIDI série — Machine à états avec running status
// ============================================================================

void MidiInput::parseSerialByte(uint8_t byte) {
    // Ignorer les messages System Realtime (0xF8-0xFF)
    if (byte >= 0xF8) return;

    // Ignorer les messages System Common (0xF0-0xF7) pour l'instant
    if (byte >= 0xF0 && byte <= 0xF7) {
        _parser_state = PARSER_IDLE;
        return;
    }

    // Status byte (0x80-0xEF)
    if (byte & 0x80) {
        _parser_status = byte;
        _parser_state = PARSER_STATUS;
        return;
    }

    // Data byte (0x00-0x7F)
    switch (_parser_state) {
        case PARSER_IDLE:
            // Running status : réutiliser le dernier status byte
            if (_parser_status != 0) {
                _parser_data1 = byte;
                if (expectedDataBytes(_parser_status) == 1) {
                    // Message à 1 data byte (Program Change)
                    uint8_t channel = _parser_status & 0x0F;
                    uint8_t type = _parser_status & 0xF0;
                    emitMidiEvent((MidiMessageType)type, channel,
                                  _parser_data1, 0, MIDI_SOURCE_SERIAL);
                    // Rester en PARSER_IDLE pour running status
                } else {
                    _parser_state = PARSER_DATA1;
                }
            }
            break;

        case PARSER_STATUS:
            _parser_data1 = byte;
            if (expectedDataBytes(_parser_status) == 1) {
                // Message à 1 data byte (Program Change)
                uint8_t channel = _parser_status & 0x0F;
                uint8_t type = _parser_status & 0xF0;
                emitMidiEvent((MidiMessageType)type, channel,
                              _parser_data1, 0, MIDI_SOURCE_SERIAL);
                _parser_state = PARSER_IDLE;
            } else {
                _parser_state = PARSER_DATA1;
            }
            break;

        case PARSER_DATA1:
            {
                // Message complet à 2 data bytes (Note On/Off, CC)
                uint8_t channel = _parser_status & 0x0F;
                uint8_t type = _parser_status & 0xF0;
                emitMidiEvent((MidiMessageType)type, channel,
                              _parser_data1, byte, MIDI_SOURCE_SERIAL);
                _parser_state = PARSER_IDLE;
            }
            break;
    }
}

uint8_t MidiInput::expectedDataBytes(uint8_t status) {
    uint8_t type = status & 0xF0;
    switch (type) {
        case 0xC0: // Program Change
        case 0xD0: // Channel Pressure
            return 1;
        default:   // Note On/Off, CC, Pitch Bend, etc.
            return 2;
    }
}

// ============================================================================
// Lecture MIDI UDP
// ============================================================================

void MidiInput::readUDP() {
    int packetSize = _udp.parsePacket();
    if (packetSize <= 0) return;

    // Lire le paquet (max 256 octets pour éviter les débordements)
    uint8_t buffer[256];
    int len = _udp.read(buffer, min(packetSize, (int)sizeof(buffer)));

    if (len > 0) {
        parseUDPPacket(buffer, len);
    }
}

void MidiInput::parseUDPPacket(const uint8_t* data, size_t len) {
    // Format simple : messages MIDI bruts de 3 octets consécutifs
    // Chaque message = status + data1 + data2
    size_t pos = 0;

    while (pos < len) {
        // Vérifier qu'on a un status byte
        if (!(data[pos] & 0x80)) {
            pos++;
            continue;
        }

        uint8_t status = data[pos];
        uint8_t type = status & 0xF0;
        uint8_t channel = status & 0x0F;

        // Ignorer les messages système
        if (status >= 0xF0) {
            pos++;
            continue;
        }

        uint8_t data_bytes = expectedDataBytes(status);

        // Vérifier qu'on a assez de données
        if (pos + 1 + data_bytes > len) break;

        uint8_t d1 = data[pos + 1];
        uint8_t d2 = (data_bytes == 2) ? data[pos + 2] : 0;

        emitMidiEvent((MidiMessageType)type, channel, d1, d2, MIDI_SOURCE_UDP);

        pos += 1 + data_bytes;
    }
}

// ============================================================================
// Lecture RTP-MIDI (AppleMIDI)
// ============================================================================

void MidiInput::readRTP() {
#ifdef USE_APPLE_MIDI
    if (_rtp_initialized) {
        // La bibliothèque AppleMIDI gère la réception via MIDI.read()
        // Les callbacks enregistrés dans begin() appellent emitMidiEvent()
        MIDI.read();
    }
#endif
}

// ============================================================================
// Émission d'un événement MIDI → jitter buffer
// ============================================================================

void MidiInput::emitMidiEvent(MidiMessageType type, uint8_t channel,
                               uint8_t data1, uint8_t data2, MidiSource source) {
    _received_count++;

    // Gestion Note On avec velocity 0 = Note Off (convention MIDI standard)
    MidiMessageType final_type = type;
    if (type == MIDI_MSG_NOTE_ON && data2 == 0) {
        final_type = MIDI_MSG_NOTE_OFF;
    }

    MidiEvent event;
    event.receive_time_us = (uint32_t)esp_timer_get_time();
    event.type = final_type;
    event.channel = channel;
    event.data1 = data1;
    event.data2 = data2;
    event.source = source;

    bufferEvent(event);
}

// ============================================================================
// Jitter Buffer — Insertion
// ============================================================================

void MidiInput::bufferEvent(const MidiEvent& event) {
    // Trouver un slot libre dans le buffer
    for (uint8_t i = 0; i < JITTER_BUFFER_SIZE; i++) {
        if (!_jitter_buffer[i].valid) {
            _jitter_buffer[i].event = event;
            _jitter_buffer[i].release_time_us =
                event.receive_time_us + ((uint32_t)_jitter_delay_ms * 1000);
            _jitter_buffer[i].valid = true;
            _jitter_count++;
            return;
        }
    }

    // Buffer plein : trouver et écraser l'entrée la plus ancienne
    uint32_t oldest_time = UINT32_MAX;
    uint8_t oldest_idx = 0;

    for (uint8_t i = 0; i < JITTER_BUFFER_SIZE; i++) {
        if (_jitter_buffer[i].valid &&
            _jitter_buffer[i].event.receive_time_us < oldest_time) {
            oldest_time = _jitter_buffer[i].event.receive_time_us;
            oldest_idx = i;
        }
    }

    _jitter_buffer[oldest_idx].event = event;
    _jitter_buffer[oldest_idx].release_time_us =
        event.receive_time_us + ((uint32_t)_jitter_delay_ms * 1000);
    _jitter_buffer[oldest_idx].valid = true;
    _dropped_count++;
}
