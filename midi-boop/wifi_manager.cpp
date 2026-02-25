#include "wifi_manager.h"

// ============================================================================
// PlayMode Midi B∞p — WiFi Manager (implémentation)
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
        Serial.println("[WIFI] Désactivé");
        return false;
    }

    // Tenter la connexion en mode STA uniquement si un SSID est configuré
    if (strlen(_config.ssid) > 0) {
        if (connectSTA(timeout_ms)) {
            startMDNS();
            return true;
        }
    } else {
        Serial.println("[WIFI] Aucun SSID configuré — passage en mode AP");
    }

    // Fallback en mode AP (ou AP direct si pas de SSID)
    if (_config.ap_fallback || strlen(_config.ssid) == 0) {
        if (startAP()) {
            startMDNS();
            return true;
        }
    }

    Serial.println("[WIFI] Échec connexion WiFi");
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
    // En mode AP : traiter les requêtes DNS captive portal
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
            Serial.printf("[WIFI] Reconnecté : %s\n", WiFi.localIP().toString().c_str());
        }
        return;
    }

    // Déconnecté — tenter reconnexion périodique
    _connected = false;
    uint32_t now = millis();
    if (now - _last_reconnect_attempt >= WIFI_RECONNECT_INTERVAL) {
        _last_reconnect_attempt = now;
        Serial.println("[WIFI] Tentative reconnexion...");
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
    Serial.println("[WIFI] Arrêté");
}

// ============================================================================
// Connexion STA
// ============================================================================
bool WiFiManager::connectSTA(uint32_t timeout_ms) {
    Serial.printf("[WIFI] Connexion à '%s'...\n", _config.ssid);

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
        Serial.printf("[WIFI] Connecté — IP: %s (RSSI: %d dBm)\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }

    Serial.println("[WIFI] Échec connexion STA");
    return false;
}

// ============================================================================
// Mode AP (fallback) + Captive Portal DNS
// ============================================================================
bool WiFiManager::startAP() {
    Serial.printf("[WIFI] Démarrage AP '%s'...\n", _config.hostname);

    WiFi.mode(WIFI_AP);
    bool result = WiFi.softAP(_config.hostname);

    if (result) {
        _ap_mode = true;
        _connected = false;

        // Démarrer serveur DNS : redirige TOUTES les requêtes DNS vers l'IP AP.
        // Cela force les téléphones à ouvrir le portail captif automatiquement.
        _dns.start(53, "*", WiFi.softAPIP());

        Serial.printf("[WIFI] AP démarré — IP: %s  (DNS captive portal actif)\n",
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[WIFI] Échec démarrage AP");
    }

    return result;
}

// ============================================================================
// mDNS + service advertisement
// ============================================================================
void WiFiManager::startMDNS() {
    if (MDNS.begin(_config.hostname)) {
        // Advertiser le service AppleMIDI pour découverte par les DAWs
        MDNS.addService("apple-midi", "udp", MIDI_RTP_PORT);
        Serial.printf("[WIFI] mDNS : %s.local\n", _config.hostname);
    } else {
        Serial.println("[WIFI] Échec démarrage mDNS");
    }
}
