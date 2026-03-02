#include "wifi_manager.h"

// ============================================================================
// PlayMode — WiFi Manager (implementation)
// ============================================================================

WiFiManager::WiFiManager()
    : _connected(false),
      _ap_mode(false),
      _last_reconnect_attempt(0) {
    memset(&_config, 0, sizeof(_config));
}

bool WiFiManager::begin(const WiFiConfig& config, uint32_t timeout_ms) {
    _config = config;

    if (!_config.enabled) {
        Serial.println("[WIFI] Disabled");
        return false;
    }

    // Attempt STA connection only if an SSID is configured
    if (strlen(_config.ssid) > 0) {
        if (connectSTA(timeout_ms)) {
            startMDNS();
            return true;
        }
    } else {
        Serial.println("[WIFI] No SSID configured — switching to AP mode");
    }

    // Fallback to AP mode (or direct AP if no SSID)
    if (_config.ap_fallback || strlen(_config.ssid) == 0) {
        if (startAP()) {
            startMDNS();
            return true;
        }
    }

    Serial.println("[WIFI] WiFi connection failed");
    return false;
}

bool WiFiManager::isConnected() const {
    return _connected && WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isAP() const {
    return _ap_mode;
}

IPAddress WiFiManager::getIP() const {
    if (_ap_mode) {
        return WiFi.softAPIP();
    }
    return WiFi.localIP();
}

void WiFiManager::maintain() {
    // In AP mode: process DNS captive portal requests
    if (_ap_mode) {
        _dns.processNextRequest();
        return;
    }

    if (!_config.enabled) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!_connected) {
            _connected = true;
            Serial.printf("[WIFI] Reconnected: %s\n", WiFi.localIP().toString().c_str());
        }
        return;
    }

    // Disconnected — attempt periodic reconnection
    _connected = false;
    uint32_t now = millis();
    if (now - _last_reconnect_attempt >= WIFI_RECONNECT_INTERVAL) {
        _last_reconnect_attempt = now;
        Serial.println("[WIFI] Reconnection attempt...");
        WiFi.reconnect();
    }
}

int8_t WiFiManager::getRSSI() const {
    if (_connected) {
        return WiFi.RSSI();
    }
    return 0;
}

void WiFiManager::stop() {
    if (_ap_mode) {
        _dns.stop();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _connected = false;
    _ap_mode = false;
    Serial.println("[WIFI] Stopped");
}

// ============================================================================
// STA Connection
// ============================================================================
bool WiFiManager::connectSTA(uint32_t timeout_ms) {
    Serial.printf("[WIFI] Connecting to '%s'...\n", _config.ssid);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(_config.hostname);
    WiFi.begin(_config.ssid, _config.password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        _connected = true;
        _ap_mode = false;
        Serial.printf("[WIFI] Connected — IP: %s (RSSI: %d dBm)\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }

    Serial.println("[WIFI] STA connection failed");
    return false;
}

// ============================================================================
// AP Mode (fallback) + Captive Portal DNS
// ============================================================================
bool WiFiManager::startAP() {
    Serial.printf("[WIFI] Starting AP '%s'...\n", _config.hostname);

    WiFi.mode(WIFI_AP);
    bool result = WiFi.softAP(_config.hostname);

    if (result) {
        _ap_mode = true;
        _connected = false;

        // Start DNS server: redirects ALL DNS requests to the AP IP.
        // This forces phones to open the captive portal automatically.
        _dns.start(53, "*", WiFi.softAPIP());

        Serial.printf("[WIFI] AP started — IP: %s  (DNS captive portal active)\n",
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[WIFI] AP start failed");
    }

    return result;
}

// ============================================================================
// mDNS + service advertisement
// ============================================================================
void WiFiManager::startMDNS() {
    if (MDNS.begin(_config.hostname)) {
        // Advertise AppleMIDI service for discovery by DAWs
        MDNS.addService("apple-midi", "udp", MIDI_RTP_PORT);
        Serial.printf("[WIFI] mDNS : %s.local\n", _config.hostname);
    } else {
        Serial.println("[WIFI] mDNS start failed");
    }
}
