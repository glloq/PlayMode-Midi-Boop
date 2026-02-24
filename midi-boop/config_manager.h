#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode Midi B∞p — Config Manager (JSON / LittleFS)
// ============================================================================

class ConfigManager {
public:
    ConfigManager();

    // Initialise LittleFS
    bool begin();

    // Charge la configuration depuis le fichier JSON
    bool load();

    // Sauvegarde la configuration dans le fichier JSON
    bool save();

    // Remet la configuration par défaut
    void loadDefaults();

    // Vérifie si une config existe sur le filesystem
    bool configExists();

    // --- Accesseurs ---

    BusConfig* getBuses();
    uint8_t getBusCount() const;

    ActuatorConfig* getActuators();
    uint8_t getActuatorCount() const;

    InstrumentConfig* getInstruments();
    uint8_t getInstrumentCount() const;

    // Ajoute un actionneur
    bool addActuator(const ActuatorConfig& actuator);

    // Ajoute un instrument
    bool addInstrument(const InstrumentConfig& instrument);

    // --- WiFi ---

    WiFiConfig* getWiFiConfig();
    void setWiFiConfig(const WiFiConfig& config);

    // --- MIDI Input ---

    MidiInputConfig* getMidiInputConfig();

    // --- Routage MIDI ---

    MidiRoutingConfig* getRoutingConfigs();
    uint8_t getRoutingCount() const;
    bool addRoutingConfig(const MidiRoutingConfig& routing);

    // Cherche le routage pour un instrument donné (par index)
    MidiRoutingConfig* getRoutingForInstrument(uint8_t instrument_index);

    // Version de la config
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

    // Sérialise un actionneur en JSON
    void serializeActuator(const ActuatorConfig& act, JsonObject& obj);

    // Désérialise un actionneur depuis JSON
    void deserializeActuator(ActuatorConfig& act, const JsonObject& obj);

    // Sérialise un bus en JSON
    void serializeBus(const BusConfig& bus, JsonObject& obj);

    // Désérialise un bus depuis JSON
    void deserializeBus(BusConfig& bus, const JsonObject& obj);

    // Sérialise un instrument en JSON
    void serializeInstrument(const InstrumentConfig& inst, JsonObject& obj);

    // Désérialise un instrument depuis JSON
    void deserializeInstrument(InstrumentConfig& inst, const JsonObject& obj);

    // Sérialise la config WiFi en JSON
    void serializeWiFi(const WiFiConfig& wifi, JsonObject& obj);

    // Désérialise la config WiFi depuis JSON
    void deserializeWiFi(WiFiConfig& wifi, const JsonObject& obj);

    // Sérialise la config MIDI Input en JSON
    void serializeMidiInput(const MidiInputConfig& midi, JsonObject& obj);

    // Désérialise la config MIDI Input depuis JSON
    void deserializeMidiInput(MidiInputConfig& midi, const JsonObject& obj);

    // Sérialise un routage MIDI en JSON
    void serializeRouting(const MidiRoutingConfig& routing, JsonObject& obj);

    // Désérialise un routage MIDI depuis JSON
    void deserializeRouting(MidiRoutingConfig& routing, const JsonObject& obj);
};

#endif // CONFIG_MANAGER_H
