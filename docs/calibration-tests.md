# Calibration and Tests

> See also: [REST API](api-rest.md) — [Architecture](architecture.md) — [Data Model](data-model.md)

---

## 1. Acoustic Calibration (Phase 7 — `calibrator.h/.cpp`)

### Principle

The calibrator measures the actual mechanical latency of each actuator using an I²S microphone (INMP441).
It compares the MIDI trigger moment with the detected sound impact moment.

### Required Hardware

* INMP441 microphone connected via I²S:
  * WS (Word Select) = GPIO 15
  * SCK (Bit Clock) = GPIO 14
  * SD (Data) = GPIO 32
* Sampling: 16 kHz, DMA (8 buffers × 128 samples)

### Per-Actuator Procedure (automatic)

1. **Ambient noise measurement** (`CAL_AMBIENT`) — duration 80 ms
   * Compute mean absolute value of samples
   * Onset threshold = mean × 4 (configurable multiplier)
   * Absolute minimum threshold: 500 LSB (24 bits)

2. **DMA flush + trigger** (`CAL_TRIGGERING`)
   * Drain I²S DMA buffer to avoid stale data
   * Send NOTE_ON to scheduler (velocity 110)

3. **Recording + onset detection** (`CAL_RECORDING`)
   * Streaming read of I²S samples in 64-sample chunks
   * Detect first sample exceeding threshold
   * Maximum detection window: 350 ms
   * Latency = samples before onset / sample rate

4. **Inter-trial pause** (`CAL_PAUSING`) — 600 ms between each trial

5. **Averaging**: 3 measurements per actuator, result = mean of valid measurements

### State Machine

| State | Description |
|-------|-------------|
| `CAL_IDLE` | Idle |
| `CAL_AMBIENT` | Ambient noise measurement (baseline) |
| `CAL_TRIGGERING` | DMA flush + actuator trigger |
| `CAL_RECORDING` | Recording + onset detection |
| `CAL_PAUSING` | Inter-trial / inter-actuator pause |
| `CAL_COMPLETE` | Calibration completed successfully |
| `CAL_ERROR` | Error (timeout, no sound, init failed) |

### Start Modes

* `startCalibration(actuator_id)` — calibrate a specific actuator
* `startCalibrationAll()` — calibrate all enabled actuators (internal queue)

### Results

Each result (`CalibrationResult`) contains:
* `actuator_id` — actuator ID
* `measured_latency_ms` — measured average latency (ms)
* `samples_taken` — number of valid measurements (out of 3)
* `success` — true if ≥ 1 valid measurement
* `timestamp_ms` — completion timestamp

`applyResults()` writes measured latencies directly into `ActuatorConfig`.

### Associated REST API

| Endpoint | Description |
|----------|-------------|
| `GET /api/calibrate/status` | Calibration state (state, progress, current actuator) |
| `GET /api/calibrate/results` | Calibration results (measured latencies) |
| `POST /api/calibrate` | Start a calibration (JSON body: actuator_id or "all") |
| `POST /api/calibrate/apply` | Apply results to actuators |
| `POST /api/calibrate/stop` | Stop current calibration |

---

## 2. Actuator Tests (Phase 8 — `test_manager.h/.cpp`)

### Principle

The Test Manager drives test sequences without external MIDI.
Events are injected directly into the Scheduler (MIDI bypass).
Safety Manager and Power Manager remain active during tests.

### Three Test Modes

| Mode | Description | Parameters |
|------|-------------|------------|
| **Sweep** | Cycles through all enabled actuators one by one | velocity (default 100), interval (400 ms), hold duration (120 ms), loop |
| **Burst** | Rapid fire on a single actuator | actuator_id, strike count (5), velocity (100), interval (150 ms) |
| **Stress** | Triggers all actuators simultaneously | velocity (100), hold duration (120 ms) |

### Event Log

Circular buffer of 64 entries (`TestLogEntry`):
* `timestamp_ms` — millis() timestamp
* `actuator_id` — actuator ID
* `velocity` — sent velocity
* `mode` — test mode (sweep/burst/stress)
* `scheduled` — true if event was accepted by scheduler

### Associated REST API

| Endpoint | Description |
|----------|-------------|
| `GET /api/test/status` | Test state (mode, progress, current actuator, events sent) |
| `GET /api/test/log` | Test event log |
| `POST /api/test/sweep` | Start a sweep (JSON body: velocity, interval_ms, hold_ms, loop) |
| `POST /api/test/burst` | Start a burst (JSON body: actuator_id, count, velocity, interval_ms) |
| `POST /api/test/stress` | Start a stress test (JSON body: velocity, hold_ms) |
| `POST /api/test/stop` | Stop current test |
| `POST /api/test/log/clear` | Clear test log |
| `POST /api/test/actuator` | Test an individual actuator (JSON body) |

---

## 3. Actuator Behaviors

### 3.1 Servomotors (4 behaviors)

| Behavior | Enum | Description | Parameters to calibrate |
|----------|------|-------------|------------------------|
| **Strike** | `SERVO_FRAPPE` | A → A+X → return | Initial angle (A), amplitude (X), speed/duration, strike direction |
| **Alternate** | `SERVO_ALTERNE` | A → B → A → B | Angle A, Angle B, speed |
| **Strum** | `SERVO_GRATTER` | A → ±X alternating on each note | Angle A, amplitude X, speed |
| **Key** | `SERVO_TOUCHE` | A → A±δ until note off | Angle A, delta, speed |

### 3.2 Solenoids (2 behaviors)

| Behavior | Enum | Description | Parameters to calibrate |
|----------|------|-------------|------------------------|
| **Strike** | `SOL_FRAPPE` | 5–50 ms activation based on velocity | Min/max duration (ms), velocity scaling |
| **Hit-and-Hold** | `SOL_HIT_AND_HOLD` | Initial PWM → ramp → hold until note off | Initial PWM (0-4095), hold PWM (0-4095), ramp (ms) |

---

## 4. Instrument Creation Workflow

### Via Wizard (4 steps)

1. **Identity** — instrument name + MIDI channel (0=Omni, 1-16)
2. **Type** — actuator type selection (servo/solenoid) + I²C bus
3. **MIDI Notes** — actuator assignment and note mapping on virtual piano
4. **Summary** — review, confirmation and save

### Via Manual Creation

Full modal with all fields:
* Name, MIDI channel, I²C bus, default latency, auto-calibration, enable/disable

### After Creation

1. Configure actuators individually (type, behavior, parameters)
2. Map MIDI notes (table or virtual piano)
3. Optional: configure CC routing (amplitude, speed, position, PWM)
4. Test each actuator (individual test or sweep)
5. Optional: acoustic calibration (if INMP441 mic connected)
6. Save configuration (LittleFS flash)

### Persistence

Complete configuration in JSON on LittleFS (ArduinoJson v7):
* Instruments: name, MIDI channel, I²C bus, associated actuators
* Actuators: type, PCA, channel, behavior, parameters, calibrated latency
* MIDI routing: notes, CC, velocity curves
* WiFi: SSID, password, hostname
* MIDI: enabled transports, ports, jitter buffer
* Power: energy budget, polyphony
* Configuration version: 7
