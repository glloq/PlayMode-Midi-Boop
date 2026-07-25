#include "web_server.h"
#include "web_ui.h"
#include "config_manager.h"
#include <WiFi.h>
#include "scheduler.h"
#include "resource_manager.h"
#include "midi_dispatcher.h"
#include "midi_transport.h"
#include "pca_driver.h"
#include "actuator_engine.h"
#include "calibrator.h"
#include "test_manager.h"
#include "log_manager.h"
#include <ArduinoJson.h>

// ============================================================================
// PlayMode — Web Server Implementation (Phase 6)
// ============================================================================

// ============================================================================
// AUDIT FIX: AP-mode authentication
// ============================================================================
// In Access Point mode anyone within radio range can reach the API. We
// derive a short token from the AP MAC (deterministic per device) that
// the UI fetches once via /api/auth-token. Sensitive POST endpoints
// (kill switch, factory reset, save, calibration, test, config writes)
// require it via the X-PlayMode-Token header or a `token` query param.
// In STA mode the token check is skipped (the local LAN is the trust
// boundary).
namespace {
    String s_ap_token;

    void ensureApToken() {
        if (s_ap_token.length() > 0) return;
        uint8_t mac[6];
        WiFi.softAPmacAddress(mac);
        char buf[17];
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3],
                 (uint8_t)(mac[0] ^ 0xA5), (uint8_t)(mac[2] ^ 0x5A),
                 (uint8_t)(mac[4] ^ 0xF0), (uint8_t)(mac[5] ^ 0x0F));
        s_ap_token = String(buf);
    }

    // True when auth is not required (STA / non-AP) — the local LAN is the
    // trust boundary. In pure AP mode the token is required.
    bool authExempt() {
        return !(WiFi.getMode() & WIFI_AP) || (WiFi.getMode() & WIFI_STA);
    }

    // AUDIT FIX (P0.9): validate a token string carried by a WebSocket command
    // (WS has no HTTP headers to run requireAuth against).
    bool wsCommandAuthorized(const String& token) {
        if (authExempt()) return true;
        ensureApToken();
        return token == s_ap_token;
    }

    bool requireAuth(AsyncWebServerRequest* req) {
        // Auth only enforced in AP mode — STA networks are user-trusted.
        if (authExempt()) {
            return true;
        }
        ensureApToken();
        if (req->hasHeader(WEB_AUTH_HEADER)) {
            if (req->header(WEB_AUTH_HEADER) == s_ap_token) return true;
        }
        if (req->hasParam(WEB_AUTH_PARAM)) {
            if (req->getParam(WEB_AUTH_PARAM)->value() == s_ap_token) return true;
        }
        req->send(401, "application/json", "{\"error\":\"auth required (AP mode)\"}");
        return false;
    }

    // AUDIT FIX (P0.3): the web UI uses the musician convention 0 = Omni,
    // 1..16 = channel. Convert to the internal representation (0..15, or the
    // Omni sentinel) on write, and back on read.
    uint8_t uiChannelToInternal(int ui_channel) {
        if (ui_channel <= 0)  return MIDI_CHANNEL_OMNI_INTERNAL;   // 0 = Omni
        if (ui_channel > 16)  ui_channel = 16;
        return (uint8_t)(ui_channel - 1);                          // 1..16 -> 0..15
    }

    int internalChannelToUi(uint8_t internal_channel) {
        if (internal_channel == MIDI_CHANNEL_OMNI_INTERNAL) return 0;
        if (internal_channel >= MIDI_CHANNEL_COUNT) return 0;      // defensive
        return internal_channel + 1;                               // 0..15 -> 1..16
    }

    // Standard security headers — added to every HTML response.
    AsyncWebServerResponse* securedHtml(AsyncWebServerRequest* req,
                                       const uint8_t* body, size_t len) {
        AsyncWebServerResponse* resp = req->beginResponse_P(
            200, "text/html; charset=UTF-8", body, len);
        resp->addHeader("X-Frame-Options", "DENY");
        resp->addHeader("X-Content-Type-Options", "nosniff");
        resp->addHeader("Referrer-Policy", "no-referrer");
        // CSP allows the embedded inline script/style (the SPA is a single
        // self-contained HTML blob); blocks external loads.
        resp->addHeader("Content-Security-Policy",
                        "default-src 'self'; "
                        "script-src 'self' 'unsafe-inline'; "
                        "style-src 'self' 'unsafe-inline'; "
                        "connect-src 'self' ws: wss:; "
                        "img-src 'self' data:; "
                        "frame-ancestors 'none'");
        return resp;
    }
}

WebServer::WebServer(uint16_t port)
    : _server(port)
    , _ws("/ws")
    , _port(port)
    , _running(false)
    , _config(nullptr)
    , _scheduler(nullptr)
    , _resources(nullptr)
    , _dispatcher(nullptr)
    , _transport(nullptr)
    , _pca(nullptr)
    , _engine(nullptr)
    , _calibrator(nullptr)
    , _testManager(nullptr)
    , _last_ws_broadcast_ms(0)
    , _restart_at_ms(0)
{
}

void WebServer::setModules(ConfigManager* config, Scheduler* scheduler,
                           ResourceManager* resources,
                           MidiDispatcher* dispatcher, MidiTransport* transport,
                           PCADriver* pca, ActuatorEngine* engine)
{
    _config     = config;
    _scheduler  = scheduler;
    _resources  = resources;
    _dispatcher = dispatcher;
    _transport  = transport;
    _pca        = pca;
    _engine     = engine;
}

void WebServer::setCalibrator(Calibrator* calibrator) {
    _calibrator = calibrator;
}

void WebServer::setTestManager(TestManager* testManager) {
    _testManager = testManager;
}

bool WebServer::begin() {
    if (_running) return true;

    // AUDIT FIX: ESPAsyncWebServer drops unknown request headers by
    // default — register the auth header so requireAuth() can read it.
    _server.collectHeader(WEB_AUTH_HEADER);

    setupStaticRoutes();
    setupAPIRoutes();
    setupWebSocket();

    _server.begin();
    _running = true;

    Serial.printf("[WEB] Server started on port %d\n", _port);
    return true;
}

void WebServer::stop() {
    if (!_running) return;
    _ws.closeAll();
    _running = false;
    Serial.println("[WEB] Server stopped");
}

void WebServer::update() {
    if (!_running) return;

    // AUDIT FIX (P0.4): perform a pending factory-reset restart once the
    // response has had time to flush.
    if (_restart_at_ms != 0 && (int32_t)(millis() - _restart_at_ms) >= 0) {
        Serial.println("[WEB] Restarting after factory reset...");
        ESP.restart();
    }

    _ws.cleanupClients();

    uint32_t now = millis();
    if (now - _last_ws_broadcast_ms >= WEB_WS_BROADCAST_MS) {
        _last_ws_broadcast_ms = now;
        if (_ws.count() > 0) {
            broadcastStatus();
        }
    }
}

bool WebServer::isRunning() const { return _running; }

uint8_t WebServer::getClientCount() const { return _ws.count(); }

// ============================================================================
// Static routes — HTML pages
// ============================================================================

void WebServer::setupStaticRoutes() {
    // Main page (SPA) — explicit size to guarantee correct Content-Length
    // AUDIT FIX: ship standard security headers (CSP, X-Frame-Options, etc.).
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(securedHtml(request,
                                  reinterpret_cast<const uint8_t*>(WEB_UI_HTML),
                                  sizeof(WEB_UI_HTML) - 1));
    });

    // Expose the auth token to clients that can already reach this device
    // on the LAN/AP — only useful in AP mode. STA mode returns "ok".
    _server.on("/api/auth-token", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        if (WiFi.getMode() & WIFI_AP) {
            ensureApToken();
            doc["ap_mode"] = true;
            doc["token"]   = s_ap_token;
        } else {
            doc["ap_mode"] = false;
        }
        String out; serializeJson(doc, out);
        request->send(200, "application/json; charset=UTF-8", out);
    });

    // Favicon (204 No Content to avoid 404 errors)
    _server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });

    // --- Captive portal probes ---
    // Android: returns 204 → OS detects portal and shows notification
    // "Sign in to network" → user opens full browser.
    _server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    _server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    // AUDIT FIX: only redirect to the AP IP when WiFi is actually up.
    // Otherwise `WiFi.softAPIP()` returns 0.0.0.0 and we'd ship a broken URL.
    auto captiveTarget = []() -> String {
        IPAddress ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
        if ((uint32_t)ip == 0) return String("/");
        return String("http://") + ip.toString() + "/";
    };
    // iOS / macOS
    _server.on("/hotspot-detect.html", HTTP_GET, [captiveTarget](AsyncWebServerRequest* request) {
        request->redirect(captiveTarget());
    });
    // Windows NCSI
    _server.on("/ncsi.txt", HTTP_GET, [captiveTarget](AsyncWebServerRequest* request) {
        request->redirect(captiveTarget());
    });
    _server.on("/connecttest.txt", HTTP_GET, [captiveTarget](AsyncWebServerRequest* request) {
        request->redirect(captiveTarget());
    });

    // Fallback: unknown API calls → 404 JSON; everything else → main page
    _server.onNotFound([captiveTarget](AsyncWebServerRequest* request) {
        if (request->url().startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }
        request->redirect(captiveTarget());
    });
}

// ============================================================================
// Routes API REST
// ============================================================================

void WebServer::setupAPIRoutes() {
    // --- GET endpoints ---

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetStatus(req);
    });

    _server.on("/api/instruments", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetInstruments(req);
    });

    _server.on("/api/actuators", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetActuators(req);
    });

    _server.on("/api/buses", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetBuses(req);
    });

    _server.on("/api/wifi", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetWiFi(req);
    });

    _server.on("/api/midi", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetMidi(req);
    });

    _server.on("/api/routing", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetRouting(req);
    });

    _server.on("/api/power", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetPower(req);
    });

    _server.on("/api/safety", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetSafety(req);
    });

    // --- POST endpoints (with JSON body) ---

    // Save config to flash
    _server.on("/api/config/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostSave(req);
    });

    // Reset config to defaults
    _server.on("/api/config/defaults", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostDefaults(req);
    });

    // Export configuration (download / backup)
    _server.on("/api/config/export", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetConfigExport(req);
    });

    // Import configuration (restore) — larger body allowed for a full config.
    auto* importHandler = new AsyncCallbackJsonWebHandler("/api/config/import",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostConfigImport(req, (uint8_t*)body.c_str(), body.length());
        });
    importHandler->setMethod(HTTP_POST);
    _server.addHandler(importHandler);

    // Scan I²C
    _server.on("/api/scan/i2c", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostScanI2C(req);
    });

    // Instrument CRUD
    auto* instrHandler = new AsyncCallbackJsonWebHandler("/api/instrument",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostInstrument(req, (uint8_t*)body.c_str(), body.length());
        });
    instrHandler->setMethod(HTTP_POST | HTTP_PUT);
    _server.addHandler(instrHandler);

    // Transactional wizard endpoint (validate-all-then-apply)
    auto* setupHandler = new AsyncCallbackJsonWebHandler("/api/setup/instrument",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostSetupInstrument(req, (uint8_t*)body.c_str(), body.length());
        });
    setupHandler->setMethod(HTTP_POST);
    _server.addHandler(setupHandler);

    _server.on("/api/instrument", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
        handleDeleteInstrument(req);
    });

    // Actuator CRUD
    auto* actHandler = new AsyncCallbackJsonWebHandler("/api/actuator",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostActuator(req, (uint8_t*)body.c_str(), body.length());
        });
    actHandler->setMethod(HTTP_POST | HTTP_PUT);
    _server.addHandler(actHandler);

    _server.on("/api/actuator", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
        handleDeleteActuator(req);
    });

    // WiFi config
    auto* wifiHandler = new AsyncCallbackJsonWebHandler("/api/wifi",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostWiFi(req, (uint8_t*)body.c_str(), body.length());
        });
    wifiHandler->setMethod(HTTP_POST);
    _server.addHandler(wifiHandler);

    // MIDI config
    auto* midiHandler = new AsyncCallbackJsonWebHandler("/api/midi",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostMidi(req, (uint8_t*)body.c_str(), body.length());
        });
    midiHandler->setMethod(HTTP_POST);
    _server.addHandler(midiHandler);

    // MIDI routing
    auto* routingHandler = new AsyncCallbackJsonWebHandler("/api/routing",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostRouting(req, (uint8_t*)body.c_str(), body.length());
        });
    routingHandler->setMethod(HTTP_POST);
    _server.addHandler(routingHandler);

    // Power budget
    auto* powerHandler = new AsyncCallbackJsonWebHandler("/api/power/budget",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostPowerBudget(req, (uint8_t*)body.c_str(), body.length());
        });
    powerHandler->setMethod(HTTP_POST);
    _server.addHandler(powerHandler);

    // Safety config
    auto* safetyHandler = new AsyncCallbackJsonWebHandler("/api/safety",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostSafety(req, (uint8_t*)body.c_str(), body.length());
        });
    safetyHandler->setMethod(HTTP_POST);
    _server.addHandler(safetyHandler);

    // Test actuator
    auto* testHandler = new AsyncCallbackJsonWebHandler("/api/test/actuator",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostTestActuator(req, (uint8_t*)body.c_str(), body.length());
        });
    testHandler->setMethod(HTTP_POST);
    _server.addHandler(testHandler);

    // Kill switch
    auto* killHandler = new AsyncCallbackJsonWebHandler("/api/killswitch",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostKillSwitch(req, (uint8_t*)body.c_str(), body.length());
        });
    killHandler->setMethod(HTTP_POST);
    _server.addHandler(killHandler);

    // AUDIT FIX (P0.1): acknowledge a latched fault (separate from re-arm).
    _server.on("/api/fault/ack", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (!requireAuth(req)) return;
        if (!_resources) { req->send(500); return; }
        _resources->requestAcknowledge();
        logger.log(LOG_WARN, CAT_SAFETY, "Fault acknowledge requested from web UI");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // AUDIT FIX: Bus PWM frequency (missing endpoint — called by the frontend)
    auto* busPwmHandler = new AsyncCallbackJsonWebHandler("/api/bus/pwm",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            if (!requireAuth(req)) return;
            if (!_resources || !_config) { req->send(500); return; }
            uint8_t bus_id = json["bus_id"] | 0xFF;
            uint16_t freq  = json["freq_pwm"] | 0;
            if (bus_id > 1 || freq < 24) {
                req->send(400, "application/json",
                          "{\"error\":\"invalid bus_id or freq_pwm (>= 24 Hz)\"}");
                return;
            }
            // AUDIT FIX (P0.3): defer the prescaler write to Core 1 (the sole
            // I²C/PCA owner) instead of writing from the web task.
            _resources->requestBusFrequency(bus_id, freq);
            // Persist the requested value.
            _config->getBuses()[bus_id].pwm_frequency = freq;
            req->send(200, "application/json", "{\"ok\":true}");
        });
    busPwmHandler->setMethod(HTTP_POST);
    _server.addHandler(busPwmHandler);

    // --- Acoustic calibration (Phase 7) ---
    _server.on("/api/calibrate/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetCalibrateStatus(req);
    });
    _server.on("/api/calibrate/results", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetCalibrateResults(req);
    });

    auto* calHandler = new AsyncCallbackJsonWebHandler("/api/calibrate",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostCalibrate(req, (uint8_t*)body.c_str(), body.length());
        });
    calHandler->setMethod(HTTP_POST);
    _server.addHandler(calHandler);

    _server.on("/api/calibrate/apply", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostCalibrateApply(req);
    });
    _server.on("/api/calibrate/stop", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostCalibrateStop(req);
    });

    // --- Industrial Test Manager (Phase 8) ---
    _server.on("/api/test/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetTestStatus(req);
    });
    _server.on("/api/test/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetTestLog(req);
    });

    auto* sweepHandler = new AsyncCallbackJsonWebHandler("/api/test/sweep",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body; serializeJson(json, body);
            handlePostTestSweep(req, (uint8_t*)body.c_str(), body.length());
        });
    sweepHandler->setMethod(HTTP_POST);
    _server.addHandler(sweepHandler);

    auto* burstHandler = new AsyncCallbackJsonWebHandler("/api/test/burst",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body; serializeJson(json, body);
            handlePostTestBurst(req, (uint8_t*)body.c_str(), body.length());
        });
    burstHandler->setMethod(HTTP_POST);
    _server.addHandler(burstHandler);

    auto* stressHandler = new AsyncCallbackJsonWebHandler("/api/test/stress",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            String body; serializeJson(json, body);
            handlePostTestStress(req, (uint8_t*)body.c_str(), body.length());
        });
    stressHandler->setMethod(HTTP_POST);
    _server.addHandler(stressHandler);

    _server.on("/api/test/stop", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostTestStop(req);
    });
    _server.on("/api/test/log/clear", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostTestClearLog(req);
    });

    // --- Log Manager (Phase 9) ---
    _server.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetLogs(req);
    });
    _server.on("/api/logs/clear", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostLogsClear(req);
    });
}

// ============================================================================
// GET handlers
// ============================================================================

void WebServer::handleGetStatus(AsyncWebServerRequest* request) {
    String json;
    buildStatusJSON(json);
    request->send(200, "application/json", json);
}

void WebServer::handleGetInstruments(AsyncWebServerRequest* request) {
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    InstrumentConfig* instruments = _config->getInstruments();
    uint8_t count = _config->getInstrumentCount();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["index"]   = i;
        obj["name"]    = instruments[i].name;
        obj["channel"] = internalChannelToUi(instruments[i].midi_channel);
        obj["bus_id"]  = instruments[i].bus_id;
        obj["actuator_count"] = instruments[i].actuator_count;
        obj["latency_ms"]     = instruments[i].default_latency_ms;
        obj["auto_cal"]       = instruments[i].auto_calibration;
        obj["enabled"]        = instruments[i].enabled;

        JsonArray acts = obj["actuators"].to<JsonArray>();
        for (uint8_t j = 0; j < instruments[i].actuator_count; j++) {
            JsonObject a = acts.add<JsonObject>();
            a["id"]   = instruments[i].actuator_ids[j];
            a["note"] = instruments[i].midi_notes[j];
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetActuators(AsyncWebServerRequest* request) {
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    ActuatorConfig* actuators = _config->getActuators();
    uint8_t count = _config->getActuatorCount();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"]       = actuators[i].id;
        obj["type"]     = actuators[i].type;
        obj["bus_id"]   = actuators[i].bus_id;
        obj["pca_addr"] = actuators[i].pca_address;
        obj["pca_ch"]   = actuators[i].pca_channel;
        obj["behavior"] = actuators[i].behavior;
        obj["enabled"]  = actuators[i].enabled;
        obj["latency_ms"] = actuators[i].latency_ms;

        // Servo parameters
        if (actuators[i].type == ACT_SERVO) {
            obj["angle_init"]   = actuators[i].angle_initial;
            obj["amplitude"]    = actuators[i].amplitude;
            obj["speed_ms"]     = actuators[i].speed_ms;
            obj["angle_b"]      = actuators[i].angle_b;
            obj["hit_reverse"]  = actuators[i].hit_reverse;
        }
        // Solenoid parameters
        else {
            obj["pulse_min_ms"] = actuators[i].pulse_min_ms;
            obj["pulse_ms"]    = actuators[i].pulse_ms;
            obj["pwm_initial"] = actuators[i].pwm_initial;
            obj["pwm_hold"]    = actuators[i].pwm_hold;
            obj["ramp_ms"]     = actuators[i].ramp_ms;
        }

        // Runtime state
        JsonObject state = obj["state"].to<JsonObject>();
        state["active"]   = actuators[i].state.active;
        state["position"] = actuators[i].state.current_position;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetBuses(AsyncWebServerRequest* request) {
    if (!_pca) { request->send(500); return; }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t b = 0; b < 2; b++) {
        BusConfig& bus = _pca->getBusConfig(b);
        JsonObject obj = arr.add<JsonObject>();
        obj["id"]        = bus.id;
        obj["sda"]       = bus.sda_pin;
        obj["scl"]       = bus.scl_pin;
        obj["oe"]        = bus.oe_pin;
        obj["freq_i2c"]  = bus.i2c_frequency;
        obj["freq_pwm"]  = bus.pwm_frequency;
        obj["enabled"]   = bus.enabled;
        obj["pca_count"] = bus.pca_count;

        JsonArray addrs = obj["pca_addrs"].to<JsonArray>();
        for (uint8_t p = 0; p < bus.pca_count; p++) {
            addrs.add(bus.pca_addresses[p]);
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetWiFi(AsyncWebServerRequest* request) {
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    WiFiConfig* wifi = _config->getWiFiConfig();
    doc["ssid"]        = wifi->ssid;
    doc["hostname"]    = wifi->hostname;
    doc["enabled"]     = wifi->enabled;
    doc["ap_fallback"] = wifi->ap_fallback;
    doc["connected"]   = (WiFi.status() == WL_CONNECTED);
    doc["ip"]          = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                                          : WiFi.softAPIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    // Do not expose the password

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetMidi(AsyncWebServerRequest* request) {
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    MidiInputConfig* midi = _config->getMidiInputConfig();
    doc["serial_enabled"]   = midi->serial_enabled;
    doc["udp_enabled"]      = midi->udp_enabled;
    doc["rtp_enabled"]      = midi->rtp_enabled;
    doc["udp_port"]         = midi->udp_port;
    doc["rtp_port"]         = midi->rtp_port;
    doc["jitter_buffer_ms"] = midi->jitter_buffer_ms;
    doc["serial_rx_pin"]    = midi->serial_rx_pin;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetRouting(AsyncWebServerRequest* request) {
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    MidiRoutingConfig* routings = _config->getRoutingConfigs();
    uint8_t count = _config->getRoutingCount();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["instrument"] = routings[i].instrument_index;

        JsonArray notes = obj["notes"].to<JsonArray>();
        for (uint8_t n = 0; n < routings[i].note_map_count; n++) {
            JsonObject nm = notes.add<JsonObject>();
            nm["note"]     = routings[i].note_map[n].midi_note;
            nm["actuator"] = routings[i].note_map[n].actuator_id;
            nm["enabled"]  = routings[i].note_map[n].enabled;
        }

        JsonArray ccs = obj["ccs"].to<JsonArray>();
        for (uint8_t c = 0; c < routings[i].cc_map_count; c++) {
            JsonObject cm = ccs.add<JsonObject>();
            cm["cc"]       = routings[i].cc_map[c].cc_number;
            cm["actuator"] = routings[i].cc_map[c].actuator_id;
            cm["target"]   = routings[i].cc_map[c].target;
            cm["min"]      = routings[i].cc_map[c].range_min;
            cm["max"]      = routings[i].cc_map[c].range_max;
            cm["enabled"]  = routings[i].cc_map[c].enabled;
        }

        JsonArray vel = obj["velocity_curve"].to<JsonArray>();
        for (uint8_t v = 0; v < routings[i].velocity_curve_count; v++) {
            JsonObject vp = vel.add<JsonObject>();
            vp["in"]  = routings[i].velocity_curve[v].input;
            vp["out"] = routings[i].velocity_curve[v].output;
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetPower(AsyncWebServerRequest* request) {
    if (!_resources) { request->send(500); return; }

    JsonDocument doc;
    const PowerStats& stats = _resources->getStats();
    const PowerBudget& budget = _resources->getBudget();

    JsonObject s = doc["stats"].to<JsonObject>();
    s["total_ma"]      = stats.total_estimated_ma;
    s["servo_bus_ma"]  = stats.servo_bus_ma;
    s["sol_bus_ma"]    = stats.solenoid_bus_ma;
    s["active_count"]  = stats.global_active_count;
    s["rejected"]      = stats.total_rejected;
    s["budget_pct"]    = stats.budget_used_percent;
    s["degradation"]   = stats.degradation_active;
    s["exceeded"]      = stats.budget_exceeded;

    JsonObject b = doc["budget"].to<JsonObject>();
    b["global_max_ma"] = budget.global_max_ma;
    b["servo_max_ma"]  = budget.servo_bus_max_ma;
    b["sol_max_ma"]    = budget.solenoid_bus_max_ma;
    b["max_polyphony"] = budget.global_max_polyphony;
    b["smart_reject"]  = budget.smart_rejection;

    JsonArray poly = b["inst_polyphony"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        poly.add(budget.instrument_max_polyphony[i]);
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetSafety(AsyncWebServerRequest* request) {
    if (!_resources) { request->send(500); return; }

    JsonDocument doc;
    const SafetyState& state = _resources->getGlobalState();

    doc["total_current_ma"]  = state.total_estimated_current_ma;
    doc["active_count"]      = state.active_actuator_count;
    doc["kill_switch"]       = state.kill_switch_active;
    doc["degradation"]       = state.degradation_active;
    doc["over_current"]      = state.over_current;

    // AUDIT FIX (UI-P1): report the RUNTIME limits actually in effect (not the
    // compile-time constants), so the form reflects what the user set.
    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["max_duty_pct"]    = _resources->getMaxDutyCycle();
    cfg["max_freq_hz"]     = _resources->getMaxFrequency();
    cfg["watchdog_ms"]     = _resources->getWatchdogTimeout();
    cfg["max_polyphony"]   = _resources->getMaxPolyphony();
    cfg["max_current_ma"]  = _resources->getMaxTotalCurrent();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ============================================================================
// POST handlers — Configuration write
// ============================================================================

void WebServer::handlePostInstrument(AsyncWebServerRequest* request,
                                     uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    InstrumentConfig inst = {};
    strlcpy(inst.name, doc["name"] | "Instrument", sizeof(inst.name));
    inst.midi_channel       = uiChannelToInternal(doc["channel"] | 0);
    inst.bus_id             = doc["bus_id"] | 0;
    inst.default_latency_ms = doc["latency_ms"] | 10;
    inst.auto_calibration   = doc["auto_cal"] | false;
    inst.enabled            = doc["enabled"] | true;
    inst.actuator_count     = 0;

    // Actuator→note mappings
    JsonArray acts = doc["actuators"];
    if (!acts.isNull()) {
        for (JsonObject a : acts) {
            if (inst.actuator_count >= MAX_ACTUATORS_PER_INSTRUMENT) break;
            inst.actuator_ids[inst.actuator_count] = a["id"] | 0;
            inst.midi_notes[inst.actuator_count]   = a["note"] | MIDI_NOTE_UNMAPPED;
            inst.actuator_count++;
        }
    }

    // If an index is provided → update existing instrument
    if (!doc["index"].isNull()) {
        uint8_t idx = (uint8_t)(doc["index"].as<int>());
        InstrumentConfig* instruments = _config->getInstruments();
        uint8_t count = _config->getInstrumentCount();
        if (idx < count) {
            // Preserve actuator/note associations if not provided
            if (acts.isNull()) {
                inst.actuator_count = instruments[idx].actuator_count;
                memcpy(inst.actuator_ids, instruments[idx].actuator_ids, sizeof(inst.actuator_ids));
                memcpy(inst.midi_notes, instruments[idx].midi_notes, sizeof(inst.midi_notes));
            }
            instruments[idx] = inst;
            if (_dispatcher) _dispatcher->refreshConfig();
            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
    }

    if (_config->addInstrument(inst)) {
        // Automatically create an empty routing entry for this new instrument
        // (routing_count must always remain == instrument_count)
        uint8_t newIdx = _config->getInstrumentCount() - 1;
        MidiRoutingConfig emptyRouting = {};
        emptyRouting.instrument_index = newIdx;
        _config->addRoutingConfig(emptyRouting);
        if (_dispatcher) _dispatcher->refreshConfig();
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"max instruments reached\"}");
    }
}

// ============================================================================
// AUDIT FIX (UI-P0): transactional wizard — validate the WHOLE request first
// (no mutation), then create the instrument + actuators + routing atomically,
// save, and roll back everything if the save fails. IDs are auto-assigned to
// the first free slots.
// ============================================================================
void WebServer::handlePostSetupInstrument(AsyncWebServerRequest* request,
                                          uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    JsonArray acts = doc["actuators"].as<JsonArray>();
    size_t nsz = acts.isNull() ? 0 : acts.size();

    // ---- Phase 1: validate everything, mutating nothing ----
    if (_config->getInstrumentCount() >= MAX_INSTRUMENTS) {
        request->send(400, "application/json", "{\"error\":\"max instruments reached\"}");
        return;
    }
    // Bound-check the count BEFORE truncating to uint8_t (avoids a wrap that
    // could pass validation yet iterate past the fixed staging arrays).
    if (nsz == 0 || nsz > MAX_ACTUATORS_PER_INSTRUMENT) {
        request->send(400, "application/json", "{\"error\":\"1.." "64" " actuators required\"}");
        return;
    }
    uint8_t n = (uint8_t)nsz;
    ActuatorConfig* existing = _config->getActuators();
    uint8_t ecount = _config->getActuatorCount();
    if ((uint16_t)(ecount + n) > MAX_ACTUATORS) {
        request->send(400, "application/json", "{\"error\":\"not enough free actuator slots\"}");
        return;
    }

    // Collect free IDs.
    bool used_id[MAX_ACTUATORS] = {false};
    for (uint8_t i = 0; i < ecount; i++)
        if (existing[i].id < MAX_ACTUATORS) used_id[existing[i].id] = true;
    uint8_t free_ids[MAX_ACTUATORS_PER_INSTRUMENT];
    uint8_t nid = 0;
    for (uint16_t id = 0; id < MAX_ACTUATORS && nid < n; id++)
        if (!used_id[id]) free_ids[nid++] = (uint8_t)id;
    if (nid < n) {
        request->send(400, "application/json", "{\"error\":\"not enough free actuator IDs\"}");
        return;
    }

    // Validate each actuator + reject duplicate output targets (new set and existing).
    struct Target { uint8_t bus, addr, ch; };
    Target targets[MAX_ACTUATORS_PER_INSTRUMENT];
    uint8_t k = 0;
    for (JsonObject a : acts) {
        int type = a["type"] | -1;
        if (type != ACT_SERVO && type != ACT_SOLENOID) {
            request->send(400, "application/json", "{\"error\":\"invalid actuator type\"}");
            return;
        }
        uint8_t bus  = a["bus_id"]  | 0;
        uint8_t addr = a["pca_addr"] | PCA_BASE_ADDRESS;
        uint8_t ch   = a["pca_ch"]  | 0;
        if (bus > 1 || addr < PCA_BASE_ADDRESS ||
            addr >= PCA_BASE_ADDRESS + PCA_MAX_PER_BUS || ch >= PCA_CHANNELS) {
            request->send(400, "application/json", "{\"error\":\"invalid bus/address/channel\"}");
            return;
        }
        // duplicate within the new set
        for (uint8_t j = 0; j < k; j++)
            if (targets[j].bus == bus && targets[j].addr == addr && targets[j].ch == ch) {
                request->send(400, "application/json", "{\"error\":\"duplicate output in request\"}");
                return;
            }
        // duplicate vs existing
        for (uint8_t j = 0; j < ecount; j++)
            if (existing[j].bus_id == bus && existing[j].pca_address == addr &&
                existing[j].pca_channel == ch) {
                request->send(400, "application/json", "{\"error\":\"output already in use\"}");
                return;
            }
        targets[k++] = { bus, addr, ch };
    }

    // ---- Phase 2: apply the array mutations under the actuator lock, but
    // release it BEFORE the flash write (AUDIT FIX P1.7: never hold the
    // real-time mutex across a LittleFS write). Web callbacks are serialised on
    // the async task, so no other web edit can interleave here.
    {
    ActuatorLockGuard guard(*_config);
    if (!guard.locked()) {
        request->send(503, "application/json", "{\"error\":\"configuration busy\"}");
        return;
    }

    // Build the instrument.
    InstrumentConfig inst = {};
    strlcpy(inst.name, doc["name"] | "Instrument", sizeof(inst.name));
    inst.midi_channel       = uiChannelToInternal(doc["channel"] | 0);
    inst.bus_id             = doc["bus_id"] | 0;
    inst.default_latency_ms = doc["latency_ms"] | 10;
    inst.auto_calibration   = doc["auto_cal"] | false;
    inst.enabled            = doc["enabled"] | true;
    inst.actuator_count     = 0;

    if (!_config->addInstrument(inst)) {
        request->send(500, "application/json", "{\"error\":\"instrument add failed\"}");
        return;
    }
    uint8_t inst_idx = _config->getInstrumentCount() - 1;

    // Create actuators with the assigned free IDs.
    k = 0;
    MidiRoutingConfig routing = {};
    routing.instrument_index = inst_idx;
    for (JsonObject a : acts) {
        ActuatorConfig act = {};
        act.id          = free_ids[k];
        act.type        = (ActuatorType)(uint8_t)(a["type"] | 0);
        act.bus_id      = targets[k].bus;
        act.pca_address = targets[k].addr;
        act.pca_channel = targets[k].ch;
        act.behavior    = a["behavior"] | 0;
        act.enabled     = a["enabled"] | true;
        act.latency_ms  = a["latency_ms"] | 10;
        act.angle_initial = a["angle_init"] | 90;
        act.amplitude     = a["amplitude"]  | 45;
        act.speed_ms      = a["speed_ms"]   | 150;
        act.angle_b       = a["angle_b"]    | 120;
        act.hit_reverse   = a["hit_reverse"] | false;
        act.pulse_min_ms  = a["pulse_min_ms"] | SOLENOID_MIN_PULSE_MS;
        act.pulse_ms      = a["pulse_ms"]     | 20;
        act.pwm_initial   = a["pwm_initial"]  | 4095;
        act.pwm_hold      = a["pwm_hold"]     | 2048;
        act.ramp_ms       = a["ramp_ms"]      | 50;
        if (act.speed_ms == 0) act.speed_ms = 1;
        _config->addActuator(act);
        ActuatorConfig* stored = _config->getActuators();
        for (uint8_t i = 0; i < _config->getActuatorCount(); i++) {
            if (stored[i].id == act.id) { if (_engine) _engine->initActuator(stored[i]); break; }
        }
        // Note mapping for this actuator.
        int note = a["note"] | -1;
        if (note >= 0 && note <= 127 && routing.note_map_count < MAX_NOTE_MAPPINGS) {
            NoteMapping& m = routing.note_map[routing.note_map_count++];
            m.midi_note = (uint8_t)note;
            m.actuator_id = act.id;
            m.behavior_override = 0xFF;
            m.enabled = true;
        }
        k++;
    }

    _config->addRoutingConfig(routing);
    _config->rebuildInstrumentFromRouting(inst_idx);
    if (_scheduler) _scheduler->syncActuators(_config->getActuators(),
                                              _config->getActuatorCount());
    if (_dispatcher) _dispatcher->refreshConfig();
    }  // actuator lock released here — flash write happens unlocked

    // Atomic save (outside the RT mutex). On failure, roll back under the lock.
    if (!_config->save()) {
        ActuatorLockGuard rb(*_config);
        for (uint8_t i = 0; i < n; i++) _config->removeActuator(free_ids[i]);
        _config->removeInstrument(inst_idx);
        if (_scheduler) _scheduler->syncActuators(_config->getActuators(),
                                                  _config->getActuatorCount());
        if (_dispatcher) _dispatcher->refreshConfig();
        request->send(500, "application/json", "{\"error\":\"save failed, rolled back\"}");
        return;
    }

    JsonDocument resp;
    resp["ok"] = true;
    resp["instrument"] = inst_idx;
    JsonArray ids = resp["actuator_ids"].to<JsonArray>();
    for (uint8_t i = 0; i < n; i++) ids.add(free_ids[i]);
    String out; serializeJson(resp, out);
    request->send(200, "application/json", out);
}

void WebServer::handlePostActuator(AsyncWebServerRequest* request,
                                   uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    // AUDIT FIX (P0.2): reject an out-of-range ID or an unknown type BEFORE it
    // can reach the runtime tables (a bogus ID would index _actuator_safety[]
    // out of bounds; a bad type would hit no behaviour handler).
    int raw_id   = doc["id"]   | -1;
    int raw_type = doc["type"] | -1;
    if (raw_id < 0 || raw_id >= MAX_ACTUATORS) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid actuator id (0.." "127" ")\"}");
        return;
    }
    if (raw_type != ACT_SERVO && raw_type != ACT_SOLENOID) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid actuator type (0=servo,1=solenoid)\"}");
        return;
    }

    ActuatorConfig act = {};
    act.id           = (uint8_t)raw_id;
    act.type         = (ActuatorType)(uint8_t)raw_type;
    act.bus_id       = doc["bus_id"] | 0;
    act.pca_address  = doc["pca_addr"] | PCA_BASE_ADDRESS;
    act.pca_channel  = doc["pca_ch"] | 0;
    act.behavior     = doc["behavior"] | 0;
    act.enabled      = doc["enabled"] | true;
    act.latency_ms   = doc["latency_ms"] | 10;

    // Servo parameters
    act.angle_initial = doc["angle_init"] | 90;
    act.amplitude     = doc["amplitude"] | 45;
    act.speed_ms      = doc["speed_ms"] | 150;
    act.angle_b       = doc["angle_b"] | 120;
    act.hit_reverse   = doc["hit_reverse"] | false;

    // Solenoid parameters
    act.pulse_min_ms  = doc["pulse_min_ms"] | SOLENOID_MIN_PULSE_MS;
    act.pulse_ms      = doc["pulse_ms"] | 20;
    act.pwm_initial   = doc["pwm_initial"] | 4095;
    act.pwm_hold      = doc["pwm_hold"] | 2048;
    act.ramp_ms       = doc["ramp_ms"] | 50;

    // AUDIT FIX: validate ranges so the API can never write out-of-spec
    // values to the runtime config (e.g. amplitude=360, pwm_hold=65535).
    if (act.bus_id > 1) act.bus_id = 0;
    if (act.pca_address < PCA_BASE_ADDRESS ||
        act.pca_address >= PCA_BASE_ADDRESS + PCA_MAX_PER_BUS) {
        act.pca_address = PCA_BASE_ADDRESS;
    }
    if (act.pca_channel >= PCA_CHANNELS) act.pca_channel = 0;
    if (act.latency_ms > 1000) act.latency_ms = 1000;
    if (act.type == ACT_SERVO) {
        if (act.angle_initial > SERVO_MAX_ANGLE) act.angle_initial = SERVO_MAX_ANGLE;
        if (act.angle_b       > SERVO_MAX_ANGLE) act.angle_b       = SERVO_MAX_ANGLE;
        if (act.amplitude     > SERVO_MAX_ANGLE) act.amplitude     = SERVO_MAX_ANGLE;
        if (act.speed_ms == 0)                    act.speed_ms      = 1;
        if (act.speed_ms > 5000)                  act.speed_ms      = 5000;
        if (act.behavior > SERVO_TOUCHE)          act.behavior      = SERVO_FRAPPE;
    } else {
        if (act.pwm_initial > SOLENOID_PWM_MAX) act.pwm_initial = SOLENOID_PWM_MAX;
        if (act.pwm_hold    > SOLENOID_PWM_MAX) act.pwm_hold    = SOLENOID_PWM_MAX;
        if (act.pulse_ms     > 500)             act.pulse_ms     = 500;
        if (act.pulse_min_ms > act.pulse_ms)    act.pulse_min_ms = act.pulse_ms;
        if (act.ramp_ms      > 5000)            act.ramp_ms      = 5000;
        if (act.behavior > SOL_HIT_AND_HOLD)    act.behavior     = SOL_FRAPPE;
    }

    // AUDIT FIX (point B / P0.5): hold the actuator lock across the add/update,
    // the rest-position init (I²C) and the scheduler resync, so the real-time
    // Scheduler on Core 1 never dereferences a slot being written or reshuffled
    // underneath it. The RAII guard guarantees the unlock and its locked()
    // result IS checked — if the lock times out the edit is refused rather than
    // performed unguarded.
    // AUDIT FIX (P0.2): reject a duplicate output target (same bus + PCA
    // address + channel as a DIFFERENT actuator) — two actuators driving one
    // physical channel is a wiring/config error.
    {
        ActuatorConfig* existing = _config->getActuators();
        uint8_t ecount = _config->getActuatorCount();
        for (uint8_t i = 0; i < ecount; i++) {
            if (existing[i].id != act.id &&
                existing[i].bus_id == act.bus_id &&
                existing[i].pca_address == act.pca_address &&
                existing[i].pca_channel == act.pca_channel) {
                request->send(400, "application/json",
                              "{\"error\":\"another actuator already uses this bus/address/channel\"}");
                return;
            }
        }
    }

    bool added = false;
    {
        ActuatorLockGuard guard(*_config);
        if (!guard.locked()) {
            request->send(503, "application/json",
                          "{\"error\":\"actuator configuration busy\"}");
            return;
        }
        added = _config->addActuator(act);
        if (added) {
            ActuatorConfig* actuators = _config->getActuators();
            uint8_t count = _config->getActuatorCount();
            for (uint8_t i = 0; i < count; i++) {
                if (actuators[i].id == act.id) {
                    _engine->initActuator(actuators[i]);
                    break;
                }
            }
            // Rebuild the scheduler's actuator table from the config array.
            if (_scheduler) _scheduler->syncActuators(actuators, count);
        }
    }  // lock released here

    if (added) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"max actuators reached\"}");
    }
}

void WebServer::handlePostWiFi(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    // AUDIT FIX (UI-P1): start from the EXISTING config and only overlay the
    // fields present, so a save with blank password fields does not wipe the
    // stored passwords. Password semantics:
    //   - field absent            -> keep the current password;
    //   - non-empty string        -> replace;
    //   - "clear_password":true    -> explicitly clear (STA);
    //   - "clear_ap_password":true -> explicitly clear (AP, -> MAC default).
    WiFiConfig wifi = *_config->getWiFiConfig();

    if (!doc["ssid"].isNull())
        strlcpy(wifi.ssid, doc["ssid"] | "", sizeof(wifi.ssid));
    if (!doc["hostname"].isNull())
        strlcpy(wifi.hostname, doc["hostname"] | WIFI_DEFAULT_HOSTNAME, sizeof(wifi.hostname));
    if (!doc["enabled"].isNull())
        wifi.enabled = doc["enabled"];
    if (!doc["ap_fallback"].isNull())
        wifi.ap_fallback = doc["ap_fallback"];

    // STA password
    if (doc["clear_password"] | false) {
        wifi.password[0] = '\0';
    } else if (doc["password"].is<const char*>() &&
               (doc["password"].as<const char*>())[0] != '\0') {
        strlcpy(wifi.password, doc["password"], sizeof(wifi.password));
    }

    // AP password (must be >= 8 chars when set)
    if (doc["clear_ap_password"] | false) {
        wifi.ap_password[0] = '\0';
    } else if (doc["ap_password"].is<const char*>() &&
               (doc["ap_password"].as<const char*>())[0] != '\0') {
        const char* ap_pw = doc["ap_password"];
        if (strlen(ap_pw) < 8) {
            request->send(400, "application/json",
                          "{\"error\":\"AP password must be >= 8 characters\"}");
            return;
        }
        strlcpy(wifi.ap_password, ap_pw, sizeof(wifi.ap_password));
    }

    _config->setWiFiConfig(wifi);
    request->send(200, "application/json",
                  "{\"ok\":true,\"note\":\"restart to apply WiFi changes\"}");
}

void WebServer::handlePostMidi(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    MidiInputConfig midi = {};
    midi.serial_enabled   = doc["serial_enabled"] | true;
    midi.udp_enabled      = doc["udp_enabled"] | true;
    midi.rtp_enabled      = doc["rtp_enabled"] | true;
    midi.udp_port         = doc["udp_port"] | MIDI_UDP_PORT;
    // AUDIT FIX (P1.2): the AppleMIDI session port is fixed at compile time
    // (APPLEMIDI_CREATE_INSTANCE) and cannot be rebound at runtime. Ignore any
    // client-supplied rtp_port and pin it to the real value so the UI never
    // presents a setting that silently has no effect.
    midi.rtp_port         = MIDI_RTP_PORT;
    midi.jitter_buffer_ms = doc["jitter_buffer_ms"] | MIDI_JITTER_BUFFER_MS;
    midi.serial_rx_pin    = doc["serial_rx_pin"] | MIDI_SERIAL_RX_PIN;

    // Keep raw UDP clear of the AppleMIDI control/data port pair.
    if (midi.udp_port == midi.rtp_port || midi.udp_port == (uint16_t)(midi.rtp_port + 1)) {
        midi.udp_port = MIDI_UDP_PORT;
    }

    _config->setMidiInputConfig(midi);

    // Note: MIDI transports require a restart to apply
    request->send(200, "application/json",
                  "{\"ok\":true,\"note\":\"restart to apply MIDI changes\"}");
}

void WebServer::handlePostRouting(AsyncWebServerRequest* request,
                                  uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config || !_dispatcher) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    uint8_t inst_idx = doc["instrument"] | 0;

    // Validate against instrument count (not routings, which may be desynchronized)
    if (inst_idx >= _config->getInstrumentCount()) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid instrument index\"}");
        return;
    }

    // ------------------------------------------------------------------------
    // AUDIT FIX (P1.6): validate the ENTIRE payload into local staging buffers
    // BEFORE touching the live routing. Nothing is applied unless every note,
    // CC and velocity point is valid, so the dispatcher can never observe a
    // partially-updated or inconsistent routing. Rejections carry a precise
    // reason.
    // ------------------------------------------------------------------------
    const ActuatorConfig* actuators = _config->getActuators();
    const uint8_t actuator_count = _config->getActuatorCount();

    // Local lambda: resolve an actuator by id (returns nullptr if absent).
    auto findActuator = [&](uint8_t id) -> const ActuatorConfig* {
        for (uint8_t i = 0; i < actuator_count; i++)
            if (actuators[i].id == id) return &actuators[i];
        return nullptr;
    };

    String errors;

    // --- Staging: notes ---
    NoteMapping staged_notes[MAX_NOTE_MAPPINGS];
    uint8_t staged_note_count = 0;
    bool have_notes = false;
    JsonArray notes = doc["notes"];
    if (!notes.isNull()) {
        have_notes = true;
        uint16_t idx = 0;
        for (JsonObject nm : notes) {
            idx++;
            if (staged_note_count >= MAX_NOTE_MAPPINGS) {
                errors += "Trop de notes (max " + String(MAX_NOTE_MAPPINGS) + ").\n";
                break;
            }
            long note      = nm["note"] | -1;
            long act_id    = nm["actuator"] | -1;
            long behavior  = nm["behavior"] | 0xFF;
            bool enabled   = nm["enabled"] | true;

            if (note < 0 || note > 127) {
                errors += "Note #" + String(idx) + " : valeur " + String(note) +
                          " hors plage MIDI [0,127].\n";
            }
            const ActuatorConfig* act = (act_id >= 0) ? findActuator((uint8_t)act_id) : nullptr;
            if (!act) {
                errors += "Note #" + String(idx) + " : actionneur " + String(act_id) +
                          " inexistant.\n";
            } else if (behavior != 0xFF) {
                bool compat = (act->type == ACT_SERVO) ? (behavior >= 0 && behavior <= 3)
                                                       : (behavior >= 0 && behavior <= 1);
                if (!compat) {
                    errors += "Note #" + String(idx) + " : comportement " + String(behavior) +
                              " incompatible avec l'actionneur " + String(act_id) +
                              (act->type == ACT_SERVO ? " (servo)" : " (solénoïde)") + ".\n";
                }
            }
            // Duplicate note detection within this instrument.
            for (uint8_t j = 0; j < staged_note_count; j++) {
                if (staged_notes[j].midi_note == (uint8_t)note) {
                    errors += "Note #" + String(idx) + " : note MIDI " + String(note) +
                              " déjà routée dans cet instrument.\n";
                    break;
                }
            }

            NoteMapping& m = staged_notes[staged_note_count++];
            m.midi_note         = (uint8_t)(note < 0 ? 0 : note);
            m.actuator_id       = (uint8_t)(act_id < 0 ? 0 : act_id);
            m.behavior_override = (uint8_t)behavior;
            m.enabled           = enabled;
        }
    }

    // --- Staging: CCs ---
    CCMapping staged_ccs[MAX_CC_MAPPINGS];
    uint8_t staged_cc_count = 0;
    bool have_ccs = false;
    JsonArray ccs = doc["ccs"];
    if (!ccs.isNull()) {
        have_ccs = true;
        uint16_t idx = 0;
        for (JsonObject cm : ccs) {
            idx++;
            if (staged_cc_count >= MAX_CC_MAPPINGS) {
                errors += "Trop de CC (max " + String(MAX_CC_MAPPINGS) + ").\n";
                break;
            }
            long cc      = cm["cc"] | -1;
            long act_id  = cm["actuator"] | -1;
            long target  = cm["target"] | 0;
            long rmin    = cm["min"] | 0;
            long rmax    = cm["max"] | 127;
            bool enabled = cm["enabled"] | true;

            if (cc < 0 || cc > 127) {
                errors += "CC #" + String(idx) + " : numéro " + String(cc) +
                          " hors plage [0,127].\n";
            }
            if (target < 0 || target > 3) {
                errors += "CC #" + String(idx) + " : cible " + String(target) + " invalide.\n";
            }
            const ActuatorConfig* act = (act_id >= 0) ? findActuator((uint8_t)act_id) : nullptr;
            if (!act) {
                errors += "CC #" + String(idx) + " : actionneur " + String(act_id) +
                          " inexistant.\n";
            } else if (target >= 0 && target <= 3) {
                // POSITION/AMPLITUDE/SPEED are servo parameters; PWM_HOLD is a
                // solenoid parameter.
                bool compat = (target == CC_TARGET_PWM_HOLD) ? (act->type == ACT_SOLENOID)
                                                             : (act->type == ACT_SERVO);
                if (!compat) {
                    errors += "CC #" + String(idx) + " : cible " + String(target) +
                              " incompatible avec l'actionneur " + String(act_id) +
                              (act->type == ACT_SERVO ? " (servo)" : " (solénoïde)") + ".\n";
                }
            }
            if (rmin < 0 || rmax < 0 || rmin > 65535 || rmax > 65535 || rmin > rmax) {
                errors += "CC #" + String(idx) + " : plage [" + String(rmin) + "," +
                          String(rmax) + "] invalide.\n";
            }

            CCMapping& c = staged_ccs[staged_cc_count++];
            c.cc_number   = (uint8_t)(cc < 0 ? 0 : cc);
            c.actuator_id = (uint8_t)(act_id < 0 ? 0 : act_id);
            c.target      = (CCTarget)(uint8_t)target;
            c.range_min   = (uint16_t)(rmin < 0 ? 0 : rmin);
            c.range_max   = (uint16_t)(rmax < 0 ? 0 : rmax);
            c.enabled     = enabled;
        }
    }

    // --- Staging: velocity curve (must be strictly increasing by input) ---
    VelocityCurvePoint staged_vel[VELOCITY_CURVE_POINTS];
    uint8_t staged_vel_count = 0;
    bool have_vel = false;
    JsonArray vel = doc["velocity_curve"];
    if (!vel.isNull()) {
        have_vel = true;
        uint16_t idx = 0;
        int last_input = -1;
        for (JsonObject vp : vel) {
            idx++;
            if (staged_vel_count >= VELOCITY_CURVE_POINTS) {
                errors += "Trop de points de courbe (max " +
                          String(VELOCITY_CURVE_POINTS) + ").\n";
                break;
            }
            long in  = vp["in"] | -1;
            long out = vp["out"] | -1;
            if (in < 0 || in > 127 || out < 0 || out > 127) {
                errors += "Courbe point #" + String(idx) + " : (" + String(in) + "," +
                          String(out) + ") hors plage [0,127].\n";
            }
            if ((int)in <= last_input) {
                errors += "Courbe point #" + String(idx) +
                          " : entrées non strictement croissantes.\n";
            }
            last_input = (int)in;

            staged_vel[staged_vel_count].input  = (uint8_t)(in < 0 ? 0 : in);
            staged_vel[staged_vel_count].output = (uint8_t)(out < 0 ? 0 : out);
            staged_vel_count++;
        }
    }

    if (errors.length() > 0) {
        JsonDocument resp;
        resp["error"] = "invalid routing";
        resp["detail"] = errors;
        String out;
        serializeJson(resp, out);
        request->send(400, "application/json", out);
        return;
    }

    // ------------------------------------------------------------------------
    // Everything validated — apply atomically. Take the actuator lock (also held
    // by structural edits) and write COUNT LAST so a concurrent dispatcher read
    // never sees a half-written map: count is lowered to 0 first, entries are
    // filled, then the count is published.
    // ------------------------------------------------------------------------
    {
        ActuatorLockGuard guard(*_config);
        if (!guard.locked()) {
            request->send(503, "application/json",
                          "{\"error\":\"routing busy\"}");
            return;
        }

        // If routing_count < inst_idx+1 (desynchronization), create the missing
        // entries under the lock.
        while (_config->getRoutingCount() <= inst_idx) {
            MidiRoutingConfig empty = {};
            empty.instrument_index = _config->getRoutingCount();
            _config->addRoutingConfig(empty);
        }
        MidiRoutingConfig& routing = _config->getRoutingConfigs()[inst_idx];

        if (have_notes) {
            routing.note_map_count = 0;  // hide before rewriting entries
            for (uint8_t i = 0; i < staged_note_count; i++)
                routing.note_map[i] = staged_notes[i];
            routing.note_map_count = staged_note_count;  // publish
        }
        if (have_ccs) {
            routing.cc_map_count = 0;
            for (uint8_t i = 0; i < staged_cc_count; i++)
                routing.cc_map[i] = staged_ccs[i];
            routing.cc_map_count = staged_cc_count;
        }
        if (have_vel) {
            routing.velocity_curve_count = 0;
            for (uint8_t i = 0; i < staged_vel_count; i++)
                routing.velocity_curve[i] = staged_vel[i];
            routing.velocity_curve_count = staged_vel_count;
        }

        // AUDIT FIX: keep the instrument's legacy actuator_ids/midi_notes cache
        // in sync with the routing note_map (single source of truth).
        _config->rebuildInstrumentFromRouting(inst_idx);
    }  // lock released before the dispatcher reload

    _dispatcher->refreshConfig();

    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostPowerBudget(AsyncWebServerRequest* request,
                                      uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_resources) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    if (!doc["global_max_ma"].isNull())
        _resources->setGlobalMaxMA(doc["global_max_ma"]);
    if (!doc["servo_max_ma"].isNull())
        _resources->setBusMaxMA(0, doc["servo_max_ma"]);
    if (!doc["sol_max_ma"].isNull())
        _resources->setBusMaxMA(1, doc["sol_max_ma"]);
    if (!doc["max_polyphony"].isNull())
        _resources->setGlobalMaxPolyphony(doc["max_polyphony"]);

    // AUDIT FIX (UI-P1): mirror the change into the persisted config so a
    // subsequent "Save to flash" keeps it across reboots.
    if (_config) _config->setPowerBudget(_resources->getBudget());

    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostSafety(AsyncWebServerRequest* request,
                                 uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_resources) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    // AUDIT FIX (P1.6): clamp every safety limit to a sane range so the API can
    // never neuter the protections (duty 255%), instantly trip them
    // (watchdog 0 ms, current 0 mA) or set a nonsensical value.
    auto clampU = [](long v, long lo, long hi) -> long {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    };

    if (!doc["max_duty_pct"].isNull())
        _resources->setMaxDutyCycle((uint8_t)clampU(doc["max_duty_pct"] | 0, 1, 100));
    if (!doc["max_freq_hz"].isNull())
        _resources->setMaxFrequency((uint16_t)clampU(doc["max_freq_hz"] | 0, 1, 1000));
    if (!doc["watchdog_ms"].isNull())
        _resources->setWatchdogTimeout((uint16_t)clampU(doc["watchdog_ms"] | 0, 100, 60000));
    if (!doc["max_polyphony"].isNull())
        _resources->setMaxPolyphony((uint8_t)clampU(doc["max_polyphony"] | 0, 1, MAX_ACTUATORS));
    if (!doc["max_current_ma"].isNull())
        _resources->setMaxTotalCurrent((uint16_t)clampU(doc["max_current_ma"] | 0, 100, 60000));

    // AUDIT FIX (UI-P1): mirror the runtime limits into the persisted config so
    // a subsequent "Save to flash" keeps them across reboots.
    if (_config) {
        SafetyLimits sl;
        sl.max_duty_pct   = _resources->getMaxDutyCycle();
        sl.max_freq_hz    = _resources->getMaxFrequency();
        sl.watchdog_ms    = _resources->getWatchdogTimeout();
        sl.max_polyphony  = _resources->getMaxPolyphony();
        sl.max_current_ma = _resources->getMaxTotalCurrent();
        _config->setSafetyLimits(sl);
    }

    request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================================
// POST handlers — Actions
// ============================================================================

void WebServer::handlePostSave(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    if (_config->save()) {
        logger.log(LOG_INFO, CAT_SYSTEM, "Config saved to flash");
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        logger.log(LOG_ERROR, CAT_SYSTEM, "Config save failed");
        request->send(500, "application/json",
                      "{\"error\":\"save failed\"}");
    }
}

// ============================================================================
// AUDIT FIX (UX): configuration backup / restore
// ============================================================================
void WebServer::handleGetConfigExport(AsyncWebServerRequest* request) {
    // The export is a full backup and includes stored WiFi credentials, so it
    // is gated behind auth (in AP mode).
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    String json;
    if (!_config->exportJson(json)) {
        request->send(500, "application/json", "{\"error\":\"export failed\"}");
        return;
    }
    AsyncWebServerResponse* resp =
        request->beginResponse(200, "application/json", json);
    resp->addHeader("Content-Disposition", "attachment; filename=\"playmode-config.json\"");
    request->send(resp);
}

bool WebServer::waitForOutputsCut(uint32_t timeout_ms) {
    // If there is no resource manager, there is nothing to confirm — treat the
    // outputs as already safe (the caller is responsible for the actuator lock).
    if (!_resources) return true;

    _resources->requestKillSwitch();

    // Poll for Core 1 to physically execute the kill (OE HIGH). We deliberately
    // do NOT hold the actuator lock here: Core 1 must be free to run its request
    // processing and drive the OE pin. A short bounded wait keeps the async_tcp
    // task responsive and well under the task watchdog.
    uint32_t start = millis();
    while (!_resources->isKillSwitchActive()) {
        if ((uint32_t)(millis() - start) >= timeout_ms) return false;
        delay(10);
    }
    return true;
}

void WebServer::handlePostConfigImport(AsyncWebServerRequest* request,
                                       uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    // AUDIT FIX (P0.5): cut the physical outputs FIRST and confirm the kill was
    // executed by Core 1 BEFORE the configuration is replaced. Previously the
    // handler grabbed the actuator lock immediately after requesting the kill,
    // starving Core 1 of the lock it needs to reset the outputs — so the config
    // could be swapped while actuators were still powered. We now confirm the
    // OE-HIGH state without holding the lock; only then do we swap.
    if (!waitForOutputsCut(1000)) {
        request->send(503, "application/json",
                      "{\"error\":\"could not confirm outputs are cut — import aborted\"}");
        return;
    }

    bool ok;
    String errors;
    {
        ActuatorLockGuard guard(*_config);
        if (!guard.locked()) {
            request->send(503, "application/json", "{\"error\":\"configuration busy\"}");
            return;
        }
        // AUDIT FIX (P1.5): strict import — the whole file is rejected (and the
        // live config left untouched) if any actuator is invalid or duplicated.
        ok = _config->importJson(data, len, &errors);
        if (ok && _scheduler)
            _scheduler->syncActuators(_config->getActuators(), _config->getActuatorCount());
    }

    if (!ok) {
        JsonDocument resp;
        resp["error"] = "invalid or rejected configuration";
        if (errors.length() > 0) resp["detail"] = errors;
        String out;
        serializeJson(resp, out);
        request->send(400, "application/json", out);
        return;
    }
    logger.log(LOG_WARN, CAT_SYSTEM, "Configuration imported — restarting");
    _restart_at_ms = millis() + 400;
    // 202 Accepted: the swap succeeded and the device will restart to apply it.
    request->send(202, "application/json",
                  "{\"ok\":true,\"note\":\"configuration imported — device is restarting\"}");
}

void WebServer::handlePostDefaults(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    // AUDIT FIX (P0.4 / P0.5): a factory reset must stop the hardware BEFORE it
    // wipes the config, and must not claim success if the persist failed.
    // Sequence:
    //   1. request the kill switch and CONFIRM it was executed by Core 1 (OE
    //      HIGH), without holding the actuator lock;
    //   2. lock the actuator array and swap in the defaults;
    //   3. resync the scheduler's actuator table to empty;
    //   4. persist the defaults — and check the result honestly;
    //   5. reboot — at boot OE starts HIGH (outputs disabled) until the fresh
    //      config is applied, satisfying "OE off until next start completes".
    if (!waitForOutputsCut(1000)) {
        request->send(503, "application/json",
                      "{\"error\":\"could not confirm outputs are cut — reset aborted\"}");
        return;
    }

    bool saved;
    {
        ActuatorLockGuard guard(*_config);
        if (!guard.locked()) {
            request->send(503, "application/json",
                          "{\"error\":\"actuator configuration busy\"}");
            return;
        }
        _config->loadDefaults();
        if (_scheduler) _scheduler->syncActuators(nullptr, 0);
        saved = _config->save();
    }  // lock released here

    if (!saved) {
        // AUDIT FIX (P0.5): do NOT report success when the defaults could not be
        // persisted — the next boot would reload the OLD config.
        logger.log(LOG_ERROR, CAT_SYSTEM, "Factory reset: persist FAILED");
        request->send(500, "application/json",
                      "{\"error\":\"factory reset applied in memory but persist failed\"}");
        return;
    }

    if (_dispatcher) _dispatcher->refreshConfig();
    logger.log(LOG_WARN, CAT_SYSTEM, "Factory reset applied — restarting");

    _restart_at_ms = millis() + 400;  // let the response flush, then reboot
    request->send(202, "application/json",
                  "{\"ok\":true,\"note\":\"factory reset — device is restarting\"}");
}

void WebServer::handlePostTestActuator(AsyncWebServerRequest* request,
                                       uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_scheduler || !_config) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    uint8_t act_id   = doc["id"] | 0;
    uint8_t velocity = doc["velocity"] | 100;
    bool note_on     = doc["note_on"] | true;
    uint16_t delay_ms = doc["delay_ms"] | 0;

    SchedulerEvent evt = {};
    evt.trigger_time_us = (uint32_t)esp_timer_get_time() + (delay_ms * 1000UL);
    evt.actuator_id = act_id;
    evt.action      = note_on ? ACTION_NOTE_ON : ACTION_NOTE_OFF;
    evt.velocity    = velocity;
    evt.priority    = 0;
    evt.behavior_override = 0xFF;   // manual test: actuator's own behaviour
    evt.instrument_index  = 0xFF;

    if (_scheduler->pushEvent(evt)) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json",
                      "{\"error\":\"scheduler queue full\"}");
    }
}

void WebServer::handlePostKillSwitch(AsyncWebServerRequest* request,
                                     uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_resources) { request->send(500); return; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    // AUDIT FIX (P0.1): the web task must not touch the PCA9685 or the
    // scheduler heap directly. Raise a request; the scheduler task (Core 1),
    // sole owner of those resources, performs the actual stop / re-arm. New
    // executions are blocked immediately because checkEvent() also consults the
    // atomic kill-request flag.
    bool activate = doc["active"] | false;
    if (activate) {
        _resources->requestKillSwitch();
        logger.log(LOG_CRITICAL, CAT_SAFETY, "Kill switch requested from web UI");
    } else {
        _resources->requestRearm();
        logger.log(LOG_INFO, CAT_SAFETY, "Kill switch re-arm requested from web UI");
    }

    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostScanI2C(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_resources) { request->send(500); return; }

    // AUDIT FIX (P0.2): the web task must not delete/recreate driver objects
    // while the scheduler (Core 1) may be dereferencing a getDriver() pointer.
    // Request the rescan; the scheduler runs it after activating the kill
    // switch and leaves the outputs disabled until a manual re-arm. Results are
    // read afterwards via GET /api/buses.
    _resources->requestRescan();
    logger.log(LOG_WARN, CAT_SYSTEM, "I2C rescan requested (outputs will be disabled)");
    request->send(202, "application/json",
                  "{\"ok\":true,\"note\":\"rescan scheduled; outputs disabled, "
                  "re-arm required. Poll /api/buses for results\"}");
}

// ============================================================================
// DELETE handlers
// ============================================================================

void WebServer::handleDeleteInstrument(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    if (!request->hasParam("index")) {
        request->send(400, "application/json",
                      "{\"error\":\"missing index param\"}");
        return;
    }

    uint8_t index = request->getParam("index")->value().toInt();
    InstrumentConfig* instruments = _config->getInstruments();
    uint8_t count = _config->getInstrumentCount();

    if (index >= count) {
        request->send(404, "application/json",
                      "{\"error\":\"instrument not found\"}");
        return;
    }

    if (_config->removeInstrument(index)) {
        if (_dispatcher) _dispatcher->refreshConfig();
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(404, "application/json",
                      "{\"error\":\"instrument not found\"}");
    }
}

void WebServer::handleDeleteActuator(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    if (!request->hasParam("id")) {
        request->send(400, "application/json",
                      "{\"error\":\"missing id param\"}");
        return;
    }

    uint8_t id = request->getParam("id")->value().toInt();

    // AUDIT FIX (point B / P0.5): hold the actuator lock across the rest reset
    // (I²C), the array-shifting removeActuator() and the scheduler resync. The
    // RAII guard's locked() result IS checked — refuse rather than mutate
    // unguarded if the lock times out.
    bool removed = false;
    {
        ActuatorLockGuard guard(*_config);
        if (!guard.locked()) {
            request->send(503, "application/json",
                          "{\"error\":\"actuator configuration busy\"}");
            return;
        }

        // Return the actuator to rest before deletion
        ActuatorConfig* actuators = _config->getActuators();
        uint8_t count = _config->getActuatorCount();
        for (uint8_t i = 0; i < count; i++) {
            if (actuators[i].id == id) {
                if (_engine) _engine->resetActuator(actuators[i]);
                break;
            }
        }

        removed = _config->removeActuator(id);
        if (removed && _scheduler) {
            // removeActuator() shifts the config array, so rebuild the
            // scheduler's pointer table to avoid stale/duplicate slots.
            _scheduler->syncActuators(_config->getActuators(),
                                      _config->getActuatorCount());
        }
    }  // lock released here

    if (removed) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(404, "application/json",
                      "{\"error\":\"actuator not found\"}");
    }
}

// ============================================================================
// WebSocket
// ============================================================================

void WebServer::setupWebSocket() {
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        onWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);
}

void WebServer::onWebSocketEvent(AsyncWebSocket* server,
                                 AsyncWebSocketClient* client,
                                 AwsEventType type, void* arg,
                                 uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WEB] WS client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("[WEB] WS client #%u disconnected\n", client->id());
            // AUDIT FIX: a virtual-piano client that drops mid-note would leave
            // Key / Hit-and-Hold actuators held. When the last client leaves,
            // release everything.
            if (_ws.count() == 0 && _dispatcher) {
                _dispatcher->allNotesOff();
            }
            break;
        case WS_EVT_DATA: {
            // Incoming WebSocket commands (future: virtual piano test)
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len
                && info->opcode == WS_TEXT)
            {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, (const char*)data, len);
                if (!err) {
                    const char* cmd = doc["cmd"];
                    // AUDIT FIX (P0.9): the WS "test" command drives a real
                    // actuator — require the AP token in AP mode, same as the
                    // REST write endpoints.
                    if (cmd && strcmp(cmd, "test") == 0 &&
                        wsCommandAuthorized(String(doc["token"] | ""))) {
                        // AUDIT FIX (UI-P1): both NOTE_ON and NOTE_OFF travel
                        // over the SAME WebSocket, so a network drop between
                        // press and release can no longer strand a held note.
                        uint8_t act_id   = doc["id"] | 0;
                        uint8_t velocity = doc["vel"] | 100;
                        bool on          = doc["on"] | true;
                        if (_scheduler) {
                            SchedulerEvent evt = {};
                            evt.trigger_time_us = (uint32_t)esp_timer_get_time();
                            evt.actuator_id = act_id;
                            evt.action = on ? ACTION_NOTE_ON : ACTION_NOTE_OFF;
                            evt.velocity = on ? velocity : 0;
                            evt.priority = 0;
                            evt.behavior_override = 0xFF;
                            evt.instrument_index  = 0xFF;
                            _scheduler->pushEvent(evt);
                        }
                    }
                }
            }
            break;
        }
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServer::broadcastStatus() {
    String json;
    buildStatusJSON(json);
    if (json.length() == 0) {
        Serial.println("[WEB] broadcastStatus: empty JSON (insufficient heap?)");
        return;
    }
    _ws.textAll(json);
}

// ============================================================================
// Helpers JSON
// ============================================================================

void WebServer::buildStatusJSON(String& output) {
    // AUDIT FIX (points D/E): ArduinoJson 7 elastic document — grows as needed
    // (active_actuators + power + safety + midi_msgs + log_count) instead of a
    // fixed capacity that could silently truncate the status payload.
    JsonDocument doc;

    doc["uptime_s"] = millis() / 1000;
    doc["heap"]     = ESP.getFreeHeap();
    doc["fw_version"] = FW_VERSION;
    doc["fw_build"]   = FW_BUILD;

    // AUDIT FIX (UI/UX): a single, explicit machine state for the permanent
    // dashboard: FAULT (latched over-current) / DISARMED (kill active) / ARMED.
    if (_resources) {
        const SafetyState& ss0 = _resources->getGlobalState();
        const char* mstate = ss0.fault_latched ? "fault"
                           : ss0.kill_switch_active ? "disarmed" : "armed";
        doc["machine_state"] = mstate;
    }

    // Scheduler
    if (_scheduler) {
        JsonObject sched = doc["scheduler"].to<JsonObject>();
        sched["queued"]    = _scheduler->getQueuedEventCount();
        sched["processed"] = _scheduler->getProcessedCount();
        sched["running"]   = _scheduler->isRunning();
    }

    // MIDI Transport
    if (_transport) {
        JsonObject midi = doc["midi"].to<JsonObject>();
        midi["serial_bytes"] = _transport->getSerialByteCount();
        midi["udp_packets"]  = _transport->getUdpPacketCount();
        midi["rtp_packets"]  = _transport->getRtpPacketCount();
    }

    // Dispatcher
    if (_dispatcher) {
        JsonObject disp = doc["dispatcher"].to<JsonObject>();
        disp["dispatched"]    = _dispatcher->getDispatchedCount();
        disp["dropped"]       = _dispatcher->getDroppedCount();
        // AUDIT FIX (core): power rejections are now counted by the ResourceManager
        // (the scheduler makes the admission decision on Core 1).
        disp["pwr_rejected"]  = _resources ? _resources->getStats().total_rejected : 0;
    }

    // Safety
    if (_resources) {
        JsonObject safe = doc["safety"].to<JsonObject>();
        const SafetyState& ss = _resources->getGlobalState();
        safe["current_ma"]  = ss.total_estimated_current_ma;
        safe["active"]      = ss.active_actuator_count;
        safe["kill_switch"] = ss.kill_switch_active;
        safe["degradation"] = ss.degradation_active;
        safe["over_current"] = ss.over_current;
    }

    // Power
    if (_resources) {
        JsonObject pwr = doc["power"].to<JsonObject>();
        const PowerStats& ps = _resources->getStats();
        const PowerBudget& pb = _resources->getBudget();
        pwr["total_ma"]      = ps.total_estimated_ma;
        pwr["servo_ma"]      = ps.servo_bus_ma;
        pwr["sol_ma"]        = ps.solenoid_bus_ma;
        pwr["budget_pct"]    = ps.budget_used_percent;
        pwr["degradation"]   = ps.degradation_active;
        // AUDIT FIX: fields expected by the frontend for the polyphony gauge
        pwr["active_count"]  = ps.global_active_count;
        pwr["max_polyphony"] = pb.global_max_polyphony;
    }

    // Active actuators (compact summary)
    if (_config) {
        JsonArray acts = doc["active_actuators"].to<JsonArray>();
        ActuatorConfig* actuators = _config->getActuators();
        uint8_t count = _config->getActuatorCount();
        for (uint8_t i = 0; i < count; i++) {
            if (actuators[i].state.active) {
                JsonObject a = acts.add<JsonObject>();
                a["id"]  = actuators[i].id;
                a["pos"] = actuators[i].state.current_position;
            }
        }
    }

    // WiFi
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = WiFi.isConnected();
    wifi["rssi"]      = WiFi.RSSI();

    // AUDIT FIX: relay recent MIDI messages via WebSocket
    if (_dispatcher) {
        static const uint8_t MAX_WS_MIDI = 16;
        MidiDispatcher::WsLogEntry entries[MAX_WS_MIDI];
        uint8_t n = _dispatcher->drainWsLog(entries, MAX_WS_MIDI);
        if (n > 0) {
            static const char* SRC_NAMES[] = {"serial", "udp", "rtp"};
            JsonArray msgs = doc["midi_msgs"].to<JsonArray>();
            for (uint8_t i = 0; i < n; i++) {
                JsonObject m = msgs.add<JsonObject>();
                m["type"]   = (uint8_t)entries[i].msg.type | entries[i].msg.channel;
                m["d1"]     = entries[i].msg.data1;
                m["d2"]     = entries[i].msg.data2;
                m["src"]    = SRC_NAMES[entries[i].msg.source % 3];
                m["t"]      = (uint32_t)(entries[i].msg.timestamp_us / 1000);
                m["routed"] = entries[i].routed;
            }
        }
    }

    // Log count (Phase 9) — allows the UI to detect new entries
    doc["log_count"] = logger.getCount();

    serializeJson(doc, output);
}

void WebServer::buildActuatorJSON(const ActuatorConfig& act, String& output) {
    JsonDocument doc;
    doc["id"]       = act.id;
    doc["type"]     = act.type;
    doc["bus_id"]   = act.bus_id;
    doc["enabled"]  = act.enabled;
    doc["active"]   = act.state.active;
    doc["position"] = act.state.current_position;
    serializeJson(doc, output);
}

void WebServer::buildInstrumentJSON(const InstrumentConfig& inst, String& output) {
    JsonDocument doc;
    doc["name"]    = inst.name;
    doc["channel"] = inst.midi_channel;
    doc["enabled"] = inst.enabled;
    doc["count"]   = inst.actuator_count;
    serializeJson(doc, output);
}

void WebServer::buildBusJSON(const BusConfig& bus, String& output) {
    JsonDocument doc;
    doc["id"]      = bus.id;
    doc["enabled"] = bus.enabled;
    doc["pca_count"] = bus.pca_count;
    serializeJson(doc, output);
}

// ============================================================================
// Acoustic calibration (Phase 7)
// ============================================================================

static const char* calStateName(CalibrationState s) {
    switch (s) {
        case CAL_IDLE:       return "idle";
        case CAL_AMBIENT:    return "ambient";
        case CAL_TRIGGERING: return "triggering";
        case CAL_RECORDING:  return "recording";
        case CAL_PAUSING:    return "pausing";
        case CAL_COMPLETE:   return "complete";
        case CAL_ERROR:      return "error";
        default:             return "unknown";
    }
}

void WebServer::handleGetCalibrateStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    if (!_calibrator) {
        doc["available"] = false;
        doc["state"]     = "idle";
        String out; serializeJson(doc, out);
        request->send(200, "application/json", out);
        return;
    }

    bool micReady = _calibrator->isMicReady();
    doc["available"]    = micReady;
    doc["state"]        = calStateName(_calibrator->getState());
    doc["running"]      = _calibrator->isRunning();
    doc["progress"]     = _calibrator->getProgress();
    doc["current_act"]  = _calibrator->getCurrentActuatorId();
    doc["result_count"] = _calibrator->getResultCount();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetCalibrateResults(AsyncWebServerRequest* request) {
    if (!_calibrator) {
        request->send(503, "application/json",
                      "{\"error\":\"calibrator not available\"}");
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    uint8_t count = _calibrator->getResultCount();

    // Iterate over active actuators to retrieve their results
    if (_config) {
        ActuatorConfig* acts     = _config->getActuators();
        uint8_t         act_count = _config->getActuatorCount();

        for (uint8_t i = 0; i < act_count; i++) {
            const CalibrationResult* res =
                _calibrator->getResult(acts[i].id);

            JsonObject obj = arr.add<JsonObject>();
            obj["actuator_id"]     = acts[i].id;
            obj["current_latency"] = acts[i].latency_ms;

            if (res) {
                obj["measured_ms"]   = res->measured_latency_ms;
                obj["samples_taken"] = res->samples_taken;
                obj["success"]       = res->success;
                obj["timestamp_ms"]  = res->timestamp_ms;
            } else {
                obj["measured_ms"]   = nullptr;
                obj["samples_taken"] = 0;
                obj["success"]       = nullptr;
                obj["timestamp_ms"]  = 0;
            }
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handlePostCalibrate(AsyncWebServerRequest* request,
                                    uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_calibrator) {
        request->send(503, "application/json",
                      "{\"error\":\"calibrator not available\"}");
        return;
    }
    if (_calibrator->isRunning()) {
        request->send(409, "application/json",
                      "{\"error\":\"calibration in progress\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    bool ok;
    if (doc["all"] | false) {
        ok = _calibrator->startCalibrationAll();
    } else {
        uint8_t act_id = doc["id"] | 0;
        ok = _calibrator->startCalibration(act_id);
    }

    if (ok) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"unable to start calibration\"}");
    }
}

void WebServer::handlePostCalibrateApply(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_calibrator) {
        request->send(503, "application/json",
                      "{\"error\":\"calibrator not available\"}");
        return;
    }

    uint8_t applied = _calibrator->applyResults();

    // AUDIT FIX: persist the new latency values to flash so they survive
    // a reboot. Without this, every power cycle reverts to the previous
    // (possibly default) latencies and re-measurement is required.
    bool saved = false;
    if (applied > 0 && _config) {
        saved = _config->save();
        if (saved) {
            logger.log(LOG_INFO, CAT_CAL, "Calibration applied & saved (%d actuators)", applied);
        } else {
            logger.log(LOG_ERROR, CAT_CAL, "Calibration applied but save failed");
        }
    }

    JsonDocument doc;
    doc["ok"]      = true;
    doc["applied"] = applied;
    doc["saved"]   = saved;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handlePostCalibrateStop(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_calibrator) {
        request->send(503, "application/json",
                      "{\"error\":\"calibrator not available\"}");
        return;
    }
    _calibrator->stop();
    request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================================
// Industrial Test Manager (Phase 8)
// ============================================================================

static const char* testModeName(TestMode m) {
    switch (m) {
        case TEST_IDLE:   return "idle";
        case TEST_SWEEP:  return "sweep";
        case TEST_BURST:  return "burst";
        case TEST_STRESS: return "stress";
        default:          return "unknown";
    }
}

void WebServer::handleGetTestStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    if (!_testManager) {
        doc["available"] = false;
        doc["mode"]      = "idle";
        String out; serializeJson(doc, out);
        request->send(200, "application/json", out);
        return;
    }

    doc["available"]    = true;
    doc["mode"]         = testModeName(_testManager->getMode());
    doc["running"]      = _testManager->isRunning();
    doc["progress"]     = _testManager->getProgress();
    doc["current_act"]  = _testManager->getCurrentActuatorId();
    doc["events_sent"]  = _testManager->getEventsSent();
    doc["tests_run"]    = _testManager->getTestsRun();
    doc["log_count"]    = _testManager->getLogCount();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetTestLog(AsyncWebServerRequest* request) {
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }

    // Limit the number of entries returned (max 32 to save JSON RAM)
    const uint8_t MAX_ENTRIES = 32;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    uint8_t count = _testManager->getLogCount();
    if (count > MAX_ENTRIES) count = MAX_ENTRIES;

    for (uint8_t i = 0; i < count; i++) {
        const TestLogEntry& e = _testManager->getLogEntry(i);
        JsonObject obj = arr.add<JsonObject>();
        obj["t"]    = e.timestamp_ms;
        obj["act"]  = e.actuator_id;
        obj["vel"]  = e.velocity;
        obj["mode"] = testModeName(e.mode);
        obj["ok"]   = e.scheduled;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handlePostTestSweep(AsyncWebServerRequest* request,
                                    uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }
    if (_testManager->isRunning()) {
        request->send(409, "application/json",
                      "{\"error\":\"test in progress\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    uint8_t  velocity    = doc["velocity"]    | TEST_DEFAULT_VELOCITY;
    uint16_t interval_ms = doc["interval_ms"] | TEST_DEFAULT_INTERVAL_MS;
    uint16_t hold_ms     = doc["hold_ms"]     | TEST_DEFAULT_HOLD_MS;
    bool     loop        = doc["loop"]        | false;

    if (_testManager->startSweep(velocity, interval_ms, hold_ms, loop)) {
        logger.log(LOG_INFO, CAT_TEST, "Sweep started vel=%d int=%dms hold=%dms loop=%d",
                   velocity, interval_ms, hold_ms, (int)loop);
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"unable to start sweep\"}");
    }
}

void WebServer::handlePostTestBurst(AsyncWebServerRequest* request,
                                    uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }
    if (_testManager->isRunning()) {
        request->send(409, "application/json",
                      "{\"error\":\"test in progress\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    uint8_t  act_id      = doc["id"]          | 0;
    uint8_t  count       = doc["count"]       | (uint8_t)TEST_BURST_DEFAULT_COUNT;
    uint8_t  velocity    = doc["velocity"]    | TEST_DEFAULT_VELOCITY;
    uint16_t interval_ms = doc["interval_ms"] | (uint16_t)TEST_BURST_DEFAULT_INTVL_MS;

    // AUDIT FIX: a burst of 0 strikes is meaningless and made getProgress()
    // divide by zero — clamp to at least one strike.
    if (count == 0) count = 1;

    if (_testManager->startBurst(act_id, count, velocity, interval_ms)) {
        logger.log(LOG_INFO, CAT_TEST, "Burst started act=%d count=%d vel=%d",
                   act_id, count, velocity);
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"unable to start burst\"}");
    }
}

void WebServer::handlePostTestStress(AsyncWebServerRequest* request,
                                     uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }
    if (_testManager->isRunning()) {
        request->send(409, "application/json",
                      "{\"error\":\"test in progress\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        // Body optional for stress
        doc.clear();
    }

    uint8_t  velocity = doc["velocity"] | TEST_DEFAULT_VELOCITY;
    uint16_t hold_ms  = doc["hold_ms"]  | TEST_DEFAULT_HOLD_MS;

    if (_testManager->startStress(velocity, hold_ms)) {
        logger.log(LOG_INFO, CAT_TEST, "Stress test executed vel=%d hold=%dms",
                   velocity, hold_ms);
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"unable to start stress test\"}");
    }
}

void WebServer::handlePostTestStop(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }
    _testManager->stop();
    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostTestClearLog(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }
    _testManager->clearLog();
    request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================================
// Log Manager (Phase 9)
// ============================================================================

void WebServer::handleGetLogs(AsyncWebServerRequest* request) {
    // Optional filter ?level=N (0=DEBUG .. 4=CRITICAL)
    int min_level = 0;
    if (request->hasParam("level")) {
        min_level = request->getParam("level")->value().toInt();
        if (min_level < 0) min_level = 0;
        if (min_level > 4) min_level = 4;
    }

    // AUDIT FIX: thread-safe snapshot via getAllEntries() instead of direct access
    // (the web handler runs in a FreeRTOS task separate from the main loop)
    static LogEntry snapshot[LOG_BUFFER_SIZE];
    uint8_t total = 0;
    logger.getAllEntries(snapshot, total);

    String out = "{\"count\":";
    out += total;
    out += ",\"entries\":[";

    bool first = true;
    uint8_t shown = 0;

    for (uint8_t i = 0; i < total && shown < 100; i++) {
        const LogEntry& e = snapshot[i];
        if ((int)e.level < min_level) continue;

        if (!first) out += ",";
        out += "{\"t\":";    out += e.timestamp_ms;
        out += ",\"lvl\":";  out += (int)e.level;
        out += ",\"cat\":";  out += (int)e.category;
        out += ",\"msg\":\"";
        // Minimal message escaping
        for (const char* p = e.message; *p; p++) {
            if      (*p == '"')  out += "\\\"";
            else if (*p == '\\') out += "\\\\";
            else if (*p == '\n') out += "\\n";
            else                 out += *p;
        }
        out += "\"}";
        first = false;
        shown++;
    }

    out += "]}";
    request->send(200, "application/json", out);
}

void WebServer::handlePostLogsClear(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    logger.clear();
    logger.log(LOG_INFO, CAT_SYSTEM, "System log cleared");
    request->send(200, "application/json", "{\"ok\":true}");
}
