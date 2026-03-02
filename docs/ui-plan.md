# UI Refactoring Plan — Simplification for Beginners

> See also: [Web Interface](web-interface.md) for full documentation of the current UI.

## Status: Implemented

---

## Current Architecture: 3 tabs + Calibration (conditional) + Settings

### "Welcome" Page (`page-welcome`, first-run, shown if 0 instruments)
- Welcome screen with Midi B∞p logo centered in the header
- 3 illustrated steps: Create → Connect → Play
- "Create my first instrument" button → launches the 4-step wizard
- Secondary link to manual configuration
- Automatically disappears once an instrument exists

### Page 1: **Instrument** (`page-instrument`, default tab)
- Instrument list (name, channel, type, actuator count, state)
- Create (wizard) / Manual / Edit / Delete buttons
- **Virtual piano(s)**: one interactive keyboard per instrument
- Collapsible "Actuators" inline section
- Collapsible "CC Mapping" inline section

### Page 2: **MIDI** (`page-midi`)
- MIDI transports: Serial / UDP / RTP-MIDI (dedicated card per transport with toggle + status)
- Jitter buffer (slider 10–80 ms, default 30 ms)
- Real-time received MIDI messages (table with pause/clear)

### Page 3: **Actuators** (`page-actuators`)
- Complete table of all actuators (ID, type, note, bus, PCA, channel, mode, state)
- CC Routing: Control Changes table per instrument

### Page 4: **Calibration** (`page-calibration`, conditional tab, hidden by default)
- Acoustic calibration: I²S mic, progress, results
- Actuator testing: sweep, burst, stress, individual/global calibration
- Tab shown only when relevant (via `style="display:none"` on nav button `#nav-cal`)

### Settings (`page-settings`, gear icon)
- **Monitoring**: 4 cards (MIDI, Polyphony with bar + rejected, Scheduler, WiFi)
- **Polyphony & Safety**: max polyphony, Kill Switch, degradation, collapsible advanced limits
- **System Log**: logs filterable by level (DEBUG → CRITICAL) and category (7 categories including Test), auto-scroll
- **WiFi Connection**: SSID, password, hostname, AP fallback
- **I²C Bus** (advanced, collapsible): bus table with configurable PWM frequency
- **Configuration**: flash save, reset to defaults

---

## Implemented Changes

### Navigation
- 10+ pages → 3 main tabs + Calibration (conditional) + Settings (gear icon)
- Welcome page on first boot (0 instruments)
- 4-step creation wizard
- "+ Manual" button for direct creation without wizard

### Settings (redesign)
- Merged Monitoring + Polyphony + Safety sections
- Removed duplicates:
  - "Active" shown once (was 3 times: monitoring, polyphony, safety)
  - "Rejected" shown once (was 2 times: monitoring, polyphony)
  - "Max polyphony": single shared input between polyphony and safety
- 4 Monitoring cards: MIDI, Polyphony (with bar + rejected), Scheduler, WiFi

### Themed Confirm/Alert Modals
- `appConfirm()` replaces all native `confirm()`
- `appAlert()` replaces all native `alert()`
- Design integrated with dark theme with contextual icons
- Danger/primary support, customizable button text

### Header
- B∞P logo centered horizontally via absolute positioning

### Preserved Unchanged
- All REST APIs (38+ endpoints, no backend changes)
- WebSocket and real-time monitoring (200 ms broadcast)
- 4-step wizard (instrument, type, notes, summary)
- Edit modals (instrument, actuator, CC)
- Interactive virtual piano (one per instrument)
- Dark GitHub-style theme (#0d1117 bg, #58a6ff accent)
- Responsive design (mobile/tablet/desktop)
- Notification toasts
- Collapsible sections for advanced settings
