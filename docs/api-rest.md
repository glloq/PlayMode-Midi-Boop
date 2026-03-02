# REST API & WebSocket

38+ endpoints for complete system control. All responses are in JSON.

## Read (GET)

| Endpoint | Description |
|----------|-------------|
| `GET /api/status` | Global system state (scheduler, MIDI, safety, power, WiFi) |
| `GET /api/instruments` | List of all configured instruments |
| `GET /api/actuators` | List of all actuators |
| `GET /api/buses` | I²C bus state (detected PCAs, frequencies, OE) |
| `GET /api/wifi` | WiFi configuration (SSID, hostname, mode) |
| `GET /api/midi` | MIDI transport configuration |
| `GET /api/routing` | MIDI routing (notes, CC, velocity curves) |
| `GET /api/power` | Energy budget and consumption statistics |
| `GET /api/safety` | Safety limits and current state |
| `GET /api/calibrate/status` | Current calibration state |
| `GET /api/calibrate/results` | Calibration results (measured latencies) |
| `GET /api/test/status` | Current test state (sweep/burst/stress) |
| `GET /api/test/log` | Test event log |
| `GET /api/logs` | System log (last 128 entries) |

## Write Configuration (POST JSON)

| Endpoint | Description |
|----------|-------------|
| `POST /api/instrument` | Create or update an instrument |
| `POST /api/actuator` | Create or update an actuator |
| `POST /api/wifi` | Update WiFi configuration |
| `POST /api/midi` | Update MIDI transports |
| `POST /api/routing` | Update MIDI routing (notes, CC) |
| `POST /api/power/budget` | Update energy budget and polyphony |
| `POST /api/safety` | Update safety limits |
| `POST /api/bus/pwm` | Update PWM frequency for a bus |

## Actions (POST)

| Endpoint | Description |
|----------|-------------|
| `POST /api/killswitch` | Enable/disable emergency stop (cuts OE) |
| `POST /api/test/actuator` | Test an individual actuator |
| `POST /api/scan/i2c` | Scan I²C buses (PCA9685 detection) |
| `POST /api/config/save` | Save configuration to flash (LittleFS) |
| `POST /api/config/defaults` | Reset configuration to defaults |
| `POST /api/calibrate` | Start an acoustic calibration |
| `POST /api/calibrate/apply` | Apply calibration results |
| `POST /api/calibrate/stop` | Stop current calibration |
| `POST /api/test/sweep` | Start a sweep test (sequential scan) |
| `POST /api/test/burst` | Start a burst test (rapid fire on one actuator) |
| `POST /api/test/stress` | Start a stress test (maximum load) |
| `POST /api/test/stop` | Stop current test |
| `POST /api/test/log/clear` | Clear test log |
| `POST /api/logs/clear` | Clear system log |

## Delete (DELETE)

| Endpoint | Description |
|----------|-------------|
| `DELETE /api/instrument` | Delete an instrument |
| `DELETE /api/actuator` | Delete an actuator |

## WebSocket

**Endpoint**: `/ws`

Automatic broadcast of complete state every 200 ms:

```json
{
  "uptime_s": 1234,
  "heap": 180000,
  "scheduler": { "queued": 2, "processed": 1500, "running": true },
  "midi": { "serial_bytes": 0, "udp_packets": 42, "rtp_packets": 0 },
  "dispatcher": { "dispatched": 38, "dropped": 4, "pwr_rejected": 0 },
  "safety": { "current_ma": 750, "active": 3, "kill_switch": false, "degradation": false },
  "power": { "total_ma": 750, "servo_ma": 500, "sol_ma": 250, "budget_pct": 12, "active_count": 3, "max_polyphony": 12 },
  "active_actuators": [{ "id": 2, "pos": 90 }, { "id": 5, "pos": 45 }],
  "wifi": { "connected": true, "rssi": -52 },
  "midi_msgs": [{ "type": "note_on", "ch": 1, "note": 60, "vel": 100, "src": "udp" }],
  "log_count": 42
}
```

### WebSocket Command (client → server)

Quick actuator test:
```json
{ "cmd": "test", "id": 3, "vel": 100 }
```
