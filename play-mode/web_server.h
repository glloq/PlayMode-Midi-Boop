#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "config.h"
#include "types.h"

// Forward declarations
class ConfigManager;
class Scheduler;
class SafetyManager;
class PowerManager;
class MidiDispatcher;
class MidiTransport;
class PCADriver;
class ActuatorEngine;
class Calibrator;
class TestManager;

// ============================================================================
// PlayMode — Web Server (Phase 6)
// ============================================================================
//
// Embedded web server with:
//   - REST JSON API for CRUD configuration
//   - WebSocket for real-time monitoring
//   - Integrated HTML/CSS/JS interface (PROGMEM)
//
// Runs on Core 0 with the MIDI pipeline.
// Uses ESPAsyncWebServer for non-blocking handling.
//

class WebServer {
public:
    WebServer(uint16_t port = WEB_SERVER_PORT);

    // Registers references to system modules
    void setModules(ConfigManager* config, Scheduler* scheduler,
                    SafetyManager* safety, PowerManager* power,
                    MidiDispatcher* dispatcher, MidiTransport* transport,
                    PCADriver* pca, ActuatorEngine* engine);

    // Registers the calibrator (Phase 7, optional)
    void setCalibrator(Calibrator* calibrator);

    // Registers the test manager (Phase 8, optional)
    void setTestManager(TestManager* testManager);

    // Starts the HTTP + WebSocket server
    bool begin();

    // Stops the server
    void stop();

    // Periodic update — sends WebSocket stats
    // Call from loop()
    void update();

    // Checks if the server is active
    bool isRunning() const;

    // Number of connected WebSocket clients
    uint8_t getClientCount() const;

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    uint16_t _port;
    bool _running;

    // Module references (not owned)
    ConfigManager*   _config;
    Scheduler*       _scheduler;
    SafetyManager*   _safety;
    PowerManager*    _power;
    MidiDispatcher*  _dispatcher;
    MidiTransport*   _transport;
    PCADriver*       _pca;
    ActuatorEngine*  _engine;
    Calibrator*      _calibrator;
    TestManager*     _testManager;

    // Timing WebSocket broadcast
    uint32_t _last_ws_broadcast_ms;

    // -------------------------------------------------------------------------
    // Static routes (HTML pages)
    // -------------------------------------------------------------------------
    void setupStaticRoutes();

    // -------------------------------------------------------------------------
    // Routes API REST (JSON)
    // -------------------------------------------------------------------------
    void setupAPIRoutes();

    // GET — read state / config
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetConfig(AsyncWebServerRequest* request);
    void handleGetInstruments(AsyncWebServerRequest* request);
    void handleGetActuators(AsyncWebServerRequest* request);
    void handleGetBuses(AsyncWebServerRequest* request);
    void handleGetWiFi(AsyncWebServerRequest* request);
    void handleGetMidi(AsyncWebServerRequest* request);
    void handleGetRouting(AsyncWebServerRequest* request);
    void handleGetPower(AsyncWebServerRequest* request);
    void handleGetSafety(AsyncWebServerRequest* request);

    // POST — write config
    void handlePostInstrument(AsyncWebServerRequest* request,
                              uint8_t* data, size_t len);
    void handlePostActuator(AsyncWebServerRequest* request,
                            uint8_t* data, size_t len);
    void handlePostWiFi(AsyncWebServerRequest* request,
                        uint8_t* data, size_t len);
    void handlePostMidi(AsyncWebServerRequest* request,
                        uint8_t* data, size_t len);
    void handlePostRouting(AsyncWebServerRequest* request,
                           uint8_t* data, size_t len);
    void handlePostPowerBudget(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len);
    void handlePostSafety(AsyncWebServerRequest* request,
                          uint8_t* data, size_t len);

    // POST — actions
    void handlePostSave(AsyncWebServerRequest* request);
    void handlePostDefaults(AsyncWebServerRequest* request);
    void handlePostTestActuator(AsyncWebServerRequest* request,
                                uint8_t* data, size_t len);
    void handlePostKillSwitch(AsyncWebServerRequest* request,
                              uint8_t* data, size_t len);
    void handlePostScanI2C(AsyncWebServerRequest* request);

    // --- Acoustic calibration (Phase 7) ---
    void handleGetCalibrateStatus(AsyncWebServerRequest* request);
    void handleGetCalibrateResults(AsyncWebServerRequest* request);
    void handlePostCalibrate(AsyncWebServerRequest* request,
                             uint8_t* data, size_t len);
    void handlePostCalibrateApply(AsyncWebServerRequest* request);
    void handlePostCalibrateStop(AsyncWebServerRequest* request);

    // --- Industrial Test Manager (Phase 8) ---
    void handleGetTestStatus(AsyncWebServerRequest* request);
    void handleGetTestLog(AsyncWebServerRequest* request);
    void handlePostTestSweep(AsyncWebServerRequest* request,
                             uint8_t* data, size_t len);
    void handlePostTestBurst(AsyncWebServerRequest* request,
                             uint8_t* data, size_t len);
    void handlePostTestStress(AsyncWebServerRequest* request,
                              uint8_t* data, size_t len);
    void handlePostTestStop(AsyncWebServerRequest* request);
    void handlePostTestClearLog(AsyncWebServerRequest* request);

    // --- Log Manager (Phase 9) ---
    void handleGetLogs(AsyncWebServerRequest* request);
    void handlePostLogsClear(AsyncWebServerRequest* request);

    // DELETE — deletion
    void handleDeleteInstrument(AsyncWebServerRequest* request);
    void handleDeleteActuator(AsyncWebServerRequest* request);

    // -------------------------------------------------------------------------
    // WebSocket
    // -------------------------------------------------------------------------
    void setupWebSocket();
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
    void broadcastStatus();

    // -------------------------------------------------------------------------
    // Helpers JSON
    // -------------------------------------------------------------------------
    void buildStatusJSON(String& output);
    void buildActuatorJSON(const ActuatorConfig& act, String& output);
    void buildInstrumentJSON(const InstrumentConfig& inst, String& output);
    void buildBusJSON(const BusConfig& bus, String& output);
};

#endif // WEB_SERVER_H
