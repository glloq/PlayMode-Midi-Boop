#include "config_manager.h"

// ============================================================================
// PlayMode Midi B∞p — Config Manager (implémentation)
// ============================================================================

ConfigManager::ConfigManager()
    : _actuator_count(0),
      _instrument_count(0),
      _routing_count(0),
      _version(CONFIG_VERSION) {
    memset(&_wifi_config, 0, sizeof(_wifi_config));
    memset(&_midi_input_config, 0, sizeof(_midi_input_config));
    memset(_actuators, 0, sizeof(_actuators));
    memset(_instruments, 0, sizeof(_instruments));
    memset(_routing_configs, 0, sizeof(_routing_configs));
}

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {  // true = format si échec
        Serial.println("[CONFIG] Erreur montage LittleFS");
        return false;
    }
    Serial.println("[CONFIG] LittleFS monté");

    // Charger la config si elle existe, sinon charger les défauts
    if (configExists()) {
        if (!load()) {
            Serial.println("[CONFIG] Erreur chargement, utilisation des défauts");
            loadDefaults();
        }
    } else {
        Serial.println("[CONFIG] Aucune config trouvée, chargement des défauts");
        loadDefaults();
        save();
    }

    return true;
}

bool ConfigManager::load() {
    File file = LittleFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        Serial.println("[CONFIG] Impossible d'ouvrir le fichier config");
        return false;
    }

    // Allouer le document JSON (taille adaptée)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[CONFIG] Erreur JSON : %s\n", error.c_str());
        return false;
    }

    // Version
    _version = doc["version"] | CONFIG_VERSION;

    // WiFi
    if (doc.containsKey("wifi")) {
        JsonObject wifiObj = doc["wifi"].as<JsonObject>();
        deserializeWiFi(_wifi_config, wifiObj);
    }

    // MIDI Input
    if (doc.containsKey("midi_input")) {
        JsonObject midiObj = doc["midi_input"].as<JsonObject>();
        deserializeMidiInput(_midi_input_config, midiObj);
    }

    // Bus
    JsonArray busArray = doc["buses"].as<JsonArray>();
    uint8_t busIdx = 0;
    for (JsonObject busObj : busArray) {
        if (busIdx >= 2) break;
        deserializeBus(_buses[busIdx], busObj);
        busIdx++;
    }

    // Actionneurs
    _actuator_count = 0;
    JsonArray actArray = doc["actuators"].as<JsonArray>();
    for (JsonObject actObj : actArray) {
        if (_actuator_count >= MAX_ACTUATORS) break;
        deserializeActuator(_actuators[_actuator_count], actObj);
        _actuator_count++;
    }

    // Instruments
    _instrument_count = 0;
    JsonArray instArray = doc["instruments"].as<JsonArray>();
    for (JsonObject instObj : instArray) {
        if (_instrument_count >= MAX_INSTRUMENTS) break;
        deserializeInstrument(_instruments[_instrument_count], instObj);
        _instrument_count++;
    }

    // Routage MIDI
    _routing_count = 0;
    if (doc.containsKey("routing")) {
        JsonArray routeArray = doc["routing"].as<JsonArray>();
        for (JsonObject routeObj : routeArray) {
            if (_routing_count >= MAX_INSTRUMENTS) break;
            deserializeRouting(_routing_configs[_routing_count], routeObj);
            _routing_count++;
        }
    }

    Serial.printf("[CONFIG] Chargé : %d actionneurs, %d instruments, %d routages\n",
                  _actuator_count, _instrument_count, _routing_count);
    return true;
}

bool ConfigManager::save() {
    JsonDocument doc;

    doc["version"] = _version;

    // WiFi
    JsonObject wifiObj = doc["wifi"].to<JsonObject>();
    serializeWiFi(_wifi_config, wifiObj);

    // MIDI Input
    JsonObject midiObj = doc["midi_input"].to<JsonObject>();
    serializeMidiInput(_midi_input_config, midiObj);

    // Bus
    JsonArray busArray = doc["buses"].to<JsonArray>();
    for (uint8_t i = 0; i < 2; i++) {
        JsonObject busObj = busArray.add<JsonObject>();
        serializeBus(_buses[i], busObj);
    }

    // Actionneurs
    JsonArray actArray = doc["actuators"].to<JsonArray>();
    for (uint8_t i = 0; i < _actuator_count; i++) {
        JsonObject actObj = actArray.add<JsonObject>();
        serializeActuator(_actuators[i], actObj);
    }

    // Instruments
    JsonArray instArray = doc["instruments"].to<JsonArray>();
    for (uint8_t i = 0; i < _instrument_count; i++) {
        JsonObject instObj = instArray.add<JsonObject>();
        serializeInstrument(_instruments[i], instObj);
    }

    // Routage MIDI
    JsonArray routeArray = doc["routing"].to<JsonArray>();
    for (uint8_t i = 0; i < _routing_count; i++) {
        JsonObject routeObj = routeArray.add<JsonObject>();
        serializeRouting(_routing_configs[i], routeObj);
    }

    File file = LittleFS.open(CONFIG_FILE_PATH, "w");
    if (!file) {
        Serial.println("[CONFIG] Impossible d'écrire le fichier config");
        return false;
    }

    serializeJsonPretty(doc, file);
    file.close();

    Serial.println("[CONFIG] Configuration sauvegardée");
    return true;
}

void ConfigManager::loadDefaults() {
    // Bus 0 — Servos
    _buses[0].id = 0;
    _buses[0].sda_pin = I2C0_SDA_PIN;
    _buses[0].scl_pin = I2C0_SCL_PIN;
    _buses[0].oe_pin = I2C0_OE_PIN;
    _buses[0].i2c_frequency = I2C0_FREQUENCY;
    _buses[0].pwm_frequency = PCA_SERVO_FREQ;
    _buses[0].enabled = true;
    _buses[0].pca_count = 0;

    // Bus 1 — Solénoïdes
    _buses[1].id = 1;
    _buses[1].sda_pin = I2C1_SDA_PIN;
    _buses[1].scl_pin = I2C1_SCL_PIN;
    _buses[1].oe_pin = I2C1_OE_PIN;
    _buses[1].i2c_frequency = I2C1_FREQUENCY;
    _buses[1].pwm_frequency = PCA_SOLENOID_FREQ;
    _buses[1].enabled = true;
    _buses[1].pca_count = 0;

    _actuator_count = 0;
    _instrument_count = 0;
    _routing_count = 0;
    _version = CONFIG_VERSION;

    // WiFi défauts
    strlcpy(_wifi_config.ssid, "MidiBoop", sizeof(_wifi_config.ssid));
    strlcpy(_wifi_config.password, "midiboop", sizeof(_wifi_config.password));
    strlcpy(_wifi_config.hostname, WIFI_DEFAULT_HOSTNAME, sizeof(_wifi_config.hostname));
    _wifi_config.enabled = false;
    _wifi_config.ap_fallback = true;

    // MIDI Input défauts
    _midi_input_config.serial_enabled = true;
    _midi_input_config.udp_enabled = true;
    _midi_input_config.rtp_enabled = true;
    _midi_input_config.udp_port = MIDI_UDP_PORT;
    _midi_input_config.rtp_port = MIDI_RTP_PORT;
    _midi_input_config.jitter_buffer_ms = MIDI_JITTER_BUFFER_MS;
    _midi_input_config.serial_rx_pin = MIDI_SERIAL_RX_PIN;

    Serial.println("[CONFIG] Défauts chargés");
}

bool ConfigManager::configExists() {
    return LittleFS.exists(CONFIG_FILE_PATH);
}

BusConfig* ConfigManager::getBuses() {
    return _buses;
}

uint8_t ConfigManager::getBusCount() const {
    return 2;
}

ActuatorConfig* ConfigManager::getActuators() {
    return _actuators;
}

uint8_t ConfigManager::getActuatorCount() const {
    return _actuator_count;
}

InstrumentConfig* ConfigManager::getInstruments() {
    return _instruments;
}

uint8_t ConfigManager::getInstrumentCount() const {
    return _instrument_count;
}

bool ConfigManager::addActuator(const ActuatorConfig& actuator) {
    if (_actuator_count >= MAX_ACTUATORS) return false;
    _actuators[_actuator_count] = actuator;
    _actuator_count++;
    return true;
}

bool ConfigManager::addInstrument(const InstrumentConfig& instrument) {
    if (_instrument_count >= MAX_INSTRUMENTS) return false;
    _instruments[_instrument_count] = instrument;
    _instrument_count++;
    return true;
}

uint8_t ConfigManager::getVersion() const {
    return _version;
}

WiFiConfig* ConfigManager::getWiFiConfig() {
    return &_wifi_config;
}

void ConfigManager::setWiFiConfig(const WiFiConfig& config) {
    _wifi_config = config;
}

MidiInputConfig* ConfigManager::getMidiInputConfig() {
    return &_midi_input_config;
}

MidiRoutingConfig* ConfigManager::getRoutingConfigs() {
    return _routing_configs;
}

uint8_t ConfigManager::getRoutingCount() const {
    return _routing_count;
}

bool ConfigManager::addRoutingConfig(const MidiRoutingConfig& routing) {
    if (_routing_count >= MAX_INSTRUMENTS) return false;
    _routing_configs[_routing_count] = routing;
    _routing_count++;
    return true;
}

MidiRoutingConfig* ConfigManager::getRoutingForInstrument(uint8_t instrument_index) {
    for (uint8_t i = 0; i < _routing_count; i++) {
        if (_routing_configs[i].instrument_index == instrument_index) {
            return &_routing_configs[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Sérialisation / Désérialisation — Actionneurs
// ============================================================================

void ConfigManager::serializeActuator(const ActuatorConfig& act, JsonObject& obj) {
    obj["id"] = act.id;
    obj["type"] = (act.type == ACT_SERVO) ? "servo" : "solenoid";
    obj["bus_id"] = act.bus_id;
    obj["pca_address"] = act.pca_address;
    obj["pca_channel"] = act.pca_channel;
    obj["enabled"] = act.enabled;
    obj["latency_ms"] = act.latency_ms;

    if (act.type == ACT_SERVO) {
        const char* behaviors[] = {"frappe", "alterne", "gratter", "touche"};
        obj["behavior"] = behaviors[act.behavior % 4];
        obj["angle_initial"] = act.angle_initial;
        obj["amplitude"] = act.amplitude;
        obj["speed_ms"] = act.speed_ms;
        obj["angle_b"] = act.angle_b;
    } else {
        const char* behaviors[] = {"frappe", "hit_and_hold"};
        obj["behavior"] = behaviors[act.behavior % 2];
        obj["pulse_ms"] = act.pulse_ms;
        obj["pwm_initial"] = act.pwm_initial;
        obj["pwm_hold"] = act.pwm_hold;
        obj["ramp_ms"] = act.ramp_ms;
    }
}

void ConfigManager::deserializeActuator(ActuatorConfig& act, const JsonObject& obj) {
    act.id = obj["id"] | 0;
    act.bus_id = obj["bus_id"] | 0;
    act.pca_address = obj["pca_address"] | PCA_BASE_ADDRESS;
    act.pca_channel = obj["pca_channel"] | 0;
    act.enabled = obj["enabled"] | true;
    act.latency_ms = obj["latency_ms"] | 0;

    const char* type_str = obj["type"] | "servo";
    act.type = (strcmp(type_str, "solenoid") == 0) ? ACT_SOLENOID : ACT_SERVO;

    const char* behavior_str = obj["behavior"] | "frappe";

    if (act.type == ACT_SERVO) {
        if (strcmp(behavior_str, "alterne") == 0) act.behavior = SERVO_ALTERNE;
        else if (strcmp(behavior_str, "gratter") == 0) act.behavior = SERVO_GRATTER;
        else if (strcmp(behavior_str, "touche") == 0) act.behavior = SERVO_TOUCHE;
        else act.behavior = SERVO_FRAPPE;

        act.angle_initial = obj["angle_initial"] | 90;
        act.amplitude = obj["amplitude"] | 45;
        act.speed_ms = obj["speed_ms"] | 100;
        act.angle_b = obj["angle_b"] | 135;
    } else {
        if (strcmp(behavior_str, "hit_and_hold") == 0) act.behavior = SOL_HIT_AND_HOLD;
        else act.behavior = SOL_FRAPPE;

        act.pulse_ms = obj["pulse_ms"] | 20;
        act.pwm_initial = obj["pwm_initial"] | 4095;
        act.pwm_hold = obj["pwm_hold"] | 2048;
        act.ramp_ms = obj["ramp_ms"] | 50;
    }

    // Initialiser l'état runtime
    memset(&act.state, 0, sizeof(ActuatorState));
}

// ============================================================================
// Sérialisation / Désérialisation — Bus
// ============================================================================

void ConfigManager::serializeBus(const BusConfig& bus, JsonObject& obj) {
    obj["id"] = bus.id;
    obj["sda_pin"] = bus.sda_pin;
    obj["scl_pin"] = bus.scl_pin;
    obj["oe_pin"] = bus.oe_pin;
    obj["i2c_frequency"] = bus.i2c_frequency;
    obj["pwm_frequency"] = bus.pwm_frequency;
    obj["enabled"] = bus.enabled;
}

void ConfigManager::deserializeBus(BusConfig& bus, const JsonObject& obj) {
    bus.id = obj["id"] | 0;
    bus.sda_pin = obj["sda_pin"] | I2C0_SDA_PIN;
    bus.scl_pin = obj["scl_pin"] | I2C0_SCL_PIN;
    bus.oe_pin = obj["oe_pin"] | I2C0_OE_PIN;
    bus.i2c_frequency = obj["i2c_frequency"] | I2C0_FREQUENCY;
    bus.pwm_frequency = obj["pwm_frequency"] | PCA_SERVO_FREQ;
    bus.enabled = obj["enabled"] | true;
    bus.pca_count = 0;
}

// ============================================================================
// Sérialisation / Désérialisation — Instruments
// ============================================================================

void ConfigManager::serializeInstrument(const InstrumentConfig& inst, JsonObject& obj) {
    obj["name"] = inst.name;
    obj["midi_channel"] = inst.midi_channel;
    obj["bus_id"] = inst.bus_id;
    obj["default_latency_ms"] = inst.default_latency_ms;
    obj["auto_calibration"] = inst.auto_calibration;
    obj["enabled"] = inst.enabled;

    JsonArray actIds = obj["actuator_ids"].to<JsonArray>();
    for (uint8_t i = 0; i < inst.actuator_count; i++) {
        actIds.add(inst.actuator_ids[i]);
    }

    JsonArray noteIds = obj["midi_notes"].to<JsonArray>();
    for (uint8_t i = 0; i < inst.actuator_count; i++) {
        noteIds.add(inst.midi_notes[i]);
    }
}

void ConfigManager::deserializeInstrument(InstrumentConfig& inst, const JsonObject& obj) {
    strlcpy(inst.name, obj["name"] | "Instrument", sizeof(inst.name));
    inst.midi_channel = obj["midi_channel"] | 0;
    inst.bus_id = obj["bus_id"] | 0;
    inst.default_latency_ms = obj["default_latency_ms"] | 10;
    inst.auto_calibration = obj["auto_calibration"] | false;
    inst.enabled = obj["enabled"] | true;

    inst.actuator_count = 0;
    JsonArray actIds = obj["actuator_ids"].as<JsonArray>();
    for (JsonVariant v : actIds) {
        if (inst.actuator_count < PCA_CHANNELS) {
            inst.actuator_ids[inst.actuator_count] = v.as<uint8_t>();
            inst.actuator_count++;
        }
    }

    // Charger les notes MIDI mappées
    memset(inst.midi_notes, MIDI_NOTE_UNMAPPED, sizeof(inst.midi_notes));
    JsonArray noteIds = obj["midi_notes"].as<JsonArray>();
    uint8_t noteIdx = 0;
    for (JsonVariant v : noteIds) {
        if (noteIdx < PCA_CHANNELS) {
            inst.midi_notes[noteIdx] = v.as<uint8_t>();
            noteIdx++;
        }
    }
}

// ============================================================================
// Sérialisation / Désérialisation — WiFi
// ============================================================================

void ConfigManager::serializeWiFi(const WiFiConfig& wifi, JsonObject& obj) {
    obj["ssid"] = wifi.ssid;
    obj["password"] = wifi.password;
    obj["hostname"] = wifi.hostname;
    obj["enabled"] = wifi.enabled;
    obj["ap_fallback"] = wifi.ap_fallback;
}

void ConfigManager::deserializeWiFi(WiFiConfig& wifi, const JsonObject& obj) {
    strlcpy(wifi.ssid, obj["ssid"] | "MidiBoop", sizeof(wifi.ssid));
    strlcpy(wifi.password, obj["password"] | "midiboop", sizeof(wifi.password));
    strlcpy(wifi.hostname, obj["hostname"] | WIFI_DEFAULT_HOSTNAME, sizeof(wifi.hostname));
    wifi.enabled = obj["enabled"] | false;
    wifi.ap_fallback = obj["ap_fallback"] | WIFI_AP_FALLBACK;
}

// ============================================================================
// Sérialisation / Désérialisation — MIDI Input
// ============================================================================

void ConfigManager::serializeMidiInput(const MidiInputConfig& midi, JsonObject& obj) {
    obj["serial_enabled"] = midi.serial_enabled;
    obj["udp_enabled"] = midi.udp_enabled;
    obj["rtp_enabled"] = midi.rtp_enabled;
    obj["udp_port"] = midi.udp_port;
    obj["rtp_port"] = midi.rtp_port;
    obj["jitter_buffer_ms"] = midi.jitter_buffer_ms;
    obj["serial_rx_pin"] = midi.serial_rx_pin;
}

void ConfigManager::deserializeMidiInput(MidiInputConfig& midi, const JsonObject& obj) {
    midi.serial_enabled = obj["serial_enabled"] | true;
    midi.udp_enabled = obj["udp_enabled"] | true;
    midi.rtp_enabled = obj["rtp_enabled"] | true;
    midi.udp_port = obj["udp_port"] | MIDI_UDP_PORT;
    midi.rtp_port = obj["rtp_port"] | MIDI_RTP_PORT;
    midi.jitter_buffer_ms = obj["jitter_buffer_ms"] | MIDI_JITTER_BUFFER_MS;
    midi.serial_rx_pin = obj["serial_rx_pin"] | MIDI_SERIAL_RX_PIN;
}

// ============================================================================
// Sérialisation / Désérialisation — Routage MIDI
// ============================================================================

void ConfigManager::serializeRouting(const MidiRoutingConfig& routing, JsonObject& obj) {
    obj["instrument_index"] = routing.instrument_index;

    // Note mappings
    JsonArray noteArray = obj["note_map"].to<JsonArray>();
    for (uint8_t i = 0; i < routing.note_map_count; i++) {
        JsonObject nm = noteArray.add<JsonObject>();
        nm["midi_note"] = routing.note_map[i].midi_note;
        nm["actuator_id"] = routing.note_map[i].actuator_id;
        nm["behavior_override"] = routing.note_map[i].behavior_override;
        nm["enabled"] = routing.note_map[i].enabled;
    }

    // CC mappings
    JsonArray ccArray = obj["cc_map"].to<JsonArray>();
    for (uint8_t i = 0; i < routing.cc_map_count; i++) {
        JsonObject cm = ccArray.add<JsonObject>();
        cm["cc_number"] = routing.cc_map[i].cc_number;
        cm["actuator_id"] = routing.cc_map[i].actuator_id;
        cm["target"] = (uint8_t)routing.cc_map[i].target;
        cm["range_min"] = routing.cc_map[i].range_min;
        cm["range_max"] = routing.cc_map[i].range_max;
        cm["enabled"] = routing.cc_map[i].enabled;
    }

    // Velocity curve
    JsonArray velArray = obj["velocity_curve"].to<JsonArray>();
    for (uint8_t i = 0; i < routing.velocity_curve_count; i++) {
        JsonObject vp = velArray.add<JsonObject>();
        vp["input"] = routing.velocity_curve[i].input;
        vp["output"] = routing.velocity_curve[i].output;
    }
}

void ConfigManager::deserializeRouting(MidiRoutingConfig& routing, const JsonObject& obj) {
    routing.instrument_index = obj["instrument_index"] | 0;

    // Note mappings
    routing.note_map_count = 0;
    JsonArray noteArray = obj["note_map"].as<JsonArray>();
    for (JsonObject nm : noteArray) {
        if (routing.note_map_count >= MAX_NOTE_MAPPINGS) break;
        NoteMapping& m = routing.note_map[routing.note_map_count];
        m.midi_note = nm["midi_note"] | 0;
        m.actuator_id = nm["actuator_id"] | 0;
        m.behavior_override = nm["behavior_override"] | 0xFF;
        m.enabled = nm["enabled"] | true;
        routing.note_map_count++;
    }

    // CC mappings
    routing.cc_map_count = 0;
    JsonArray ccArray = obj["cc_map"].as<JsonArray>();
    for (JsonObject cm : ccArray) {
        if (routing.cc_map_count >= MAX_CC_MAPPINGS) break;
        CCMapping& c = routing.cc_map[routing.cc_map_count];
        c.cc_number = cm["cc_number"] | 0;
        c.actuator_id = cm["actuator_id"] | 0;
        c.target = (CCTarget)(cm["target"] | 0);
        c.range_min = cm["range_min"] | 0;
        c.range_max = cm["range_max"] | 4095;
        c.enabled = cm["enabled"] | true;
        routing.cc_map_count++;
    }

    // Velocity curve
    routing.velocity_curve_count = 0;
    JsonArray velArray = obj["velocity_curve"].as<JsonArray>();
    for (JsonObject vp : velArray) {
        if (routing.velocity_curve_count >= VELOCITY_CURVE_POINTS) break;
        VelocityCurvePoint& p = routing.velocity_curve[routing.velocity_curve_count];
        p.input = vp["input"] | 0;
        p.output = vp["output"] | 0;
        routing.velocity_curve_count++;
    }
}
