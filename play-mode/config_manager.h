#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode — Config Manager (JSON / LittleFS)
// ============================================================================

class ConfigManager {
public:
    ConfigManager();

    // Initializes LittleFS
    bool begin();

    // Loads configuration from the JSON file
    bool load();

    // Saves configuration to the JSON file
    bool save();

    // Resets configuration to defaults
    void loadDefaults();

    // Checks if a config exists on the filesystem
    bool configExists();

    // --- Bus accessors ---

    BusConfig* getBuses();
    uint8_t getBusCount() const;

    // --- Actuator accessors ---

    ActuatorConfig* getActuators();
    uint8_t getActuatorCount() const;

    // AUDIT FIX (point B): serialise structural actuator-array edits against
    // the real-time Scheduler. Web handlers lock() before any add/update/
    // remove, then unlock() once the scheduler view has been resynced.
    bool lockActuators(uint32_t timeout_ms = 100);
    void unlockActuators();

    // Adds or updates an actuator (updates if same ID exists)
    bool addActuator(const ActuatorConfig& actuator);

    // Removes an actuator by ID (decrements the counter)
    bool removeActuator(uint8_t id);

    // --- Instrument accessors ---

    InstrumentConfig* getInstruments();
    uint8_t getInstrumentCount() const;

    // Adds an instrument (always creates a new slot)
    bool addInstrument(const InstrumentConfig& instrument);

    // Removes an instrument by index (decrements the counter)
    bool removeInstrument(uint8_t index);

    // --- WiFi + MIDI ---

    WiFiConfig* getWiFiConfig();
    void setWiFiConfig(const WiFiConfig& config);

    MidiInputConfig* getMidiInputConfig();
    void setMidiInputConfig(const MidiInputConfig& config);

    // --- MIDI Routing ---

    MidiRoutingConfig* getRoutingConfigs();
    uint8_t getRoutingCount() const;
    bool addRoutingConfig(const MidiRoutingConfig& routing);

    // Finds the routing for a given instrument (by index)
    MidiRoutingConfig* getRoutingForInstrument(uint8_t instrument_index);

    // Config version
    uint8_t getVersion() const;

private:
    WiFiConfig _wifi_config;
    MidiInputConfig _midi_input_config;
    BusConfig _buses[2];
    ActuatorConfig _actuators[MAX_ACTUATORS];
    uint8_t _actuator_count;
    InstrumentConfig _instruments[MAX_INSTRUMENTS];
    uint8_t _instrument_count;
    MidiRoutingConfig _routing_configs[MAX_INSTRUMENTS];
    uint8_t _routing_count;
    uint8_t _version;

    // Serializes an actuator to JSON
    void serializeActuator(const ActuatorConfig& act, JsonObject& obj);

    // Deserializes an actuator from JSON
    void deserializeActuator(ActuatorConfig& act, const JsonObject& obj);

    // Serializes a bus to JSON
    void serializeBus(const BusConfig& bus, JsonObject& obj);

    // Deserializes a bus from JSON
    void deserializeBus(BusConfig& bus, const JsonObject& obj);

    // Serializes an instrument to JSON
    void serializeInstrument(const InstrumentConfig& inst, JsonObject& obj);

    // Deserializes an instrument from JSON
    void deserializeInstrument(InstrumentConfig& inst, const JsonObject& obj);

    // Serializes the WiFi config to JSON
    void serializeWiFi(const WiFiConfig& wifi, JsonObject& obj);

    // Deserializes the WiFi config from JSON
    void deserializeWiFi(WiFiConfig& wifi, const JsonObject& obj);

    // Serializes the MIDI Input config to JSON
    void serializeMidiInput(const MidiInputConfig& midi, JsonObject& obj);

    // Deserializes the MIDI Input config from JSON
    void deserializeMidiInput(MidiInputConfig& midi, const JsonObject& obj);

    // Serializes a MIDI routing to JSON
    void serializeRouting(const MidiRoutingConfig& routing, JsonObject& obj);

    // Deserializes a MIDI routing from JSON
    void deserializeRouting(MidiRoutingConfig& routing, const JsonObject& obj);
};

#endif // CONFIG_MANAGER_H
