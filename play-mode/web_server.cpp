#include "web_server.h"
#include "web_ui.h"
#include "config_manager.h"
#include <WiFi.h>
#include "scheduler.h"
#include "safety_manager.h"
#include "power_manager.h"
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

    bool requireAuth(AsyncWebServerRequest* req) {
        // Auth only enforced in AP mode — STA networks are user-trusted.
        if (!(WiFi.getMode() & WIFI_AP) || (WiFi.getMode() & WIFI_STA)) {
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
    , _safety(nullptr)
    , _power(nullptr)
    , _dispatcher(nullptr)
    , _transport(nullptr)
    , _pca(nullptr)
    , _engine(nullptr)
    , _calibrator(nullptr)
    , _testManager(nullptr)
    , _last_ws_broadcast_ms(0)
{
}

void WebServer::setModules(ConfigManager* config, Scheduler* scheduler,
                           SafetyManager* safety, PowerManager* power,
                           MidiDispatcher* dispatcher, MidiTransport* transport,
                           PCADriver* pca, ActuatorEngine* engine)
{
    _config     = config;
    _scheduler  = scheduler;
    _safety     = safety;
    _power      = power;
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
        StaticJsonDocument<128> doc;
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

    // AUDIT FIX: Bus PWM frequency (missing endpoint — called by the frontend)
    auto* busPwmHandler = new AsyncCallbackJsonWebHandler("/api/bus/pwm",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            if (!_pca || !_config) { req->send(500); return; }
            uint8_t bus_id = json["bus_id"] | 0xFF;
            uint16_t freq  = json["freq_pwm"] | 0;
            if (bus_id > 1 || freq == 0) {
                req->send(400, "application/json",
                          "{\"error\":\"invalid bus_id or freq_pwm\"}");
                return;
            }
            _pca->setFrequency(bus_id, freq);
            // Update config for persistence
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

    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    InstrumentConfig* instruments = _config->getInstruments();
    uint8_t count = _config->getInstrumentCount();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["index"]   = i;
        obj["name"]    = instruments[i].name;
        obj["channel"] = instruments[i].midi_channel;
        obj["bus_id"]  = instruments[i].bus_id;
        obj["actuator_count"] = instruments[i].actuator_count;
        obj["latency_ms"]     = instruments[i].default_latency_ms;
        obj["auto_cal"]       = instruments[i].auto_calibration;
        obj["enabled"]        = instruments[i].enabled;

        JsonArray acts = obj.createNestedArray("actuators");
        for (uint8_t j = 0; j < instruments[i].actuator_count; j++) {
            JsonObject a = acts.createNestedObject();
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

    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();

    ActuatorConfig* actuators = _config->getActuators();
    uint8_t count = _config->getActuatorCount();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
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
        JsonObject state = obj.createNestedObject("state");
        state["active"]   = actuators[i].state.active;
        state["position"] = actuators[i].state.current_position;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetBuses(AsyncWebServerRequest* request) {
    if (!_pca) { request->send(500); return; }

    StaticJsonDocument<512> doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t b = 0; b < 2; b++) {
        BusConfig& bus = _pca->getBusConfig(b);
        JsonObject obj = arr.createNestedObject();
        obj["id"]        = bus.id;
        obj["sda"]       = bus.sda_pin;
        obj["scl"]       = bus.scl_pin;
        obj["oe"]        = bus.oe_pin;
        obj["freq_i2c"]  = bus.i2c_frequency;
        obj["freq_pwm"]  = bus.pwm_frequency;
        obj["enabled"]   = bus.enabled;
        obj["pca_count"] = bus.pca_count;

        JsonArray addrs = obj.createNestedArray("pca_addrs");
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

    StaticJsonDocument<256> doc;
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

    StaticJsonDocument<256> doc;
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

    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();

    MidiRoutingConfig* routings = _config->getRoutingConfigs();
    uint8_t count = _config->getRoutingCount();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["instrument"] = routings[i].instrument_index;

        JsonArray notes = obj.createNestedArray("notes");
        for (uint8_t n = 0; n < routings[i].note_map_count; n++) {
            JsonObject nm = notes.createNestedObject();
            nm["note"]     = routings[i].note_map[n].midi_note;
            nm["actuator"] = routings[i].note_map[n].actuator_id;
            nm["enabled"]  = routings[i].note_map[n].enabled;
        }

        JsonArray ccs = obj.createNestedArray("ccs");
        for (uint8_t c = 0; c < routings[i].cc_map_count; c++) {
            JsonObject cm = ccs.createNestedObject();
            cm["cc"]       = routings[i].cc_map[c].cc_number;
            cm["actuator"] = routings[i].cc_map[c].actuator_id;
            cm["target"]   = routings[i].cc_map[c].target;
            cm["min"]      = routings[i].cc_map[c].range_min;
            cm["max"]      = routings[i].cc_map[c].range_max;
            cm["enabled"]  = routings[i].cc_map[c].enabled;
        }

        JsonArray vel = obj.createNestedArray("velocity_curve");
        for (uint8_t v = 0; v < routings[i].velocity_curve_count; v++) {
            JsonObject vp = vel.createNestedObject();
            vp["in"]  = routings[i].velocity_curve[v].input;
            vp["out"] = routings[i].velocity_curve[v].output;
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetPower(AsyncWebServerRequest* request) {
    if (!_power) { request->send(500); return; }

    StaticJsonDocument<512> doc;
    const PowerStats& stats = _power->getStats();
    const PowerBudget& budget = _power->getBudget();

    JsonObject s = doc.createNestedObject("stats");
    s["total_ma"]      = stats.total_estimated_ma;
    s["servo_bus_ma"]  = stats.servo_bus_ma;
    s["sol_bus_ma"]    = stats.solenoid_bus_ma;
    s["active_count"]  = stats.global_active_count;
    s["rejected"]      = stats.total_rejected;
    s["budget_pct"]    = stats.budget_used_percent;
    s["degradation"]   = stats.degradation_active;
    s["exceeded"]      = stats.budget_exceeded;

    JsonObject b = doc.createNestedObject("budget");
    b["global_max_ma"] = budget.global_max_ma;
    b["servo_max_ma"]  = budget.servo_bus_max_ma;
    b["sol_max_ma"]    = budget.solenoid_bus_max_ma;
    b["max_polyphony"] = budget.global_max_polyphony;
    b["smart_reject"]  = budget.smart_rejection;

    JsonArray poly = b.createNestedArray("inst_polyphony");
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        poly.add(budget.instrument_max_polyphony[i]);
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handleGetSafety(AsyncWebServerRequest* request) {
    if (!_safety) { request->send(500); return; }

    StaticJsonDocument<512> doc;
    const SafetyState& state = _safety->getGlobalState();

    doc["total_current_ma"]  = state.total_estimated_current_ma;
    doc["active_count"]      = state.active_actuator_count;
    doc["kill_switch"]       = state.kill_switch_active;
    doc["degradation"]       = state.degradation_active;
    doc["over_current"]      = state.over_current;

    // Current config
    JsonObject cfg = doc.createNestedObject("config");
    cfg["max_duty_pct"]    = SAFETY_MAX_DUTY_CYCLE;
    cfg["max_freq_hz"]     = SAFETY_MAX_FREQ_HZ;
    cfg["watchdog_ms"]     = SAFETY_WATCHDOG_MS;
    cfg["max_polyphony"]   = SAFETY_MAX_POLYPHONY;
    cfg["max_current_ma"]  = SAFETY_MAX_TOTAL_CURRENT_MA;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ============================================================================
// POST handlers — Configuration write
// ============================================================================

void WebServer::handlePostInstrument(AsyncWebServerRequest* request,
                                     uint8_t* data, size_t len) {
    if (!_config) { request->send(500); return; }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    InstrumentConfig inst = {};
    strlcpy(inst.name, doc["name"] | "Instrument", sizeof(inst.name));
    inst.midi_channel       = doc["channel"] | 0;
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
    if (doc.containsKey("index")) {
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

void WebServer::handlePostActuator(AsyncWebServerRequest* request,
                                   uint8_t* data, size_t len) {
    if (!_config) { request->send(500); return; }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    ActuatorConfig act = {};
    act.id           = doc["id"] | 0;
    act.type         = (ActuatorType)(uint8_t)(doc["type"] | 0);
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

    // AUDIT FIX (point B): hold the actuator lock across the add/update, the
    // rest-position init (I²C) and the scheduler resync, so the real-time
    // Scheduler on Core 1 never dereferences a slot that is being written or
    // reshuffled underneath it.
    _config->lockActuators();
    bool added = _config->addActuator(act);
    if (added) {
        ActuatorConfig* actuators = _config->getActuators();
        uint8_t count = _config->getActuatorCount();
        for (uint8_t i = 0; i < count; i++) {
            if (actuators[i].id == act.id) {
                _engine->initActuator(actuators[i]);
                break;
            }
        }
        // Rebuild the scheduler's actuator table from the config array. The
        // previous per-actuator registerActuator() left stale and duplicate
        // pointers after a delete-then-add (the config array shifts on
        // removal), which double-counted current in the SafetyManager and
        // could trip a false overcurrent kill switch.
        if (_scheduler) _scheduler->syncActuators(actuators, count);
    }
    _config->unlockActuators();

    if (added) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(400, "application/json",
                      "{\"error\":\"max actuators reached\"}");
    }
}

void WebServer::handlePostWiFi(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len) {
    if (!_config) { request->send(500); return; }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    WiFiConfig wifi = {};
    strlcpy(wifi.ssid, doc["ssid"] | "", sizeof(wifi.ssid));
    strlcpy(wifi.password, doc["password"] | "", sizeof(wifi.password));
    strlcpy(wifi.hostname, doc["hostname"] | WIFI_DEFAULT_HOSTNAME,
            sizeof(wifi.hostname));
    wifi.enabled     = doc["enabled"] | true;
    wifi.ap_fallback = doc["ap_fallback"] | true;

    _config->setWiFiConfig(wifi);
    request->send(200, "application/json",
                  "{\"ok\":true,\"note\":\"restart to apply WiFi changes\"}");
}

void WebServer::handlePostMidi(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len) {
    if (!_config) { request->send(500); return; }

    StaticJsonDocument<256> doc;
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
    midi.rtp_port         = doc["rtp_port"] | MIDI_RTP_PORT;
    midi.jitter_buffer_ms = doc["jitter_buffer_ms"] | MIDI_JITTER_BUFFER_MS;
    midi.serial_rx_pin    = doc["serial_rx_pin"] | MIDI_SERIAL_RX_PIN;

    _config->setMidiInputConfig(midi);

    // Note: MIDI transports require a restart to apply
    request->send(200, "application/json",
                  "{\"ok\":true,\"note\":\"restart to apply MIDI changes\"}");
}

void WebServer::handlePostRouting(AsyncWebServerRequest* request,
                                  uint8_t* data, size_t len) {
    if (!_config || !_dispatcher) { request->send(500); return; }

    DynamicJsonDocument doc(2048);
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

    // If routing_count < inst_idx+1 (desynchronization), create the missing entries
    while (_config->getRoutingCount() <= inst_idx) {
        MidiRoutingConfig empty = {};
        empty.instrument_index = _config->getRoutingCount();
        _config->addRoutingConfig(empty);
    }

    MidiRoutingConfig* routings = _config->getRoutingConfigs();

    MidiRoutingConfig& routing = routings[inst_idx];

    // Notes
    JsonArray notes = doc["notes"];
    if (!notes.isNull()) {
        routing.note_map_count = 0;
        for (JsonObject nm : notes) {
            if (routing.note_map_count >= MAX_NOTE_MAPPINGS) break;
            NoteMapping& m = routing.note_map[routing.note_map_count];
            m.midi_note         = nm["note"] | 0;
            m.actuator_id       = nm["actuator"] | 0;
            m.behavior_override = nm["behavior"] | 0xFF;
            m.enabled           = nm["enabled"] | true;
            routing.note_map_count++;
        }
    }

    // CCs
    JsonArray ccs = doc["ccs"];
    if (!ccs.isNull()) {
        routing.cc_map_count = 0;
        for (JsonObject cm : ccs) {
            if (routing.cc_map_count >= MAX_CC_MAPPINGS) break;
            CCMapping& c = routing.cc_map[routing.cc_map_count];
            c.cc_number   = cm["cc"] | 0;
            c.actuator_id = cm["actuator"] | 0;
            c.target      = (CCTarget)(uint8_t)(cm["target"] | 0);
            c.range_min   = cm["min"] | 0;
            c.range_max   = cm["max"] | 127;
            c.enabled     = cm["enabled"] | true;
            routing.cc_map_count++;
        }
    }

    // Velocity curve
    JsonArray vel = doc["velocity_curve"];
    if (!vel.isNull()) {
        routing.velocity_curve_count = 0;
        for (JsonObject vp : vel) {
            if (routing.velocity_curve_count >= VELOCITY_CURVE_POINTS) break;
            routing.velocity_curve[routing.velocity_curve_count].input  = vp["in"] | 0;
            routing.velocity_curve[routing.velocity_curve_count].output = vp["out"] | 0;
            routing.velocity_curve_count++;
        }
    }

    // Reload the dispatcher lookup tables
    _dispatcher->refreshConfig();

    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostPowerBudget(AsyncWebServerRequest* request,
                                      uint8_t* data, size_t len) {
    if (!_power) { request->send(500); return; }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    if (doc.containsKey("global_max_ma"))
        _power->setGlobalMaxMA(doc["global_max_ma"]);
    if (doc.containsKey("servo_max_ma"))
        _power->setServoBusMaxMA(doc["servo_max_ma"]);
    if (doc.containsKey("sol_max_ma"))
        _power->setSolenoidBusMaxMA(doc["sol_max_ma"]);
    if (doc.containsKey("max_polyphony"))
        _power->setGlobalMaxPolyphony(doc["max_polyphony"]);

    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostSafety(AsyncWebServerRequest* request,
                                 uint8_t* data, size_t len) {
    if (!_safety) { request->send(500); return; }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    if (doc.containsKey("max_duty_pct"))
        _safety->setMaxDutyCycle(doc["max_duty_pct"]);
    if (doc.containsKey("max_freq_hz"))
        _safety->setMaxFrequency(doc["max_freq_hz"]);
    if (doc.containsKey("watchdog_ms"))
        _safety->setWatchdogTimeout(doc["watchdog_ms"]);
    if (doc.containsKey("max_polyphony"))
        _safety->setMaxPolyphony(doc["max_polyphony"]);
    if (doc.containsKey("max_current_ma"))
        _safety->setMaxTotalCurrent(doc["max_current_ma"]);

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

void WebServer::handlePostDefaults(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!_config) { request->send(500); return; }

    _config->loadDefaults();
    if (_dispatcher) _dispatcher->refreshConfig();
    logger.log(LOG_WARN, CAT_SYSTEM, "Factory reset applied");
    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostTestActuator(AsyncWebServerRequest* request,
                                       uint8_t* data, size_t len) {
    if (!requireAuth(request)) return;
    if (!_scheduler || !_config) { request->send(500); return; }

    StaticJsonDocument<128> doc;
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
    if (!_safety) { request->send(500); return; }

    StaticJsonDocument<64> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
        return;
    }

    bool activate = doc["active"] | false;
    if (activate) {
        _safety->activateKillSwitch();
        logger.log(LOG_CRITICAL, CAT_SAFETY, "Kill switch ACTIVATED from web UI");
    } else {
        _safety->deactivateKillSwitch();
        logger.log(LOG_INFO, CAT_SAFETY, "Kill switch deactivated from web UI");
    }

    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostScanI2C(AsyncWebServerRequest* request) {
    if (!_pca) { request->send(500); return; }

    StaticJsonDocument<256> doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t b = 0; b < 2; b++) {
        uint8_t found = _pca->scanBus(b);
        JsonObject obj = arr.createNestedObject();
        obj["bus"] = b;
        obj["pca_count"] = found;

        BusConfig& bus = _pca->getBusConfig(b);
        JsonArray addrs = obj.createNestedArray("addresses");
        for (uint8_t p = 0; p < bus.pca_count; p++) {
            char hex[5];
            snprintf(hex, sizeof(hex), "0x%02X", bus.pca_addresses[p]);
            addrs.add(hex);
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ============================================================================
// DELETE handlers
// ============================================================================

void WebServer::handleDeleteInstrument(AsyncWebServerRequest* request) {
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
    if (!_config) { request->send(500); return; }

    if (!request->hasParam("id")) {
        request->send(400, "application/json",
                      "{\"error\":\"missing id param\"}");
        return;
    }

    uint8_t id = request->getParam("id")->value().toInt();

    // AUDIT FIX (point B): hold the actuator lock across the rest-position
    // reset (I²C), the array-shifting removeActuator() and the scheduler
    // resync — this is the removal path whose element shift most needs to be
    // mutually exclusive with the Scheduler's Core 1 dereferences.
    _config->lockActuators();

    // Return the actuator to rest before deletion
    ActuatorConfig* actuators = _config->getActuators();
    uint8_t count = _config->getActuatorCount();
    for (uint8_t i = 0; i < count; i++) {
        if (actuators[i].id == id) {
            if (_engine) _engine->resetActuator(actuators[i]);
            break;
        }
    }

    bool removed = _config->removeActuator(id);
    if (removed && _scheduler) {
        // removeActuator() shifts the config array, so the scheduler's
        // pointers (and count) must be rebuilt to avoid pointing at stale or
        // duplicated slots.
        _scheduler->syncActuators(_config->getActuators(),
                                  _config->getActuatorCount());
    }
    _config->unlockActuators();

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
            break;
        case WS_EVT_DATA: {
            // Incoming WebSocket commands (future: virtual piano test)
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len
                && info->opcode == WS_TEXT)
            {
                StaticJsonDocument<128> doc;
                DeserializationError err = deserializeJson(doc, (const char*)data, len);
                if (!err) {
                    const char* cmd = doc["cmd"];
                    if (cmd && strcmp(cmd, "test") == 0) {
                        // Quick actuator test via WS
                        uint8_t act_id   = doc["id"] | 0;
                        uint8_t velocity = doc["vel"] | 100;
                        if (_scheduler) {
                            SchedulerEvent evt = {};
                            evt.trigger_time_us = (uint32_t)esp_timer_get_time();
                            evt.actuator_id = act_id;
                            evt.action = ACTION_NOTE_ON;
                            evt.velocity = velocity;
                            evt.priority = 0;
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
    // AUDIT FIX: size increased to 4096 (active_actuators + power + safety + midi_msgs + log_count)
    StaticJsonDocument<4096> doc;

    doc["uptime_s"] = millis() / 1000;
    doc["heap"]     = ESP.getFreeHeap();

    // Scheduler
    if (_scheduler) {
        JsonObject sched = doc.createNestedObject("scheduler");
        sched["queued"]    = _scheduler->getQueuedEventCount();
        sched["processed"] = _scheduler->getProcessedCount();
        sched["running"]   = _scheduler->isRunning();
    }

    // MIDI Transport
    if (_transport) {
        JsonObject midi = doc.createNestedObject("midi");
        midi["serial_bytes"] = _transport->getSerialByteCount();
        midi["udp_packets"]  = _transport->getUdpPacketCount();
        midi["rtp_packets"]  = _transport->getRtpPacketCount();
    }

    // Dispatcher
    if (_dispatcher) {
        JsonObject disp = doc.createNestedObject("dispatcher");
        disp["dispatched"]    = _dispatcher->getDispatchedCount();
        disp["dropped"]       = _dispatcher->getDroppedCount();
        disp["pwr_rejected"]  = _dispatcher->getPowerRejectedCount();
    }

    // Safety
    if (_safety) {
        JsonObject safe = doc.createNestedObject("safety");
        const SafetyState& ss = _safety->getGlobalState();
        safe["current_ma"]  = ss.total_estimated_current_ma;
        safe["active"]      = ss.active_actuator_count;
        safe["kill_switch"] = ss.kill_switch_active;
        safe["degradation"] = ss.degradation_active;
        safe["over_current"] = ss.over_current;
    }

    // Power
    if (_power) {
        JsonObject pwr = doc.createNestedObject("power");
        const PowerStats& ps = _power->getStats();
        const PowerBudget& pb = _power->getBudget();
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
        JsonArray acts = doc.createNestedArray("active_actuators");
        ActuatorConfig* actuators = _config->getActuators();
        uint8_t count = _config->getActuatorCount();
        for (uint8_t i = 0; i < count; i++) {
            if (actuators[i].state.active) {
                JsonObject a = acts.createNestedObject();
                a["id"]  = actuators[i].id;
                a["pos"] = actuators[i].state.current_position;
            }
        }
    }

    // WiFi
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["connected"] = WiFi.isConnected();
    wifi["rssi"]      = WiFi.RSSI();

    // AUDIT FIX: relay recent MIDI messages via WebSocket
    if (_dispatcher) {
        static const uint8_t MAX_WS_MIDI = 16;
        MidiDispatcher::WsLogEntry entries[MAX_WS_MIDI];
        uint8_t n = _dispatcher->drainWsLog(entries, MAX_WS_MIDI);
        if (n > 0) {
            static const char* SRC_NAMES[] = {"serial", "udp", "rtp"};
            JsonArray msgs = doc.createNestedArray("midi_msgs");
            for (uint8_t i = 0; i < n; i++) {
                JsonObject m = msgs.createNestedObject();
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
    StaticJsonDocument<256> doc;
    doc["id"]       = act.id;
    doc["type"]     = act.type;
    doc["bus_id"]   = act.bus_id;
    doc["enabled"]  = act.enabled;
    doc["active"]   = act.state.active;
    doc["position"] = act.state.current_position;
    serializeJson(doc, output);
}

void WebServer::buildInstrumentJSON(const InstrumentConfig& inst, String& output) {
    StaticJsonDocument<256> doc;
    doc["name"]    = inst.name;
    doc["channel"] = inst.midi_channel;
    doc["enabled"] = inst.enabled;
    doc["count"]   = inst.actuator_count;
    serializeJson(doc, output);
}

void WebServer::buildBusJSON(const BusConfig& bus, String& output) {
    StaticJsonDocument<128> doc;
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
    StaticJsonDocument<256> doc;

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

    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();

    uint8_t count = _calibrator->getResultCount();

    // Iterate over active actuators to retrieve their results
    if (_config) {
        ActuatorConfig* acts     = _config->getActuators();
        uint8_t         act_count = _config->getActuatorCount();

        for (uint8_t i = 0; i < act_count; i++) {
            const CalibrationResult* res =
                _calibrator->getResult(acts[i].id);

            JsonObject obj = arr.createNestedObject();
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

    StaticJsonDocument<128> doc;
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

    StaticJsonDocument<96> doc;
    doc["ok"]      = true;
    doc["applied"] = applied;
    doc["saved"]   = saved;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServer::handlePostCalibrateStop(AsyncWebServerRequest* request) {
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
    StaticJsonDocument<256> doc;

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
    DynamicJsonDocument doc(1536);
    JsonArray arr = doc.to<JsonArray>();

    uint8_t count = _testManager->getLogCount();
    if (count > MAX_ENTRIES) count = MAX_ENTRIES;

    for (uint8_t i = 0; i < count; i++) {
        const TestLogEntry& e = _testManager->getLogEntry(i);
        JsonObject obj = arr.createNestedObject();
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

    StaticJsonDocument<128> doc;
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

    StaticJsonDocument<128> doc;
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

    StaticJsonDocument<64> doc;
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
    if (!_testManager) {
        request->send(503, "application/json",
                      "{\"error\":\"test manager not available\"}");
        return;
    }
    _testManager->stop();
    request->send(200, "application/json", "{\"ok\":true}");
}

void WebServer::handlePostTestClearLog(AsyncWebServerRequest* request) {
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
    logger.clear();
    logger.log(LOG_INFO, CAT_SYSTEM, "System log cleared");
    request->send(200, "application/json", "{\"ok\":true}");
}
