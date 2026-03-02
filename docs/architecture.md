# Software Architecture

## Modules (15 modules + main entry point)

| Module | Files | Role |
|--------|-------|------|
| **Main** | `play-mode.ino` | Entry point, setup() 12 steps, loop() Core 0, LED status (5 states), test harness |
| **MIDI Transport** | `midi_transport.h/.cpp` | 3 inputs: Serial (31250 baud, GPIO4), raw UDP (port 5004), RTP-MIDI/AppleMIDI v3 |
| **MIDI Parser** | `midi_parser.h/.cpp` | Byte-by-byte parsing with running status |
| **Jitter Buffer** | `jitter_buffer.h/.cpp` | Configurable temporal buffer (10–80 ms, default 30 ms) for network synchronization |
| **MIDI Dispatcher** | `midi_dispatcher.h/.cpp` | Channel→instrument→note→actuator routing, CC routing, velocity curves, latency compensation |
| **Scheduler** | `scheduler.h/.cpp` | Microsecond priority queue on Core 1 (FreeRTOS, tick ≤ 1 ms, 128 events max), dispatch to Actuator Engine |
| **Actuator Engine** | `actuator_engine.h/.cpp` | Servo/solenoid behavior execution, PWM updates via PCA9685 |
| **PCA Driver** | `pca_driver.h/.cpp` | Multi-bus I²C driver, auto-scan, batch write |
| **Safety Manager** | `safety_manager.h/.cpp` | Duty cycle limits (80%), frequency (50 Hz), watchdog (5 s), current estimation, kill switch, degradation |
| **Power Manager** | `power_manager.h/.cpp` | Global energy budget (6000 mA) / per-bus (servo 3000 mA, solenoid 4000 mA) / per-instrument, polyphony limiting, smart velocity-based rejection |
| **Calibrator** | `calibrator.h/.cpp` | I²S acoustic calibration: ambient measurement, trigger, onset detection, averaging over 3 trials |
| **Test Manager** | `test_manager.h/.cpp` | Actuator tests: sweep, burst, stress, event log (64 entries) |
| **Config Manager** | `config_manager.h/.cpp` | JSON persistence on LittleFS (ArduinoJson v7), save/rollback/defaults |
| **Log Manager** | `log_manager.h/.cpp` | Thread-safe circular buffer (128 entries), 5 levels (DEBUG → CRITICAL), 7 categories |
| **WiFi Manager** | `wifi_manager.h/.cpp` | STA mode + automatic AP fallback, mDNS (`hostname.local`), captive portal |
| **Web Server** | `web_server.h/.cpp` | REST API (38+ endpoints) + real-time WebSocket, ESPAsyncWebServer |
| **Web UI** | `web_ui.h` | Embedded HTML/CSS/JS interface (PROGMEM), dark theme, ~3000 lines |

## Configuration and Type Files

| File | Role |
|------|------|
| `config.h` | ~150 global constants (#define): GPIO, frequencies, limits, buffer sizes |
| `types.h` | Shared data structures: ActuatorConfig, InstrumentConfig, SchedulerEvent, PowerBudget, etc. |
| `midi_types.h` | MIDI types: MidiMessage, MidiInputConfig, WiFiConfig, transport/message enumerations |

## Data Flow

```
MIDI In (Serial/UDP/RTP) → Parser → Jitter Buffer → Dispatcher → Scheduler (Core 1) → Actuator Engine → PCA9685 → Actuator
                                                                       ↑
                                                          Latency compensation
                                                          (acoustic calibration)
```

* Safety Manager continuously monitors (current, frequency, duty cycle, watchdog)
* Power Manager controls polyphony and rejects events when budget is exceeded
* WebSocket broadcasts state every 200 ms

## Dual-core FreeRTOS

| Core | Role | Modules |
|------|------|---------|
| **Core 0** | WiFi, Web, MIDI | WiFi Manager, Web Server, MIDI Transport/Parser, MIDI Dispatcher, Power Manager, Calibrator, Test Manager, Log Manager |
| **Core 1** | Real-time | Scheduler (priority 5, 8 KB stack, tick ≤ 1 ms), Actuator Engine, PCA Driver, Safety Manager |

## Initialization Sequence (setup)

The ESP32 executes 12 steps at startup:

1. **Logger** — system log (before everything else)
2. **Config Manager** — mount LittleFS, load JSON configuration
3. **PCA Driver** — dual-bus I²C initialization (PCA9685 scan)
4. **Actuators** — load from config, register in scheduler
5. **Bus activation** — OE = LOW on both PCA buses
6. **Safety Manager** — start safety monitoring
7. **Scheduler** — FreeRTOS task on Core 1 (priority 5, 8 KB stack)
8. **WiFi Manager** — STA connection with automatic AP fallback + mDNS
9. **MIDI Transport** — start all 3 MIDI inputs + jitter buffer
10. **Power Manager** — energy budget, polyphony, smart rejection
11. **Web Server** — REST API + WebSocket (if WiFi active)
12. **Calibrator** — I²S microphone (optional, if hardware present)

## Main Loop (loop — Core 0)

1. Update LED status
2. WiFi maintenance (reconnection, captive portal DNS)
3. Poll MIDI inputs (fill jitter buffer)
4. Dispatch ready messages (jitter buffer → dispatcher → scheduler)
5. Update Power Manager (current estimation)
6. Update Acoustic Calibrator
7. Update Test Manager
8. Detect state changes (kill switch, WiFi, degradation) → log
9. WebSocket broadcast (every 200 ms)
10. Serial output (every 5 s)

## ESP32 LED Status

| State | Behavior | Meaning |
|-------|----------|---------|
| `LED_BOOT` | Fast blink (100 ms) | Boot in progress |
| `LED_AP` | Slow blink (500 ms) | AP mode, waiting for configuration |
| `LED_STA` | Solid on | Connected to WiFi in Station mode |
| `LED_ERROR` | Double flash (2 s) | Critical error |
| `LED_OFF` | Off | System stopped |
