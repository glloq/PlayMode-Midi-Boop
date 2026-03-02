# PlayMode Midi B∞p

> [!WARNING]
> this is a BETA, not tested yet

**No-code MIDI controller for ESP32** — drives servos and solenoids to play real instruments (percussion, mechanical piano, bells, xylophone…). All configuration is done from a web interface, without writing a single line of code.

## Features

* **3 simultaneous MIDI inputs**: DIN/TRS cable (Serial), WiFi UDP, WiFi RTP-MIDI (Apple MIDI)
* **Up to 128 actuators** (servos + solenoids) on 2 independent I²C buses (64 per bus)
* **8 simultaneous instruments**, each on its own MIDI channel
* **6 behaviors**: 4 servo (strike, alternate, strum, key) + 2 solenoid (strike, hit-and-hold)
* **Configurable PWM frequency** per bus — supports 128 servos, 128 solenoids, or any mix
* **Acoustic calibration** via I²S microphone (measures actual latency of each actuator)
* **Embedded web interface** with virtual piano(s), real-time monitoring, creation wizard
* **Built-in safety**: hardware kill switch, current/frequency/duty cycle limits, watchdog, graceful degradation
* **Persistent configuration** in JSON on flash (LittleFS) — survives reboots

## Wiring (ESP32 Pinout)

```
              ┌──────────────────────┐
              │    ESP32-WROOM-32D   │
              │                      │
   I²C Bus 0 │  GPIO 21 ── SDA      │  4× PCA9685 = 64 PWM outputs
              │  GPIO 22 ── SCL      │  (addresses 0x40–0x43)
              │  GPIO 25 ── OE       │  Hardware kill switch bus 0
              │                      │
   I²C Bus 1 │  GPIO 16 ── SDA      │  4× PCA9685 = 64 PWM outputs
              │  GPIO 17 ── SCL      │  (addresses 0x40–0x43)
              │  GPIO 26 ── OE       │  Hardware kill switch bus 1
              │                      │
   MIDI In    │  GPIO  4 ── RX       │  Serial MIDI 31250 baud
              │                      │
   I²S Mic    │  GPIO 15 ── WS       │  INMP441 (optional)
   (optional) │  GPIO 14 ── SCK      │  Acoustic calibration
              │  GPIO 32 ── SD       │  16 kHz sampling
              │                      │
   LED        │  GPIO  2 ── Status   │  Built-in LED
              └──────────────────────┘
```

### Connection Details

| Function | GPIO | Direction | Notes |
|----------|------|-----------|-------|
| I²C0 SDA | 21 | I/O | Bus 0 — 2.2k–4.7k pull-up recommended |
| I²C0 SCL | 22 | Output | Bus 0 — 400 kHz |
| I²C0 OE | 25 | Output | Output Enable bus 0 (LOW = active, HIGH = disabled) |
| I²C1 SDA | 16 | I/O | Bus 1 — 2.2k–4.7k pull-up recommended |
| I²C1 SCL | 17 | Output | Bus 1 — 400 kHz |
| I²C1 OE | 26 | Output | Output Enable bus 1 (LOW = active, HIGH = disabled) |
| MIDI RX | 4 | Input | Serial2 RX — 31250 baud, optocoupler recommended |
| I²S WS | 15 | Output | Word Select INMP441 mic (L/R → GND = left) |
| I²S SCK | 14 | Output | Bit Clock INMP441 mic |
| I²S SD | 32 | Input | Data INMP441 mic |
| Status LED | 2 | Output | ESP32 built-in LED |

### I²C Buses and PCA9685

Each I²C bus supports up to **4 PCA9685 modules** (addresses 0x40 to 0x43), providing 64 PWM outputs per bus and **128 PWM outputs total**.

| Bus | Default PWM Frequency | OE | PWM Outputs |
|-----|----------------------|-----|-------------|
| Bus 0 | 50 Hz (servos) | GPIO 25 | 64 (4 PCA × 16 ch) |
| Bus 1 | 200 Hz (solenoids) | GPIO 26 | 64 (4 PCA × 16 ch) |

**Configurable frequency** — The PWM frequency of each bus can be changed from the Settings page of the web interface. By default, bus 0 is set to 50 Hz (servos) and bus 1 to 200 Hz (solenoids), but nothing prevents setting both buses to 50 Hz to drive **128 servos**, or both to 200 Hz for **128 solenoids**. Frequency changes take effect immediately, without reflashing the firmware.

**Why max 4 PCA per bus?** The PCA9685 communicates via I²C at 400 kHz. Each PWM channel update requires an I²C transaction (~30 bytes). With 4 PCA boards (64 channels) on a single bus, a full update of all channels takes about **5 ms** — which remains compatible with the 1 ms scheduler tick for real-world cases (partial updates). Beyond 4 PCA, cumulative I²C latency would exceed the scheduler's real-time constraints and cause audible triggering delays. The limit of 4 is a trade-off between actuator count and timing precision.

**OE (Output Enable)** — Each bus has an OE pin that instantly disables all PWM outputs on the bus (hardware kill switch). In case of overcurrent or emergency, the Safety Manager can disable an entire bus with a single GPIO operation.

## Getting Started

### Build & Flash

```bash
# PlatformIO (recommended)
pio run -t upload

# Serial monitor
pio device monitor
```

### Connection

1. On first boot, the ESP32 creates a WiFi access point **PlayMode-XXXX**
2. Connect to the hotspot and open `http://192.168.4.1`
3. The home page guides you in 3 steps: **Create → Connect → Play**
4. Configure your WiFi in Settings to switch to Station mode (`http://play-mode.local`)

### Dependencies

| Library | Version | Usage |
|---------|---------|-------|
| ArduinoJson | ^7 | JSON configuration |
| Adafruit PWM Servo Driver | ^2 | PCA9685 driver |
| ESPAsyncWebServer | ^1.2.3 | HTTP + WebSocket server |
| AsyncTCP | ^1.1.1 | Async TCP |
| AppleMIDI | ^3 | RTP-MIDI |

## Documentation

| Document | Contents |
|----------|----------|
| [Architecture](docs/architecture.md) | Modules, data flow, dual-core, boot sequence, main loop |
| [Data Model](docs/data-model.md) | C++ structures (instruments, actuators, MIDI routing), safety |
| [REST API](docs/api-rest.md) | 38+ HTTP endpoints + WebSocket (full list, JSON format) |
| [Web Interface](docs/web-interface.md) | UI navigation, pages, features, design system |
| [Calibration & Tests](docs/calibration-tests.md) | I²S acoustic calibration, sweep/burst/stress tests, workflow |
| [UI Plan](docs/ui-plan.md) | UI refactoring history (3 tabs + Settings) |
