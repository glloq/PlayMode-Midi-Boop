#include "config_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================================
// PlayMode — Config Manager (implementation)
// ============================================================================

// AUDIT FIX (point B): mutex that serialises structural edits of the actuator
// array (add/remove/update) against the real-time Scheduler, which
// dereferences pointers into that array from Core 1 while driving the PCA9685.
// Defined here, referenced by the Scheduler via an extern declaration.
SemaphoreHandle_t g_actuator_mutex = nullptr;

// AUDIT FIX (P0.5): RAII guard implementation.
ActuatorLockGuard::ActuatorLockGuard(ConfigManager& cfg, uint32_t timeout_ms)
    : _cfg(cfg), _locked(cfg.lockActuators(timeout_ms)) {}

ActuatorLockGuard::~ActuatorLockGuard() {
    if (_locked) _cfg.unlockActuators();
}

ConfigManager::ConfigManager()
    : _actuator_count(0),
      _instrument_count(0),
      _routing_count(0),
      _version(CONFIG_VERSION) {
    memset(&_wifi_config, 0, sizeof(_wifi_config));
    memset(&_midi_input_config, 0, sizeof(_midi_input_config));
    memset(&_power_budget, 0, sizeof(_power_budget));
    memset(&_safety_limits, 0, sizeof(_safety_limits));
    memset(_actuators, 0, sizeof(_actuators));
    memset(_instruments, 0, sizeof(_instruments));
    memset(_routing_configs, 0, sizeof(_routing_configs));
}

bool ConfigManager::begin() {
    // AUDIT FIX (point B): create the actuator mutex before the Scheduler task
    // is started (Scheduler::begin() runs later in setup()), so structural
    // edits and real-time processing are serialised from the first tick.
    if (g_actuator_mutex == nullptr) {
        g_actuator_mutex = xSemaphoreCreateMutex();
    }

    // AUDIT FIX: never eagerly format on a mount failure — that silently wipes
    // every saved instrument, mapping and calibration result. A mount failure
    // is often transient, so retry a few times first; only format as a last
    // resort (genuinely blank/corrupt flash on a new board).
    bool mounted = false;
    for (uint8_t attempt = 0; attempt < 3 && !mounted; attempt++) {
        mounted = LittleFS.begin(false);
        if (!mounted) {
            Serial.printf("[CONFIG] LittleFS mount attempt %d failed\n", attempt + 1);
            delay(100);
        }
    }
    if (!mounted) {
        Serial.println("[CONFIG] LittleFS unmountable after retries — one-time format");
        if (!LittleFS.begin(true)) {
            Serial.println("[CONFIG] LittleFS mount error after format attempt");
            return false;
        }
        Serial.println("[CONFIG] LittleFS formatted (was uninitialised/corrupt)");
    } else {
        Serial.println("[CONFIG] LittleFS mounted");
    }

    // Load config; on failure fall back to the atomic-save backup before
    // resorting to defaults.
    if (configExists()) {
        if (!load()) {
            Serial.println("[CONFIG] Primary config load failed — trying backup");
            // Remove the corrupt primary so the backup can be renamed into place.
            LittleFS.remove(CONFIG_FILE_PATH);
            if (LittleFS.exists(CONFIG_BAK_PATH) &&
                LittleFS.rename(CONFIG_BAK_PATH, CONFIG_FILE_PATH) && load()) {
                Serial.println("[CONFIG] Recovered configuration from backup");
            } else {
                Serial.println("[CONFIG] Load error, using defaults");
                loadDefaults();
            }
        }
    } else if (LittleFS.exists(CONFIG_BAK_PATH) &&
               LittleFS.rename(CONFIG_BAK_PATH, CONFIG_FILE_PATH) && load()) {
        Serial.println("[CONFIG] Primary missing — recovered from backup");
    } else {
        Serial.println("[CONFIG] No config found, loading defaults");
        loadDefaults();
        save();
    }

    return true;
}

bool ConfigManager::load() {
    File file = LittleFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        Serial.println("[CONFIG] Unable to open config file");
        return false;
    }

    // Allocate JSON document (appropriate size)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[CONFIG] JSON error: %s\n", error.c_str());
        return false;
    }

    // AUDIT FIX: start from a fully-populated default set, THEN overlay the JSON
    // fields that are present. This guarantees every field (both buses, WiFi,
    // MIDI) has a sane per-item default even if the file is partial.
    loadDefaults();

    // Version (capture what was on disk so we can migrate below).
    uint8_t loaded_version = doc["version"] | 0;
    _version = CONFIG_VERSION;

    // WiFi
    if (!doc["wifi"].isNull()) {
        JsonObject wifiObj = doc["wifi"].as<JsonObject>();
        deserializeWiFi(_wifi_config, wifiObj);
    }

    // MIDI Input
    if (!doc["midi_input"].isNull()) {
        JsonObject midiObj = doc["midi_input"].as<JsonObject>();
        deserializeMidiInput(_midi_input_config, midiObj);
    }

    // Power budget + Safety limits (persisted)
    if (!doc["power"].isNull()) {
        JsonObject p = doc["power"].as<JsonObject>();
        deserializePower(_power_budget, p);
    }
    if (!doc["safety"].isNull()) {
        JsonObject s = doc["safety"].as<JsonObject>();
        deserializeSafety(_safety_limits, s);
    }

    // Bus
    JsonArray busArray = doc["buses"].as<JsonArray>();
    uint8_t busIdx = 0;
    for (JsonObject busObj : busArray) {
        if (busIdx >= 2) break;
        deserializeBus(_buses[busIdx], busObj);
        busIdx++;
    }

    // Actuators
    // AUDIT FIX (P0.2): validate on load — skip actuators with an out-of-range
    // ID, a duplicate ID, or a duplicate bus/address/channel target so a
    // corrupt file can never poison the runtime tables.
    _actuator_count = 0;
    JsonArray actArray = doc["actuators"].as<JsonArray>();
    for (JsonObject actObj : actArray) {
        if (_actuator_count >= MAX_ACTUATORS) break;
        ActuatorConfig cand = {};
        deserializeActuator(cand, actObj);
        if (cand.id >= MAX_ACTUATORS) {
            Serial.printf("[CONFIG] Skipping actuator with invalid id %d\n", cand.id);
            continue;
        }
        bool dup = false;
        for (uint8_t j = 0; j < _actuator_count; j++) {
            if (_actuators[j].id == cand.id ||
                (_actuators[j].bus_id == cand.bus_id &&
                 _actuators[j].pca_address == cand.pca_address &&
                 _actuators[j].pca_channel == cand.pca_channel)) {
                dup = true;
                break;
            }
        }
        if (dup) {
            Serial.printf("[CONFIG] Skipping duplicate actuator id %d\n", cand.id);
            continue;
        }
        _actuators[_actuator_count] = cand;
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

    // MIDI Routing
    _routing_count = 0;
    if (!doc["routing"].isNull()) {
        JsonArray routeArray = doc["routing"].as<JsonArray>();
        for (JsonObject routeObj : routeArray) {
            if (_routing_count >= MAX_INSTRUMENTS) break;
            deserializeRouting(_routing_configs[_routing_count], routeObj);
            _routing_count++;
        }
    }

    // AUDIT FIX (P1.1): migrate the MIDI channel representation UNAMBIGUOUSLY.
    // A config written by this firmware carries channel_encoding="internal" and
    // is never migrated. A legacy file without that marker is inspected:
    //   - any 0xFF present  -> already internal (Omni sentinel) -> keep;
    //   - any value  > 15   -> old UI convention (had a 16)     -> migrate;
    //   - only values 0..15 -> AMBIGUOUS: keep as-is (assume internal) and warn
    //     rather than risk corrupting an already-internal config.
    const char* enc = doc["channel_encoding"] | "";
    bool already_internal = (strcmp(enc, "internal") == 0);
    if (!already_internal && loaded_version < CONFIG_VERSION_CHANNELS_INTERNAL) {
        bool has_sentinel = false, has_over_15 = false;
        for (uint8_t i = 0; i < _instrument_count; i++) {
            uint8_t ch = _instruments[i].midi_channel;
            if (ch == MIDI_CHANNEL_OMNI_INTERNAL) has_sentinel = true;
            else if (ch > 15) has_over_15 = true;
        }
        if (has_over_15 && !has_sentinel) {
            Serial.println("[CONFIG] Migrating legacy MIDI channels (UI 0/1-16 -> internal)");
            for (uint8_t i = 0; i < _instrument_count; i++) {
                uint8_t old_ch = _instruments[i].midi_channel;
                _instruments[i].midi_channel =
                    (old_ch == 0) ? MIDI_CHANNEL_OMNI_INTERNAL
                  : (old_ch <= 16) ? (uint8_t)(old_ch - 1)
                  : MIDI_CHANNEL_OMNI_INTERNAL;
            }
        } else if (!has_sentinel) {
            Serial.println("[CONFIG] WARNING: ambiguous legacy channel encoding "
                           "(0..15 only) — assuming already internal, not migrating");
        }
        save();  // re-write with the channel_encoding marker (one-shot)
    }

    Serial.printf("[CONFIG] Loaded: %d actuators, %d instruments, %d routings\n",
                  _actuator_count, _instrument_count, _routing_count);
    return true;
}

void ConfigManager::populateDoc(JsonDocument& doc) {
    doc["version"] = _version;
    // AUDIT FIX: explicit, unambiguous marker so a reload never double-migrates
    // the channel representation regardless of the numeric version.
    doc["channel_encoding"] = "internal";

    JsonObject wifiObj = doc["wifi"].to<JsonObject>();
    serializeWiFi(_wifi_config, wifiObj);

    JsonObject midiObj = doc["midi_input"].to<JsonObject>();
    serializeMidiInput(_midi_input_config, midiObj);

    JsonObject powerObj = doc["power"].to<JsonObject>();
    serializePower(_power_budget, powerObj);
    JsonObject safetyObj = doc["safety"].to<JsonObject>();
    serializeSafety(_safety_limits, safetyObj);

    JsonArray busArray = doc["buses"].to<JsonArray>();
    for (uint8_t i = 0; i < 2; i++) {
        JsonObject busObj = busArray.add<JsonObject>();
        serializeBus(_buses[i], busObj);
    }

    JsonArray actArray = doc["actuators"].to<JsonArray>();
    for (uint8_t i = 0; i < _actuator_count; i++) {
        JsonObject actObj = actArray.add<JsonObject>();
        serializeActuator(_actuators[i], actObj);
    }

    JsonArray instArray = doc["instruments"].to<JsonArray>();
    for (uint8_t i = 0; i < _instrument_count; i++) {
        JsonObject instObj = instArray.add<JsonObject>();
        serializeInstrument(_instruments[i], instObj);
    }

    JsonArray routeArray = doc["routing"].to<JsonArray>();
    for (uint8_t i = 0; i < _routing_count; i++) {
        JsonObject routeObj = routeArray.add<JsonObject>();
        serializeRouting(_routing_configs[i], routeObj);
    }
}

bool ConfigManager::exportJson(String& out) {
    JsonDocument doc;
    populateDoc(doc);
    return serializeJsonPretty(doc, out) > 0;
}

bool ConfigManager::save() {
    JsonDocument doc;
    populateDoc(doc);

    // AUDIT FIX: atomic write. Serialise to a temp file first; only if that
    // fully succeeds do we rotate it over the live file (keeping a .bak). A
    // power loss mid-write can now only corrupt the temp file, never the live
    // config or its backup.
    File file = LittleFS.open(CONFIG_TMP_PATH, "w");
    if (!file) {
        Serial.println("[CONFIG] Unable to open temp config file");
        return false;
    }

    // AUDIT FIX (P1-1): compute the exact expected length so we can detect a
    // TRUNCATED write (e.g. LittleFS full → file.write() returns short and
    // ArduinoJson stops early with written > 0). Previously only written==0 and
    // on_disk==0 were caught, so a truncated-but-nonzero temp file passed both
    // guards and was rotated over the live config; a second failed save then
    // overwrote the .bak too — losing BOTH copies permanently. Requiring an
    // exact byte-complete match before committing prevents that.
    size_t expected = measureJsonPretty(doc);
    size_t written = serializeJsonPretty(doc, file);
    file.close();

    if (written == 0 || written != expected) {
        Serial.printf("[CONFIG] Short/failed serialize (%u/%u bytes) — aborting save\n",
                      (unsigned)written, (unsigned)expected);
        LittleFS.remove(CONFIG_TMP_PATH);
        return false;
    }

    // Verify the temp file is byte-complete on disk before committing.
    File verify = LittleFS.open(CONFIG_TMP_PATH, "r");
    size_t on_disk = verify ? verify.size() : 0;
    if (verify) verify.close();
    if (on_disk != expected) {
        Serial.printf("[CONFIG] Temp file incomplete on disk (%u/%u bytes) — aborting save\n",
                      (unsigned)on_disk, (unsigned)expected);
        LittleFS.remove(CONFIG_TMP_PATH);
        return false;
    }

    // Rotate: current -> backup, temp -> current.
    if (LittleFS.exists(CONFIG_FILE_PATH)) {
        LittleFS.remove(CONFIG_BAK_PATH);
        LittleFS.rename(CONFIG_FILE_PATH, CONFIG_BAK_PATH);
    }
    if (!LittleFS.rename(CONFIG_TMP_PATH, CONFIG_FILE_PATH)) {
        Serial.println("[CONFIG] Rename temp->config failed — restoring backup");
        LittleFS.rename(CONFIG_BAK_PATH, CONFIG_FILE_PATH);
        return false;
    }

    Serial.println("[CONFIG] Configuration saved (atomic)");
    return true;
}

bool ConfigManager::validateConfigDoc(JsonDocument& doc, String& errors) {
    // AUDIT FIX (P1.5): STRICT validation used only for user-initiated import.
    // Every problem is reported; the caller rejects the whole file if any exist.
    // This is deliberately distinct from load(), whose tolerant skipping exists
    // to recover a bootable state from a partially-corrupt on-disk file.
    bool ok = true;

    if (doc["version"].isNull() && doc["actuators"].isNull() && doc["buses"].isNull()) {
        errors += "Fichier non reconnu comme configuration PlayMode.\n";
        return false;  // nothing else is meaningful
    }

    JsonArray actArray = doc["actuators"].as<JsonArray>();
    if (actArray.isNull()) {
        // No actuators is a valid (empty) config; nothing to validate here.
        return true;
    }

    uint16_t index = 0;
    uint16_t kept = 0;
    // Track seen ids and bus/address/channel targets to detect duplicates.
    // Compact 4-byte record (NOT full ActuatorConfig) to keep this off-stack
    // footprint small on the web/async task.
    struct SeenActuator { uint8_t id; uint8_t bus_id; uint8_t pca_address; uint8_t pca_channel; };
    static SeenActuator seen[MAX_ACTUATORS];
    for (JsonObject actObj : actArray) {
        index++;
        if (kept >= MAX_ACTUATORS) {
            errors += "Trop d'actionneurs (maximum " + String(MAX_ACTUATORS) + ").\n";
            ok = false;
            break;
        }

        // id (raw, unclamped)
        long id = actObj["id"] | -1;
        if (id < 0 || id >= MAX_ACTUATORS) {
            errors += "Actionneur #" + String(index) + " : id " + String(id) +
                      " hors plage [0," + String(MAX_ACTUATORS - 1) + "].\n";
            ok = false;
        }

        // bus_id
        long bus = actObj["bus_id"] | 0;
        if (bus < 0 || bus > 1) {
            errors += "Actionneur #" + String(index) + " (id " + String(id) +
                      ") : bus_id " + String(bus) + " invalide (0 ou 1).\n";
            ok = false;
        }

        // pca_address
        long addr = actObj["pca_address"] | (long)PCA_BASE_ADDRESS;
        if (addr < PCA_BASE_ADDRESS || addr >= PCA_BASE_ADDRESS + PCA_MAX_PER_BUS) {
            errors += "Actionneur #" + String(index) + " (id " + String(id) +
                      ") : pca_address 0x" + String((uint32_t)addr, HEX) +
                      " hors plage.\n";
            ok = false;
        }

        // pca_channel
        long ch = actObj["pca_channel"] | 0;
        if (ch < 0 || ch >= PCA_CHANNELS) {
            errors += "Actionneur #" + String(index) + " (id " + String(id) +
                      ") : pca_channel " + String(ch) + " hors plage [0," +
                      String(PCA_CHANNELS - 1) + "].\n";
            ok = false;
        }

        // type
        const char* type_str = actObj["type"] | "servo";
        if (strcmp(type_str, "servo") != 0 && strcmp(type_str, "solenoid") != 0) {
            errors += "Actionneur #" + String(index) + " (id " + String(id) +
                      ") : type '" + String(type_str) + "' inconnu (servo/solenoid).\n";
            ok = false;
        }

        // Duplicate detection against previously accepted-shape entries.
        bool dup_id = false, dup_target = false;
        for (uint16_t j = 0; j < kept; j++) {
            if ((long)seen[j].id == id) dup_id = true;
            if ((long)seen[j].bus_id == bus &&
                (long)seen[j].pca_address == addr &&
                (long)seen[j].pca_channel == ch) dup_target = true;
        }
        if (dup_id) {
            errors += "Actionneur #" + String(index) + " : id " + String(id) +
                      " en double.\n";
            ok = false;
        }
        if (dup_target) {
            errors += "Actionneur #" + String(index) + " (id " + String(id) +
                      ") : sortie bus " + String(bus) + "/0x" +
                      String((uint32_t)addr, HEX) + "/canal " + String(ch) +
                      " déjà utilisée.\n";
            ok = false;
        }

        // Record its target for subsequent duplicate checks (even if invalid,
        // so we still surface secondary duplicates against it).
        if (kept < MAX_ACTUATORS) {
            seen[kept].id = (uint8_t)(id < 0 ? 0xFF : id);
            seen[kept].bus_id = (uint8_t)(bus & 0xFF);
            seen[kept].pca_address = (uint8_t)(addr & 0xFF);
            seen[kept].pca_channel = (uint8_t)(ch & 0xFF);
            kept++;
        }
    }

    return ok;
}

bool ConfigManager::importJson(const uint8_t* data, size_t len, String* errors) {
    // Validate that it parses and looks like a PlayMode config before touching
    // the live file.
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        Serial.println("[CONFIG] Import rejected — invalid JSON");
        if (errors) *errors += "JSON invalide (échec du parsing).\n";
        return false;
    }

    // AUDIT FIX (P1.5): reject the WHOLE file if any actuator is invalid or a
    // duplicate — never silently drop entries and report success.
    String problems;
    if (!validateConfigDoc(doc, problems)) {
        Serial.println("[CONFIG] Import rejected — validation errors:");
        Serial.print(problems);
        if (errors) *errors += problems;
        return false;
    }

    // Stage the uploaded bytes, then rotate into place (keeping a backup).
    File f = LittleFS.open(CONFIG_TMP_PATH, "w");
    if (!f) { if (errors) *errors += "Écriture temporaire impossible.\n"; return false; }
    size_t w = f.write(data, len);
    f.close();
    if (w != len) {
        LittleFS.remove(CONFIG_TMP_PATH);
        if (errors) *errors += "Écriture temporaire incomplète.\n";
        return false;
    }

    if (LittleFS.exists(CONFIG_FILE_PATH)) {
        LittleFS.remove(CONFIG_BAK_PATH);
        LittleFS.rename(CONFIG_FILE_PATH, CONFIG_BAK_PATH);
    }
    if (!LittleFS.rename(CONFIG_TMP_PATH, CONFIG_FILE_PATH)) {
        LittleFS.rename(CONFIG_BAK_PATH, CONFIG_FILE_PATH);
        if (errors) *errors += "Rotation du fichier de configuration impossible.\n";
        return false;
    }

    // Re-load through the normal path (defaults + overlay + validation +
    // migration). On failure, restore the backup. Because we validated strictly
    // above, load() here should keep every actuator; a reload failure now means
    // a filesystem-level problem, not silent dropping.
    if (!load()) {
        Serial.println("[CONFIG] Import failed on reload — restoring previous config");
        if (errors) *errors += "Rechargement échoué — configuration précédente restaurée.\n";
        LittleFS.remove(CONFIG_FILE_PATH);
        if (LittleFS.exists(CONFIG_BAK_PATH)) {
            LittleFS.rename(CONFIG_BAK_PATH, CONFIG_FILE_PATH);
            load();
        }
        return false;
    }
    Serial.println("[CONFIG] Configuration imported");
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

    // Bus 1 — Solenoids
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

    // WiFi defaults — AP enabled by default for first access without configuration
    strlcpy(_wifi_config.ssid, "", sizeof(_wifi_config.ssid));
    strlcpy(_wifi_config.password, "", sizeof(_wifi_config.password));
    // Empty AP password → WiFiManager derives a unique one from the chip MAC.
    strlcpy(_wifi_config.ap_password, "", sizeof(_wifi_config.ap_password));
    strlcpy(_wifi_config.hostname, WIFI_DEFAULT_HOSTNAME, sizeof(_wifi_config.hostname));
    _wifi_config.enabled = true;
    _wifi_config.ap_fallback = true;

    // Power budget defaults
    memset(&_power_budget, 0, sizeof(_power_budget));
    _power_budget.global_max_ma        = POWER_GLOBAL_MAX_MA;
    _power_budget.servo_bus_max_ma     = POWER_SERVO_BUS_MAX_MA;
    _power_budget.solenoid_bus_max_ma  = POWER_SOLENOID_BUS_MAX_MA;
    _power_budget.global_max_polyphony = POWER_MAX_POLYPHONY;
    _power_budget.smart_rejection      = true;
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        _power_budget.instrument_max_polyphony[i] = 4;
    }

    // Safety limit defaults
    _safety_limits.max_duty_pct   = SAFETY_MAX_DUTY_CYCLE;
    _safety_limits.max_freq_hz    = SAFETY_MAX_FREQ_HZ;
    _safety_limits.watchdog_ms    = SAFETY_WATCHDOG_MS;
    _safety_limits.max_polyphony  = SAFETY_MAX_POLYPHONY;
    _safety_limits.max_current_ma = SAFETY_MAX_TOTAL_CURRENT_MA;

    // MIDI Input defaults
    _midi_input_config.serial_enabled = true;
    _midi_input_config.udp_enabled = true;
    _midi_input_config.rtp_enabled = true;
    _midi_input_config.udp_port = MIDI_UDP_PORT;
    _midi_input_config.rtp_port = MIDI_RTP_PORT;
    _midi_input_config.jitter_buffer_ms = MIDI_JITTER_BUFFER_MS;
    _midi_input_config.serial_rx_pin = MIDI_SERIAL_RX_PIN;

    Serial.println("[CONFIG] Defaults loaded");
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

// AUDIT FIX (point B): guard structural actuator-array edits against the
// real-time Scheduler. Returns true if the caller now holds the lock (or the
// mutex is unavailable, in which case behaviour degrades to the previous
// unguarded path — never a hard failure).
bool ConfigManager::lockActuators(uint32_t timeout_ms) {
    if (g_actuator_mutex == nullptr) return true;
    return xSemaphoreTake(g_actuator_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void ConfigManager::unlockActuators() {
    if (g_actuator_mutex != nullptr) xSemaphoreGive(g_actuator_mutex);
}

bool ConfigManager::addActuator(const ActuatorConfig& actuator) {
    // If an actuator with the same ID exists, update in place
    for (uint8_t i = 0; i < _actuator_count; i++) {
        if (_actuators[i].id == actuator.id) {
            _actuators[i] = actuator;
            return true;
        }
    }
    // New slot
    if (_actuator_count >= MAX_ACTUATORS) return false;
    _actuators[_actuator_count] = actuator;
    _actuator_count++;
    return true;
}

bool ConfigManager::removeActuator(uint8_t id) {
    bool found = false;
    for (uint8_t i = 0; i < _actuator_count; i++) {
        if (_actuators[i].id == id) {
            // Shift subsequent actuators
            for (uint8_t j = i; j < _actuator_count - 1; j++) {
                _actuators[j] = _actuators[j + 1];
            }
            _actuators[_actuator_count - 1] = {};
            _actuator_count--;
            found = true;
            break;
        }
    }
    if (!found) return false;

    // AUDIT FIX: purge every reference to the removed actuator so no orphan
    // mapping survives (a note that still "routes" to a missing actuator).
    for (uint8_t r = 0; r < _routing_count; r++) {
        MidiRoutingConfig& routing = _routing_configs[r];
        // Compact note_map, dropping entries that referenced this actuator.
        uint8_t w = 0;
        for (uint8_t k = 0; k < routing.note_map_count; k++) {
            if (routing.note_map[k].actuator_id != id) {
                routing.note_map[w++] = routing.note_map[k];
            }
        }
        routing.note_map_count = w;
        // Compact cc_map likewise.
        w = 0;
        for (uint8_t k = 0; k < routing.cc_map_count; k++) {
            if (routing.cc_map[k].actuator_id != id) {
                routing.cc_map[w++] = routing.cc_map[k];
            }
        }
        routing.cc_map_count = w;
        // Keep the legacy instrument arrays consistent.
        if (routing.instrument_index < _instrument_count) {
            rebuildInstrumentFromRouting(routing.instrument_index);
        }
    }
    return true;
}

void ConfigManager::rebuildInstrumentFromRouting(uint8_t instrument_index) {
    if (instrument_index >= _instrument_count) return;
    InstrumentConfig& inst = _instruments[instrument_index];
    MidiRoutingConfig* routing = getRoutingForInstrument(instrument_index);

    memset(inst.midi_notes, MIDI_NOTE_UNMAPPED, sizeof(inst.midi_notes));
    inst.actuator_count = 0;
    if (routing == nullptr) return;

    for (uint8_t i = 0; i < routing->note_map_count &&
                        inst.actuator_count < MAX_ACTUATORS_PER_INSTRUMENT; i++) {
        if (!routing->note_map[i].enabled) continue;
        inst.actuator_ids[inst.actuator_count] = routing->note_map[i].actuator_id;
        inst.midi_notes[inst.actuator_count]   = routing->note_map[i].midi_note;
        inst.actuator_count++;
    }
}

bool ConfigManager::addInstrument(const InstrumentConfig& instrument) {
    if (_instrument_count >= MAX_INSTRUMENTS) return false;
    _instruments[_instrument_count] = instrument;
    _instrument_count++;
    return true;
}

bool ConfigManager::removeInstrument(uint8_t index) {
    if (index >= _instrument_count) return false;
    // Shift subsequent instruments
    for (uint8_t i = index; i < _instrument_count - 1; i++) {
        _instruments[i] = _instruments[i + 1];
    }
    _instruments[_instrument_count - 1] = {};
    _instrument_count--;

    // AUDIT FIX (P1-3): routings are matched to instruments by their
    // `instrument_index` FIELD (getRoutingForInstrument searches by field), not
    // by array position — the two arrays are not guaranteed parallel. The old
    // code renumbered positionally, which deleted/mislabelled the wrong
    // routing whenever they had diverged. Instead: drop the routing(s) whose
    // instrument_index == the removed index, compact the array, and decrement
    // instrument_index on every surviving routing that referenced an instrument
    // AFTER the removed one (those instruments just shifted down by one).
    uint8_t w = 0;
    for (uint8_t r = 0; r < _routing_count; r++) {
        if (_routing_configs[r].instrument_index == index) continue;  // drop
        MidiRoutingConfig kept = _routing_configs[r];
        if (kept.instrument_index > index) kept.instrument_index--;
        _routing_configs[w++] = kept;
    }
    for (uint8_t r = w; r < _routing_count; r++) _routing_configs[r] = {};
    _routing_count = w;

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

void ConfigManager::setMidiInputConfig(const MidiInputConfig& config) {
    _midi_input_config = config;
}

PowerBudget* ConfigManager::getPowerBudget() {
    return &_power_budget;
}

void ConfigManager::setPowerBudget(const PowerBudget& budget) {
    _power_budget = budget;
}

SafetyLimits* ConfigManager::getSafetyLimits() {
    return &_safety_limits;
}

void ConfigManager::setSafetyLimits(const SafetyLimits& limits) {
    _safety_limits = limits;
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
// Serialization / Deserialization — Actuators
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
        obj["hit_reverse"] = act.hit_reverse;
    } else {
        const char* behaviors[] = {"frappe", "hit_and_hold"};
        obj["behavior"] = behaviors[act.behavior % 2];
        obj["pulse_min_ms"] = act.pulse_min_ms;
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

    // AUDIT FIX: validate bus_id, pca_address and pca_channel
    if (act.bus_id > 1) act.bus_id = 0;
    if (act.pca_address < PCA_BASE_ADDRESS ||
        act.pca_address >= PCA_BASE_ADDRESS + PCA_MAX_PER_BUS) {
        act.pca_address = PCA_BASE_ADDRESS;
    }
    if (act.pca_channel >= PCA_CHANNELS) act.pca_channel = 0;

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
        act.hit_reverse = obj["hit_reverse"] | false;

        // AUDIT FIX: clamp servo angles to [0, SERVO_MAX_ANGLE]
        if (act.angle_initial > SERVO_MAX_ANGLE) act.angle_initial = SERVO_MAX_ANGLE;
        if (act.angle_b > SERVO_MAX_ANGLE) act.angle_b = SERVO_MAX_ANGLE;
        if (act.amplitude > SERVO_MAX_ANGLE) act.amplitude = SERVO_MAX_ANGLE;
    } else {
        if (strcmp(behavior_str, "hit_and_hold") == 0) act.behavior = SOL_HIT_AND_HOLD;
        else act.behavior = SOL_FRAPPE;

        act.pulse_min_ms = obj["pulse_min_ms"] | SOLENOID_MIN_PULSE_MS;
        act.pulse_ms = obj["pulse_ms"] | 20;
        act.pwm_initial = obj["pwm_initial"] | 4095;
        act.pwm_hold = obj["pwm_hold"] | 2048;
        act.ramp_ms = obj["ramp_ms"] | 50;

        // AUDIT FIX: clamp solenoid PWM values to [0, SOLENOID_PWM_MAX]
        if (act.pwm_initial > SOLENOID_PWM_MAX) act.pwm_initial = SOLENOID_PWM_MAX;
        if (act.pwm_hold > SOLENOID_PWM_MAX) act.pwm_hold = SOLENOID_PWM_MAX;
    }

    // Initialize runtime state
    memset(&act.state, 0, sizeof(ActuatorState));
}

// ============================================================================
// Serialization / Deserialization — Bus
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
    // AUDIT FIX: fall back to the bus's CURRENT (already-defaulted) values, not
    // the bus-0 constants — otherwise a bus-1 entry missing a field would be
    // silently rewritten with bus-0 pins/frequency.
    bus.id = obj["id"] | bus.id;
    bus.sda_pin = obj["sda_pin"] | bus.sda_pin;
    bus.scl_pin = obj["scl_pin"] | bus.scl_pin;
    bus.oe_pin = obj["oe_pin"] | bus.oe_pin;
    bus.i2c_frequency = obj["i2c_frequency"] | bus.i2c_frequency;
    bus.pwm_frequency = obj["pwm_frequency"] | bus.pwm_frequency;
    bus.enabled = obj["enabled"] | bus.enabled;
    bus.pca_count = 0;
}

// ============================================================================
// Serialization / Deserialization — Instruments
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
        if (inst.actuator_count < MAX_ACTUATORS_PER_INSTRUMENT) {
            inst.actuator_ids[inst.actuator_count] = v.as<uint8_t>();
            inst.actuator_count++;
        }
    }

    // Load mapped MIDI notes
    memset(inst.midi_notes, MIDI_NOTE_UNMAPPED, sizeof(inst.midi_notes));
    JsonArray noteIds = obj["midi_notes"].as<JsonArray>();
    uint8_t noteIdx = 0;
    for (JsonVariant v : noteIds) {
        if (noteIdx < MAX_ACTUATORS_PER_INSTRUMENT) {
            inst.midi_notes[noteIdx] = v.as<uint8_t>();
            noteIdx++;
        }
    }
}

// ============================================================================
// Serialization / Deserialization — WiFi
// ============================================================================

void ConfigManager::serializeWiFi(const WiFiConfig& wifi, JsonObject& obj) {
    obj["ssid"] = wifi.ssid;
    obj["password"] = wifi.password;
    obj["ap_password"] = wifi.ap_password;
    obj["hostname"] = wifi.hostname;
    obj["enabled"] = wifi.enabled;
    obj["ap_fallback"] = wifi.ap_fallback;
}

void ConfigManager::deserializeWiFi(WiFiConfig& wifi, const JsonObject& obj) {
    strlcpy(wifi.ssid, obj["ssid"] | "", sizeof(wifi.ssid));
    strlcpy(wifi.password, obj["password"] | "", sizeof(wifi.password));
    strlcpy(wifi.ap_password, obj["ap_password"] | "", sizeof(wifi.ap_password));
    strlcpy(wifi.hostname, obj["hostname"] | WIFI_DEFAULT_HOSTNAME, sizeof(wifi.hostname));
    wifi.enabled = obj["enabled"] | true;
    wifi.ap_fallback = obj["ap_fallback"] | WIFI_AP_FALLBACK;
}

// ============================================================================
// Serialization / Deserialization — MIDI Input
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

    // AUDIT FIX (P0.4): heal configs saved before raw UDP and RTP-MIDI were
    // split onto distinct ports. AppleMIDI reserves rtp_port AND rtp_port+1, so
    // any UDP port colliding with that pair is moved to the dedicated default.
    if (midi.udp_port == midi.rtp_port || midi.udp_port == (uint16_t)(midi.rtp_port + 1)) {
        Serial.printf("[CONFIG] UDP port %d clashes with RTP-MIDI %d — moving UDP to %d\n",
                      midi.udp_port, midi.rtp_port, MIDI_UDP_PORT);
        midi.udp_port = MIDI_UDP_PORT;
    }
}

// ============================================================================
// Serialization / Deserialization — Power budget + Safety limits
// ============================================================================

void ConfigManager::serializePower(const PowerBudget& p, JsonObject& obj) {
    obj["global_max_ma"]   = p.global_max_ma;
    obj["servo_max_ma"]    = p.servo_bus_max_ma;
    obj["sol_max_ma"]      = p.solenoid_bus_max_ma;
    obj["max_polyphony"]   = p.global_max_polyphony;
    obj["smart_rejection"] = p.smart_rejection;
    JsonArray poly = obj["inst_polyphony"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) poly.add(p.instrument_max_polyphony[i]);
}

void ConfigManager::deserializePower(PowerBudget& p, const JsonObject& obj) {
    p.global_max_ma        = obj["global_max_ma"]   | (uint32_t)POWER_GLOBAL_MAX_MA;
    p.servo_bus_max_ma     = obj["servo_max_ma"]    | (uint32_t)POWER_SERVO_BUS_MAX_MA;
    p.solenoid_bus_max_ma  = obj["sol_max_ma"]      | (uint32_t)POWER_SOLENOID_BUS_MAX_MA;
    p.global_max_polyphony = obj["max_polyphony"]   | (uint8_t)POWER_MAX_POLYPHONY;
    p.smart_rejection      = obj["smart_rejection"] | true;
    JsonArray poly = obj["inst_polyphony"].as<JsonArray>();
    uint8_t i = 0;
    for (JsonVariant v : poly) {
        if (i >= MAX_INSTRUMENTS) break;
        p.instrument_max_polyphony[i++] = v.as<uint8_t>();
    }
    for (; i < MAX_INSTRUMENTS; i++) p.instrument_max_polyphony[i] = 4;
}

void ConfigManager::serializeSafety(const SafetyLimits& s, JsonObject& obj) {
    obj["max_duty_pct"]   = s.max_duty_pct;
    obj["max_freq_hz"]    = s.max_freq_hz;
    obj["watchdog_ms"]    = s.watchdog_ms;
    obj["max_polyphony"]  = s.max_polyphony;
    obj["max_current_ma"] = s.max_current_ma;
}

void ConfigManager::deserializeSafety(SafetyLimits& s, const JsonObject& obj) {
    s.max_duty_pct   = obj["max_duty_pct"]   | (uint8_t)SAFETY_MAX_DUTY_CYCLE;
    s.max_freq_hz    = obj["max_freq_hz"]    | (uint16_t)SAFETY_MAX_FREQ_HZ;
    s.watchdog_ms    = obj["watchdog_ms"]    | (uint16_t)SAFETY_WATCHDOG_MS;
    s.max_polyphony  = obj["max_polyphony"]  | (uint8_t)SAFETY_MAX_POLYPHONY;
    s.max_current_ma = obj["max_current_ma"] | (uint16_t)SAFETY_MAX_TOTAL_CURRENT_MA;
}

// ============================================================================
// Serialization / Deserialization — MIDI Routing
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
