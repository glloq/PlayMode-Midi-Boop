// ============================================================================
// PlayMode Midi B∞p — Contrôleur MIDI No-Code
// Point d'entrée principal
// ============================================================================
//
// Architecture dual-core ESP32 :
//   Core 0 : WiFi, MIDI parsing, Power Manager, Web UI (loop)
//   Core 1 : Scheduler temps réel, PCA/I²C, Safety Manager
//
// Bibliothèques requises :
//   - Adafruit PWM Servo Driver Library
//   - ArduinoJson
//   - MIDI Library (FortySevenEffects) — pour Serial MIDI
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
#include "power_manager.h"
#include "midi_input.h"
#include "event_normalizer.h"

// --- Objets globaux (Phase 1) ---
PCADriver      pcaDriver;
ActuatorEngine actuatorEngine(pcaDriver);
Scheduler      scheduler(actuatorEngine);
ConfigManager  configManager;

// --- Objets globaux (Phase 2) ---
SafetyManager safetyManager(pcaDriver);
MidiInput     midiInput;

// --- Objets globaux (Phase 5) ---
PowerManager   powerManager;
EventNormalizer eventNormalizer(scheduler, configManager);

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
    Serial.println("  PlayMode Midi B\u221ep v0.5");
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

    // 7. Initialiser le Power Manager (Phase 5)
    Serial.println("\n[INIT] Power Manager...");
    {
        PowerBudget budget = {};
        budget.global_max_ma         = POWER_GLOBAL_MAX_MA;
        budget.servo_bus_max_ma      = POWER_SERVO_BUS_MAX_MA;
        budget.solenoid_bus_max_ma   = POWER_SOLENOID_BUS_MAX_MA;
        budget.global_max_polyphony  = POWER_MAX_POLYPHONY;
        budget.smart_rejection       = true;  // Rejet intelligent par vélocité
        // Polyphonie par instrument : 4 par défaut (modifiable depuis Web UI phase 6)
        for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
            budget.instrument_max_polyphony[i] = 4;
        }
        powerManager.begin(budget);
    }

    // 8. Initialiser MIDI Input (Serial + WiFi/UDP + RTP-MIDI)
    Serial.println("\n[INIT] MIDI Input...");
    midiInput.begin(configManager.getWiFiConfig());

    // 9. Initialiser l'Event Normalizer avec le Power Manager (Phase 5)
    Serial.println("\n[INIT] Event Normalizer...");
    eventNormalizer.setPowerManager(&powerManager);
    eventNormalizer.begin();

    Serial.println("\n========================================");
    Serial.println("  Initialisation terminée — Phase 5");
    Serial.printf("  Heap libre : %d bytes\n", ESP.getFreeHeap());
    Serial.println("========================================\n");

#ifdef ENABLE_TEST_HARNESS
    // Lancer la séquence de test si en mode test
    delay(500);
    runTestSequence();
#endif
}

// ============================================================================
// LOOP — Core 0 : Pipeline MIDI complet + Power Manager
// ============================================================================
void loop() {
    // 1. Lire les entrées MIDI et remplir le jitter buffer
    midiInput.update();

    // 2. Retirer les événements prêts du jitter buffer et les normaliser
    MidiEvent midi_event;
    while (midiInput.readEvent(midi_event)) {
        eventNormalizer.processMidiEvent(midi_event);
    }

    // 3. Mise à jour périodique du Power Manager
    {
        ActuatorConfig* actuators = configManager.getActuators();
        uint8_t count = configManager.getActuatorCount();
        // Construire un tableau de pointeurs pour le power manager
        static ActuatorConfig* act_ptrs[MAX_ACTUATORS];
        for (uint8_t i = 0; i < count; i++) act_ptrs[i] = &actuators[i];
        powerManager.update(act_ptrs, count);
    }

    // 4. Affichage périodique de l'état (toutes les 5 secondes)
    static uint32_t last_status = 0;
    uint32_t now = millis();

    if (now - last_status > 5000) {
        last_status = now;

        const PowerStats& pwr = powerManager.getStats();

        Serial.printf("[STATUS] Sched: %d queue, %d traités | "
                      "MIDI: %d reçus, %d routés, %d unmapped, %d power-rejected | "
                      "Safety: %dmA, %d actifs%s | "
                      "Power: %umA/%umA (%u%%) srv=%umA sol=%umA%s | "
                      "Heap: %d\n",
                      scheduler.getQueuedEventCount(),
                      scheduler.getProcessedCount(),
                      midiInput.getReceivedCount(),
                      eventNormalizer.getRoutedCount(),
                      eventNormalizer.getUnmappedCount(),
                      eventNormalizer.getPowerRejectedCount(),
                      safetyManager.getEstimatedCurrentMA(),
                      safetyManager.getActiveActuatorCount(),
                      safetyManager.isKillSwitchActive() ? " [KILL]" :
                          (safetyManager.isDegradationActive() ? " [DEGRAD]" : ""),
                      pwr.total_estimated_ma,
                      powerManager.getBudget().global_max_ma,
                      pwr.budget_used_percent,
                      pwr.servo_bus_ma,
                      pwr.solenoid_bus_ma,
                      pwr.degradation_active ? " [DEGRAD]" : "",
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
        evt.trigger_time_us = now_us + 500000 + (i * 300000);
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
