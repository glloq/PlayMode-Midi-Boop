// ============================================================================
// PlayMode Midi B∞p — Contrôleur MIDI No-Code
// Point d'entrée principal
// ============================================================================
//
// Architecture dual-core ESP32 :
//   Core 0 : WiFi, MIDI parsing, Web UI (loop)
//   Core 1 : Scheduler temps réel, PCA/I²C, sécurité
//
// Bibliothèques requises :
//   - Adafruit PWM Servo Driver Library
//   - ArduinoJson
//   - MIDI Library (FortySevenEffects) — pour AppleMIDI
//   - AppleMIDI (lathoub) — pour RTP-MIDI
//   - ESPAsyncWebServer (phase 6)
//   - AsyncTCP (phase 6)
//
// ============================================================================

#include "config.h"
#include "types.h"
#include "midi_types.h"
#include "pca_driver.h"
#include "actuator_engine.h"
#include "scheduler.h"
#include "config_manager.h"
#include "safety_manager.h"
#include "wifi_manager.h"
#include "jitter_buffer.h"
#include "midi_transport.h"
#include "midi_dispatcher.h"

// --- Objets globaux (Phase 1) ---
PCADriver pcaDriver;
ActuatorEngine actuatorEngine(pcaDriver);
Scheduler scheduler(actuatorEngine);
ConfigManager configManager;

// --- Objets globaux (Phase 2) ---
SafetyManager safetyManager(pcaDriver);

// --- Objets globaux (Phase 3+4 — Pipeline MIDI) ---
WiFiManager wifiManager;
JitterBuffer jitterBuffer;
MidiTransport midiTransport(jitterBuffer);
MidiDispatcher midiDispatcher(scheduler, configManager);

// --- Mode test (décommenter pour activer le test harness sans MIDI) ---
// #define ENABLE_TEST_HARNESS

#ifdef ENABLE_TEST_HARNESS
void setupTestActuators();
void runTestSequence();
#endif

// ============================================================================
// SETUP — Initialisation
// ============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);  // Attendre la stabilisation du port série

    Serial.println("========================================");
    Serial.println("  PlayMode Midi B∞p v0.4");
    Serial.println("  No-Code MIDI Controller");
    Serial.println("========================================");
    Serial.printf("  Core actuel : %d\n", xPortGetCoreID());
    Serial.printf("  Fréquence CPU : %d MHz\n", getCpuFrequencyMhz());
    Serial.printf("  Heap libre : %d bytes\n", ESP.getFreeHeap());
    Serial.println("========================================");

    // 1. Initialiser le Config Manager (LittleFS)
    Serial.println("\n[INIT] Config Manager...");
    if (!configManager.begin()) {
        Serial.println("[INIT] ERREUR : Config Manager");
    }

    // 2. Initialiser le driver PCA9685 (dual-bus I²C)
    Serial.println("\n[INIT] PCA Driver (I²C dual-bus)...");
    if (!pcaDriver.begin()) {
        Serial.println("[INIT] ATTENTION : Problème init PCA (certains bus manquants ?)");
    }

    // 3. Configurer les actionneurs (depuis config ou test)
    Serial.println("\n[INIT] Actionneurs...");
    if (configManager.getActuatorCount() > 0) {
        // Charger depuis la config sauvegardée
        ActuatorConfig* actuators = configManager.getActuators();
        uint8_t count = configManager.getActuatorCount();
        for (uint8_t i = 0; i < count; i++) {
            actuatorEngine.initActuator(actuators[i]);
            scheduler.registerActuator(&actuators[i]);
        }
        Serial.printf("[INIT] %d actionneurs chargés depuis config\n", count);
    }
#ifdef ENABLE_TEST_HARNESS
    else {
        // Mode test : créer des actionneurs hardcodés
        setupTestActuators();
    }
#endif

    // 4. Activer les bus PCA (OE = LOW)
    Serial.println("\n[INIT] Activation des sorties PCA...");
    pcaDriver.enableBus(0, true);
    pcaDriver.enableBus(1, true);

    // 5. Initialiser le Safety Manager
    Serial.println("\n[INIT] Safety Manager...");
    safetyManager.begin();

    // 6. Démarrer le scheduler sur Core 1 (avec safety intégré)
    Serial.println("\n[INIT] Scheduler...");
    scheduler.setSafetyManager(&safetyManager);
    if (!scheduler.begin()) {
        Serial.println("[INIT] ERREUR : Scheduler");
    }

    // 7. Initialiser le WiFi Manager
    Serial.println("\n[INIT] WiFi Manager...");
    WiFiConfig* wifiCfg = configManager.getWiFiConfig();
    if (wifiCfg->enabled) {
        if (wifiManager.begin(*wifiCfg)) {
            Serial.printf("[INIT] WiFi connecté : %s\n", wifiManager.getIP().toString().c_str());
        } else {
            Serial.println("[INIT] ATTENTION : WiFi non disponible");
        }
    } else {
        Serial.println("[INIT] WiFi désactivé");
    }

    // 8. Configurer le jitter buffer et démarrer les transports MIDI
    Serial.println("\n[INIT] MIDI Transport...");
    MidiInputConfig* midiCfg = configManager.getMidiInputConfig();
    jitterBuffer.setDepth(midiCfg->jitter_buffer_ms);
    if (!midiTransport.begin(*midiCfg)) {
        Serial.println("[INIT] ATTENTION : Aucun transport MIDI actif");
    }

    // 9. Initialiser le MIDI Dispatcher (mapping notes/CC)
    Serial.println("\n[INIT] MIDI Dispatcher...");
    midiDispatcher.refreshConfig();

    Serial.println("\n========================================");
    Serial.println("  Initialisation terminée — Phase 4");
    Serial.printf("  Heap libre : %d bytes\n", ESP.getFreeHeap());
    Serial.println("========================================\n");

#ifdef ENABLE_TEST_HARNESS
    // Lancer la séquence de test si en mode test
    delay(500);
    runTestSequence();
#endif
}

// ============================================================================
// LOOP — Core 0 : Pipeline MIDI complet
// ============================================================================
void loop() {
    // 1. Maintenance WiFi (reconnexion si nécessaire)
    wifiManager.maintain();

    // 2. Lire les entrées MIDI (remplit le jitter buffer)
    midiTransport.poll();

    // 3. Retirer les messages prêts du jitter buffer et les dispatcher
    MidiMessage msg;
    while (jitterBuffer.pop(msg)) {
        midiDispatcher.dispatch(msg);
    }

    // 4. Affichage périodique de l'état
    static uint32_t last_status = 0;
    uint32_t now = millis();

    if (now - last_status > 5000) {
        last_status = now;
        Serial.printf("[STATUS] Sched: %d queue, %d traités | "
                      "MIDI: S:%d U:%d R:%d | Disp: %d routés, %d dropped | "
                      "Safety: %dmA, %d actifs%s | Heap: %d\n",
                      scheduler.getQueuedEventCount(),
                      scheduler.getProcessedCount(),
                      midiTransport.getSerialByteCount(),
                      midiTransport.getUdpPacketCount(),
                      midiTransport.getRtpPacketCount(),
                      midiDispatcher.getDispatchedCount(),
                      midiDispatcher.getDroppedCount(),
                      safetyManager.getEstimatedCurrentMA(),
                      safetyManager.getActiveActuatorCount(),
                      safetyManager.isKillSwitchActive() ? " [KILL]" :
                          (safetyManager.isDegradationActive() ? " [DEGRAD]" : ""),
                      ESP.getFreeHeap());
    }

    delay(1);  // Yield minimal — MIDI nécessite une lecture fréquente
}

// ============================================================================
// Test Harness (compilation conditionnelle)
// ============================================================================

#ifdef ENABLE_TEST_HARNESS

// Stockage statique pour les actionneurs de test
static ActuatorConfig testActuators[4];
static uint8_t testActuatorCount = 0;

void setupTestActuators() {
    Serial.println("[TEST] Création d'actionneurs de test...");

    // Servo 0 — Mode Frappe (bus 0, PCA 0x40, canal 0)
    testActuators[0] = {};
    testActuators[0].id = 0;
    testActuators[0].type = ACT_SERVO;
    testActuators[0].bus_id = 0;
    testActuators[0].pca_address = 0x40;
    testActuators[0].pca_channel = 0;
    testActuators[0].behavior = SERVO_FRAPPE;
    testActuators[0].angle_initial = 90;
    testActuators[0].amplitude = 45;
    testActuators[0].speed_ms = 150;
    testActuators[0].latency_ms = 10;
    testActuators[0].enabled = true;

    // Servo 1 — Mode Alterné (bus 0, PCA 0x40, canal 1)
    testActuators[1] = {};
    testActuators[1].id = 1;
    testActuators[1].type = ACT_SERVO;
    testActuators[1].bus_id = 0;
    testActuators[1].pca_address = 0x40;
    testActuators[1].pca_channel = 1;
    testActuators[1].behavior = SERVO_ALTERNE;
    testActuators[1].angle_initial = 60;
    testActuators[1].amplitude = 0;
    testActuators[1].speed_ms = 100;
    testActuators[1].angle_b = 120;
    testActuators[1].latency_ms = 12;
    testActuators[1].enabled = true;

    // Solénoïde 0 — Mode Frappe (bus 1, PCA 0x40, canal 0)
    testActuators[2] = {};
    testActuators[2].id = 2;
    testActuators[2].type = ACT_SOLENOID;
    testActuators[2].bus_id = 1;
    testActuators[2].pca_address = 0x40;
    testActuators[2].pca_channel = 0;
    testActuators[2].behavior = SOL_FRAPPE;
    testActuators[2].pulse_ms = 20;
    testActuators[2].latency_ms = 5;
    testActuators[2].enabled = true;

    // Solénoïde 1 — Mode Hit-and-Hold (bus 1, PCA 0x40, canal 1)
    testActuators[3] = {};
    testActuators[3].id = 3;
    testActuators[3].type = ACT_SOLENOID;
    testActuators[3].bus_id = 1;
    testActuators[3].pca_address = 0x40;
    testActuators[3].pca_channel = 1;
    testActuators[3].behavior = SOL_HIT_AND_HOLD;
    testActuators[3].pwm_initial = 4095;
    testActuators[3].pwm_hold = 2048;
    testActuators[3].ramp_ms = 50;
    testActuators[3].latency_ms = 3;
    testActuators[3].enabled = true;

    testActuatorCount = 4;

    // Initialiser et enregistrer les actionneurs
    for (uint8_t i = 0; i < testActuatorCount; i++) {
        actuatorEngine.initActuator(testActuators[i]);
        scheduler.registerActuator(&testActuators[i]);
    }

    Serial.printf("[TEST] %d actionneurs de test créés\n", testActuatorCount);
}

void runTestSequence() {
    Serial.println("\n[TEST] === Début séquence de test ===\n");

    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // Test 1 : Servo Frappe (actionneur 0) — Note On vélocité 100
    {
        SchedulerEvent evt = {};
        evt.trigger_time_us = now_us + 100000;  // +100ms
        evt.actuator_id = 0;
        evt.action = ACTION_NOTE_ON;
        evt.velocity = 100;
        evt.priority = 0;

        if (scheduler.pushEvent(evt)) {
            Serial.println("[TEST] Servo Frappe programmé à +100ms");
        }
    }

    // Test 2 : Servo Alterné (actionneur 1) — 3 notes successives
    for (int i = 0; i < 3; i++) {
        SchedulerEvent evt = {};
        evt.trigger_time_us = now_us + 500000 + (i * 300000);  // +500ms, +800ms, +1100ms
        evt.actuator_id = 1;
        evt.action = ACTION_NOTE_ON;
        evt.velocity = 80;
        evt.priority = 0;

        if (scheduler.pushEvent(evt)) {
            Serial.printf("[TEST] Servo Alterné #%d programmé à +%dms\n", i + 1, 500 + (i * 300));
        }
    }

    // Test 3 : Solénoïde Frappe (actionneur 2) — Note On vélocité 127
    {
        SchedulerEvent evt = {};
        evt.trigger_time_us = now_us + 2000000;  // +2000ms
        evt.actuator_id = 2;
        evt.action = ACTION_NOTE_ON;
        evt.velocity = 127;
        evt.priority = 0;

        if (scheduler.pushEvent(evt)) {
            Serial.println("[TEST] Solénoïde Frappe programmé à +2000ms");
        }
    }

    // Test 4 : Solénoïde Hit-and-Hold (actionneur 3) — Note On puis Note Off
    {
        SchedulerEvent evt_on = {};
        evt_on.trigger_time_us = now_us + 3000000;  // +3000ms
        evt_on.actuator_id = 3;
        evt_on.action = ACTION_NOTE_ON;
        evt_on.velocity = 100;
        evt_on.priority = 0;

        SchedulerEvent evt_off = {};
        evt_off.trigger_time_us = now_us + 4000000;  // +4000ms (1s de maintien)
        evt_off.actuator_id = 3;
        evt_off.action = ACTION_NOTE_OFF;
        evt_off.velocity = 0;
        evt_off.priority = 0;

        if (scheduler.pushEvent(evt_on) && scheduler.pushEvent(evt_off)) {
            Serial.println("[TEST] Solénoïde Hit-and-Hold programmé : ON +3000ms, OFF +4000ms");
        }
    }

    Serial.println("\n[TEST] Séquence envoyée au scheduler");
    Serial.println("[TEST] Les événements seront exécutés automatiquement\n");
}

#endif // ENABLE_TEST_HARNESS
