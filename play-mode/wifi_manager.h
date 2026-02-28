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

    // Initialise le WiFi avec la configuration donnée.
    // Bloque pendant timeout_ms en attendant la connexion STA.
    // Retourne true si connecté (STA) ou AP démarré (fallback).
    bool begin(const WiFiConfig& config, uint32_t timeout_ms = WIFI_CONNECT_TIMEOUT_MS);

    // Vérifie si le WiFi est connecté en mode STA
    bool isConnected() const;

    // Vérifie si le mode AP est actif
    bool isAP() const;

    // Retourne l'adresse IP courante (STA ou AP)
    IPAddress getIP() const;

    // Maintient la connexion WiFi (reconnexion si déconnecté).
    // Appeler périodiquement depuis loop().
    void maintain();

    // Force de signal WiFi (dBm)
    int8_t getRSSI() const;

    // Déconnecte et arrête le WiFi
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
