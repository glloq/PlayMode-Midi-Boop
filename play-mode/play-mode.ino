// ============================================================================
// PlayMode — No-Code MIDI Controller
// Main entry point
// ============================================================================
//
// ESP32 dual-core architecture:
//   Core 0: WiFi, MIDI parsing, Power Manager, Web UI (loop)
//   Core 1: Real-time scheduler, PCA/I²C, Safety Manager
//
// Required libraries:
//   - Adafruit PWM Servo Driver Library
//   - ArduinoJson
//   - MIDI Library (FortySevenEffects) — for Serial MIDI
//   - AppleMIDI (lathoub) — for RTP-MIDI
//   - ESPAsyncWebServer (phase 6)
//   - AsyncTCP (phase 6)
//   - driver/i2s.h (ESP-IDF, built-in) — for the calibrator (phase 7)
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
#include "wifi_manager.h"
#include "jitter_buffer.h"
#include "midi_transport.h"
#include "midi_dispatcher.h"
#include "calibrator.h"
#include "test_manager.h"
#include "log_manager.h"
#include "web_server.h"

// --- Global objects (Phase 1) ---
PCADriver      pcaDriver;
ActuatorEngine actuatorEngine(pcaDriver);
Scheduler      scheduler(actuatorEngine);
ConfigManager  configManager;

// --- Global objects (Phase 2) ---
SafetyManager safetyManager(pcaDriver);

// --- Global objects (Phase 3+4 — MIDI Pipeline) ---
WiFiManager wifiManager;
JitterBuffer jitterBuffer;
MidiTransport midiTransport(jitterBuffer);
MidiDispatcher midiDispatcher(scheduler, configManager);

// --- Global objects (Phase 5) ---
PowerManager powerManager;

// --- Global objects (Phase 6) ---
WebServer webServer;

// --- Global objects (Phase 7) ---
Calibrator calibrator(scheduler, configManager);

// --- Global objects (Phase 8) ---
TestManager testManager(scheduler, configManager);

// --- LED Status ---
enum LedState { LED_OFF, LED_BOOT, LED_AP, LED_STA, LED_ERROR };
static LedState ledState = LED_BOOT;
static uint32_t ledLastToggle = 0;
static bool     ledOn = false;

void ledInit() {
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, HIGH);
    ledOn = true;
}

void ledSet(LedState state) {
    ledState = state;
}

void ledUpdate() {
    uint32_t now = millis();
    switch (ledState) {
        case LED_OFF:
            if (ledOn) { digitalWrite(LED_STATUS_PIN, LOW); ledOn = false; }
            break;
        case LED_BOOT:
            // Fast blink 100ms
            if (now - ledLastToggle >= 100) {
                ledLastToggle = now;
                ledOn = !ledOn;
                digitalWrite(LED_STATUS_PIN, ledOn ? HIGH : LOW);
            }
            break;
        case LED_AP:
            // Slow blink 500ms (AP mode, waiting for config)
            if (now - ledLastToggle >= 500) {
                ledLastToggle = now;
                ledOn = !ledOn;
                digitalWrite(LED_STATUS_PIN, ledOn ? HIGH : LOW);
            }
            break;
        case LED_STA:
            // Solid on (connected in STA mode)
            if (!ledOn) { digitalWrite(LED_STATUS_PIN, HIGH); ledOn = true; }
            break;
        case LED_ERROR:
            // Double fast flash every 2s
            {
                uint32_t phase = now % 2000;
                bool on = (phase < 100) || (phase >= 200 && phase < 300);
                if (on != ledOn) {
                    ledOn = on;
                    digitalWrite(LED_STATUS_PIN, ledOn ? HIGH : LOW);
                }
            }
            break;
    }
}

// --- Test mode (uncomment to enable the test harness without MIDI) ---
// #define ENABLE_TEST_HARNESS

#ifdef ENABLE_TEST_HARNESS
void setupTestActuators();
void runTestSequence();
#endif

// ============================================================================
// SETUP — Initialization
// ============================================================================
void setup() {
    // LED: fast blink during boot
    ledInit();

    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);  // Wait for serial port stabilization

    // Initialize the logger first (before everything else)
    logger.begin();

    Serial.println("========================================");
    Serial.println("  PlayMode Midi B\u221ep v0.9");
    Serial.println("  No-Code MIDI Controller");
    Serial.println("========================================");
    Serial.printf("  Current core: %d\n", xPortGetCoreID());
    Serial.printf("  CPU frequency: %d MHz\n", getCpuFrequencyMhz());
    Serial.printf("  Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("========================================");

    logger.log(LOG_INFO, CAT_SYSTEM, "Starting PlayMode v0.9");

    // 1. Initialize the Config Manager (LittleFS)
    Serial.println("\n[INIT] Config Manager...");
    if (!configManager.begin()) {
        Serial.println("[INIT] ERROR: Config Manager");
        logger.log(LOG_ERROR, CAT_SYSTEM, "Config Manager: LittleFS init failed");
    } else {
        logger.log(LOG_INFO, CAT_SYSTEM, "Config: %d act, %d inst",
                   configManager.getActuatorCount(), configManager.getInstrumentCount());
    }

    // 2. Initialize the PCA9685 driver (dual-bus I²C)
    Serial.println("\n[INIT] PCA Driver (I²C dual-bus)...");
    if (!pcaDriver.begin()) {
        Serial.println("[INIT] WARNING: PCA init problem (some buses missing?)");
    }

    // 3. Configure actuators (from config or test)
    Serial.println("\n[INIT] Actuators...");
    if (configManager.getActuatorCount() > 0) {
        ActuatorConfig* actuators = configManager.getActuators();
        uint8_t count = configManager.getActuatorCount();
        for (uint8_t i = 0; i < count; i++) {
            actuatorEngine.initActuator(actuators[i]);
            scheduler.registerActuator(&actuators[i]);
        }
        Serial.printf("[INIT] %d actuators loaded from config\n", count);
    }
#ifdef ENABLE_TEST_HARNESS
    else {
        setupTestActuators();
    }
#endif

    // 4. Enable PCA buses (OE = LOW)
    Serial.println("\n[INIT] Enabling PCA outputs...");
    pcaDriver.enableBus(0, true);
    pcaDriver.enableBus(1, true);

    // 5. Initialize the Safety Manager
    Serial.println("\n[INIT] Safety Manager...");
    safetyManager.begin();

    // 6. Start the scheduler on Core 1 (with integrated safety)
    Serial.println("\n[INIT] Scheduler...");
    scheduler.setSafetyManager(&safetyManager);
    if (!scheduler.begin()) {
        Serial.println("[INIT] ERROR: Scheduler");
        logger.log(LOG_ERROR, CAT_SCHED, "Scheduler: Core 1 start failed");
    } else {
        logger.log(LOG_INFO, CAT_SCHED, "Scheduler started on Core 1");
    }

    // 7. Initialize WiFi — ALWAYS start at least in AP mode
    Serial.println("\n[INIT] WiFi Manager...");
    {
        WiFiConfig* wifiCfg = configManager.getWiFiConfig();

        // Force enabled + ap_fallback to guarantee web access
        wifiCfg->enabled     = true;
        wifiCfg->ap_fallback = true;
        if (wifiCfg->hostname[0] == '\0') {
            strlcpy(wifiCfg->hostname, WIFI_DEFAULT_HOSTNAME, sizeof(wifiCfg->hostname));
        }
        // Clean up old default SSID "MidiBoop" (placeholder, not a real network)
        if (strcmp(wifiCfg->ssid, "MidiBoop") == 0) {
            wifiCfg->ssid[0] = '\0';
            Serial.println("[INIT] Placeholder SSID detected — switching directly to AP");
        }

        if (wifiManager.begin(*wifiCfg)) {
            if (wifiManager.isAP()) {
                Serial.printf("[INIT] AP mode: http://%s\n",
                              wifiManager.getIP().toString().c_str());
                logger.log(LOG_INFO, CAT_SYSTEM, "AP mode: http://%s",
                           wifiManager.getIP().toString().c_str());
                ledSet(LED_AP);
            } else {
                Serial.printf("[INIT] WiFi connected: %s\n",
                              wifiManager.getIP().toString().c_str());
                logger.log(LOG_INFO, CAT_SYSTEM, "WiFi STA: %s",
                           wifiManager.getIP().toString().c_str());
                ledSet(LED_STA);
            }
        } else {
            Serial.println("[INIT] ERROR: WiFi unavailable");
            logger.log(LOG_ERROR, CAT_SYSTEM, "WiFi unavailable");
            ledSet(LED_ERROR);
        }
    }

    // 8. Configure the jitter buffer and start MIDI transports
    Serial.println("\n[INIT] MIDI Transport...");
    MidiInputConfig* midiCfg = configManager.getMidiInputConfig();
    jitterBuffer.setDepth(midiCfg->jitter_buffer_ms);
    if (!midiTransport.begin(*midiCfg)) {
        Serial.println("[INIT] WARNING: No active MIDI transport");
        logger.log(LOG_WARN, CAT_MIDI, "No active MIDI transport");
    } else {
        logger.log(LOG_INFO, CAT_MIDI, "MIDI Transport ready (jitter=%dms)",
                   midiCfg->jitter_buffer_ms);
    }

    // 9. Initialize the Power Manager (Phase 5)
    Serial.println("\n[INIT] Power Manager...");
    {
        PowerBudget budget = {};
        budget.global_max_ma         = POWER_GLOBAL_MAX_MA;
        budget.servo_bus_max_ma      = POWER_SERVO_BUS_MAX_MA;
        budget.solenoid_bus_max_ma   = POWER_SOLENOID_BUS_MAX_MA;
        budget.global_max_polyphony  = POWER_MAX_POLYPHONY;
        budget.smart_rejection       = true;
        for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
            budget.instrument_max_polyphony[i] = 4;
        }
        powerManager.begin(budget);
        logger.log(LOG_INFO, CAT_POWER, "Power Manager: max %dmA poly=%d",
                   POWER_GLOBAL_MAX_MA, POWER_MAX_POLYPHONY);
    }

    // 10. Initialize the MIDI Dispatcher with PowerManager (note/CC mapping)
    Serial.println("\n[INIT] MIDI Dispatcher...");
    midiDispatcher.setPowerManager(&powerManager);
    midiDispatcher.refreshConfig();

    // 11. Start the Web Server (Phase 6) — only if WiFi is active
    Serial.println("\n[INIT] Web Server...");
    webServer.setModules(&configManager, &scheduler, &safetyManager,
                         &powerManager, &midiDispatcher, &midiTransport,
                         &pcaDriver, &actuatorEngine);
    webServer.setCalibrator(&calibrator);
    webServer.setTestManager(&testManager);
    if (!wifiManager.isConnected() && !wifiManager.isAP()) {
        Serial.println("[INIT] Web Server skipped (WiFi not active)");
        logger.log(LOG_WARN, CAT_SYSTEM, "Web Server not started (WiFi disabled)");
    } else if (!webServer.begin()) {
        Serial.println("[INIT] ERROR: Web Server");
        logger.log(LOG_ERROR, CAT_SYSTEM, "Web Server: start failed");
    } else {
        Serial.printf("[INIT] Web UI available at http://%s:%d\n",
                      wifiManager.getIP().toString().c_str(), WEB_SERVER_PORT);
        logger.log(LOG_INFO, CAT_SYSTEM, "Web UI at http://%s:%d",
                   wifiManager.getIP().toString().c_str(), WEB_SERVER_PORT);
    }

    // 12. Initialize the Acoustic Calibrator (Phase 7)
    Serial.println("\n[INIT] Acoustic Calibrator...");
    if (!calibrator.begin()) {
        Serial.println("[INIT] WARNING: I²S microphone unavailable (calibration disabled)");
        logger.log(LOG_WARN, CAT_CAL, "I2S mic unavailable (calibration disabled)");
    } else {
        logger.log(LOG_INFO, CAT_CAL, "Acoustic calibrator ready");
    }

    Serial.println("\n========================================");
    Serial.println("  Initialization complete — Phase 9");
    Serial.printf("  Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("  Mode: %s\n", wifiManager.isAP() ? "AP (hotspot)" : "STA (WiFi)");
    Serial.println("========================================\n");

    logger.log(LOG_INFO, CAT_SYSTEM, "Init complete — free heap: %d bytes",
               ESP.getFreeHeap());

#ifdef ENABLE_TEST_HARNESS
    delay(500);
    runTestSequence();
#endif
}

// ============================================================================
// LOOP — Core 0 : Pipeline MIDI + Power Manager + Web Server
// ============================================================================
void loop() {
    // 0. LED status
    ledUpdate();

    // 1. WiFi maintenance (reconnection + captive portal DNS in AP mode)
    wifiManager.maintain();

    // 2. Read MIDI inputs (fills the jitter buffer)
    midiTransport.poll();

    // 3. Pop ready messages from the jitter buffer and dispatch them
    MidiMessage msg;
    while (jitterBuffer.pop(msg)) {
        midiDispatcher.dispatch(msg);
    }

    // 4. Periodic Power Manager update
    {
        ActuatorConfig* actuators = configManager.getActuators();
        uint8_t count = configManager.getActuatorCount();
        static ActuatorConfig* act_ptrs[MAX_ACTUATORS];
        for (uint8_t i = 0; i < count; i++) act_ptrs[i] = &actuators[i];
        powerManager.update(act_ptrs, count);
    }

    // 5. Acoustic calibrator update (Phase 7)
    calibrator.update();

    // 5b. Test manager update (Phase 8)
    testManager.update();

    // 5c. State change detection for logging (Phase 9)
    {
        static bool last_kill = false;
        static bool last_wifi = false;
        static bool last_degrad = false;

        bool cur_kill  = safetyManager.isKillSwitchActive();
        bool cur_wifi  = WiFi.isConnected();
        bool cur_degrad = safetyManager.isDegradationActive();

        if (cur_kill != last_kill) {
            logger.log(cur_kill ? LOG_CRITICAL : LOG_INFO, CAT_SAFETY,
                       cur_kill ? "Kill switch activated automatically"
                                : "Kill switch deactivated");
            last_kill = cur_kill;
        }
        if (cur_wifi != last_wifi) {
            if (cur_wifi) {
                logger.log(LOG_INFO, CAT_SYSTEM, "WiFi reconnected: %s",
                           WiFi.localIP().toString().c_str());
                ledSet(LED_STA);
            } else {
                logger.log(LOG_WARN, CAT_SYSTEM, "WiFi disconnected");
                if (wifiManager.isAP()) ledSet(LED_AP);
            }
            last_wifi = cur_wifi;
        }
        if (cur_degrad != last_degrad) {
            logger.log(cur_degrad ? LOG_WARN : LOG_INFO, CAT_SAFETY,
                       cur_degrad ? "Graceful degradation activated (current threshold)"
                                  : "Graceful degradation lifted");
            last_degrad = cur_degrad;
        }
    }

    // 6. Web Server update (WebSocket broadcast)
    webServer.update();

    // 7. Periodic status display (every 5 seconds)
    static uint32_t last_status = 0;
    uint32_t now = millis();

    if (now - last_status > 5000) {
        last_status = now;

        const PowerStats& pwr = powerManager.getStats();

        Serial.printf("[STATUS] Sched: %d queue, %d processed | "
                      "MIDI: S:%d U:%d R:%d | Disp: %d routed, %d dropped, %d pwr-rejected | "
                      "Safety: %dmA, %d active%s | "
                      "Power: %umA/%umA (%u%%) srv=%umA sol=%umA%s | "
                      "Web: %d clients | Heap: %d\n",
                      scheduler.getQueuedEventCount(),
                      scheduler.getProcessedCount(),
                      midiTransport.getSerialByteCount(),
                      midiTransport.getUdpPacketCount(),
                      midiTransport.getRtpPacketCount(),
                      midiDispatcher.getDispatchedCount(),
                      midiDispatcher.getDroppedCount(),
                      midiDispatcher.getPowerRejectedCount(),
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
                      webServer.getClientCount(),
                      ESP.getFreeHeap());
    }

    delay(1);  // Minimal yield — MIDI requires frequent reading
}

// ============================================================================
// Test Harness (conditional compilation)
// ============================================================================

#ifdef ENABLE_TEST_HARNESS

static ActuatorConfig testActuators[4];
static uint8_t testActuatorCount = 0;

void setupTestActuators() {
    Serial.println("[TEST] Creating test actuators...");

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

    for (uint8_t i = 0; i < testActuatorCount; i++) {
        actuatorEngine.initActuator(testActuators[i]);
        scheduler.registerActuator(&testActuators[i]);
    }

    Serial.printf("[TEST] %d test actuators created\n", testActuatorCount);
}

void runTestSequence() {
    Serial.println("\n[TEST] === Starting test sequence ===\n");

    uint32_t now_us = (uint32_t)esp_timer_get_time();

    {
        SchedulerEvent evt = {};
        evt.trigger_time_us = now_us + 100000;
        evt.actuator_id = 0;
        evt.action = ACTION_NOTE_ON;
        evt.velocity = 100;
        evt.priority = 0;
        if (scheduler.pushEvent(evt)) {
            Serial.println("[TEST] Servo Strike scheduled at +100ms");
        }
    }

    for (int i = 0; i < 3; i++) {
        SchedulerEvent evt = {};
        evt.trigger_time_us = now_us + 500000 + (i * 300000);
        evt.actuator_id = 1;
        evt.action = ACTION_NOTE_ON;
        evt.velocity = 80;
        evt.priority = 0;
        if (scheduler.pushEvent(evt)) {
            Serial.printf("[TEST] Servo Alternate #%d scheduled at +%dms\n", i + 1, 500 + (i * 300));
        }
    }

    {
        SchedulerEvent evt = {};
        evt.trigger_time_us = now_us + 2000000;
        evt.actuator_id = 2;
        evt.action = ACTION_NOTE_ON;
        evt.velocity = 127;
        evt.priority = 0;
        if (scheduler.pushEvent(evt)) {
            Serial.println("[TEST] Solenoid Strike scheduled at +2000ms");
        }
    }

    {
        SchedulerEvent evt_on = {};
        evt_on.trigger_time_us = now_us + 3000000;
        evt_on.actuator_id = 3;
        evt_on.action = ACTION_NOTE_ON;
        evt_on.velocity = 100;
        evt_on.priority = 0;

        SchedulerEvent evt_off = {};
        evt_off.trigger_time_us = now_us + 4000000;
        evt_off.actuator_id = 3;
        evt_off.action = ACTION_NOTE_OFF;
        evt_off.velocity = 0;
        evt_off.priority = 0;

        if (scheduler.pushEvent(evt_on) && scheduler.pushEvent(evt_off)) {
            Serial.println("[TEST] Solenoid Hit-and-Hold scheduled: ON +3000ms, OFF +4000ms");
        }
    }

    Serial.println("\n[TEST] Sequence sent to scheduler");
    Serial.println("[TEST] Events will be executed automatically\n");
}

#endif // ENABLE_TEST_HARNESS
