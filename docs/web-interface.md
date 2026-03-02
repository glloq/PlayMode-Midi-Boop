# Web Interface

> See also: [REST API](api-rest.md) — [Architecture](architecture.md) — [Calibration & Tests](calibration-tests.md)

## 1. Main UI Goals

* **Immediate clarity** → user understands what they can do from the main page
* **No-code / adaptable** → easily add/modify actuators, instruments, CC
* **Real-time monitoring** → active notes, latencies, buses, PCA, safety state
* **First-use guidance** → Welcome page with 3-step guide

---

# 2. Interface Structure (implemented)

The UI is a single-page application embedded in PROGMEM (~3000 lines of vanilla HTML/CSS/JS).
Dark GitHub-style theme (#0d1117 bg, #58a6ff accent, #3fb950 success, #f85149 error).

### 2.1 Navigation

3 main tabs + Calibration (conditional) + Settings (gear icon):

| Page | ID | Visible by default | Description |
|------|----|--------------------|-------------|
| **Welcome** | `page-welcome` | If 0 instruments | 3-step guide: Create → Connect → Play |
| **Instrument** | `page-instrument` | Yes (default) | Instrument management + virtual pianos |
| **MIDI** | `page-midi` | Yes | MIDI transports + real-time messages |
| **Actuators** | `page-actuators` | Yes | Actuator table + CC routing |
| **Calibration** | `page-calibration` | No (conditional) | Acoustic calibration + tests |
| **Settings** | `page-settings` | Via gear icon | Monitoring, safety, logs, WiFi, config |

### 2.2 Welcome Page (first-run)

* Automatically shown if no instruments exist
* Midi B∞p logo centered in the header
* 3 illustrated steps: Create → Connect → Play
* Main button "Create my first instrument" → launches 4-step wizard
* Secondary link "skip to manual configuration"
* Disappears once an instrument is created

### 2.3 Instrument Page

* **Instrument list**: table (name, channel, type, actuators, state, actions)
* **Creation**:
  * "Wizard" button → 4-step assistant (identity, type, MIDI notes, summary)
  * "+ Manual" button → full modal
* **Editing**: modal with all parameters
* **Deletion**: confirmation via themed `appConfirm()`
* **Virtual piano(s)**: one interactive keyboard per instrument
  * Visual MIDI notes, touch scrolling
  * Real-time feedback on active notes

### 2.4 MIDI Page

* **MIDI transports**: 3 dedicated cards with toggle + status:
  * MIDI Cable (DIN / TRS) — Serial 31250 baud, GPIO 4
  * WiFi — raw UDP (port 5004)
  * WiFi — Apple / RTP-MIDI (AppleMIDI, synchronized)
* **Jitter buffer**: slider 10–80 ms (default 30 ms)
  * Help text: "Anti-jitter buffer for network MIDI"
* **Received MIDI messages**: real-time table (type, channel, note, velocity, source, timestamp)
  * Pause / clear buttons

### 2.5 Actuators Page

* **Complete table**: ID, type (servo/solenoid), MIDI note, bus, PCA, channel, mode, state
* **Editing**: modal with dynamic parameters based on type and behavior
  * Servo: initial angle, amplitude, speed, angle B, strike direction
  * Solenoid: min/max duration, initial PWM, hold PWM, ramp
* **CC Routing**: Control Changes table per instrument
  * CC number, target actuator, target parameter (position/amplitude/speed/PWM hold)

### 2.6 Calibration Page (conditional)

* Tab hidden by default (`#nav-cal` with `display:none`)
* **Acoustic calibration**: I²S mic INMP441, progress, latency results
* **Actuator tests**:
  * Sweep: sequential scan of all actuators
  * Burst: rapid fire on a specific actuator
  * Stress: simultaneous maximum load
* Test event log (64 entries)

### 2.7 Settings Page

* **Monitoring**: 4 real-time cards
  * MIDI: received/routed/rejected messages
  * Polyphony: progress bar + active/rejected
  * Scheduler: queued/processed events
  * WiFi: IP, mode (STA/AP), signal
* **Polyphony & Safety**:
  * Max polyphony (single shared input)
  * Kill Switch (toggle)
  * Graceful degradation
  * Advanced limits (collapsible): duty cycle, frequency, watchdog, current
* **System Log**:
  * Logs filterable by level (DEBUG, INFO, WARN, ERROR, CRITICAL)
  * Filter by category (System, MIDI, Scheduler, Safety, Power, Calibration, Test)
  * Auto-scroll, clear button
* **WiFi Connection**: SSID, password, hostname, AP fallback
* **I²C Bus** (advanced, collapsible): configurable PWM frequency per bus
* **Configuration**: LittleFS flash save, reset to defaults

---

# 3. Modals and Dialogs

* **`appConfirm()`**: replaces all native `confirm()`
  * Integrated dark theme design
  * Contextual icons, danger/primary support
  * Customizable button text
* **`appAlert()`**: replaces all native `alert()`
  * Same themed style
* **Edit modals**: instrument, actuator, CC mapping
* **Wizard**: multi-step modal with visual progress

---

# 4. Real-time Communication

* **WebSocket** (`/ws`): full state broadcast every 200 ms
  * Scheduler: queue, processed events
  * MIDI: serial/UDP/RTP counters, routed/rejected messages
  * Safety: estimated current, active count, kill switch, degradation
  * Power: used budget (%), servo/solenoid bus
  * Actuators: active states
* **Toasts**: temporary notifications (green success, red error, yellow warning)
* **Piano feedback**: active notes highlighted

---

# 5. Backend REST API

38+ endpoints organized in 4 HTTP verbs (GET, POST, DELETE) + WebSocket.
See [api-rest.md](api-rest.md) for the full endpoint list.

Backend unchanged by UI refactoring — all API routes are identical.

---

# 6. UX / Ergonomics

* **Clean**: 3 main tabs, no nested menus
* **Real-time feedback**: virtual piano + actuator indicators + monitoring cards
* **Guidance**: automatic Welcome page on first boot
* **Modularity**: add instruments / actuators via UI, no recompilation needed
* **Responsiveness**: adaptive interface for mobile / tablet / desktop
* **Colors and visual codes**: active buses, running actuators, safety alerts
* **Dark theme**: comfortable viewing, consistent GitHub-style palette
