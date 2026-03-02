#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "config.h"
#include "midi_types.h"

// ============================================================================
// PlayMode — WiFi Manager (Phase 3)
// ============================================================================

class WiFiManager {
public:
    WiFiManager();

    // Initializes WiFi with the given configuration.
    // Blocks for timeout_ms while waiting for STA connection.
    // Returns true if connected (STA) or AP started (fallback).
    bool begin(const WiFiConfig& config, uint32_t timeout_ms = WIFI_CONNECT_TIMEOUT_MS);

    // Checks if WiFi is connected in STA mode
    bool isConnected() const;

    // Checks if AP mode is active
    bool isAP() const;

    // Returns the current IP address (STA or AP)
    IPAddress getIP() const;

    // Maintains WiFi connection (reconnects if disconnected).
    // Call periodically from loop().
    void maintain();

    // WiFi signal strength (dBm)
    int8_t getRSSI() const;

    // Disconnects and stops WiFi
    void stop();

private:
    WiFiConfig _config;
    bool _connected;
    bool _ap_mode;
    uint32_t _last_reconnect_attempt;
    DNSServer _dns;

    bool connectSTA(uint32_t timeout_ms);
    bool startAP();
    void startMDNS();
};

#endif // WIFI_MANAGER_H
