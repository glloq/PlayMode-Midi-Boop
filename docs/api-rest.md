# API REST & WebSocket

38+ endpoints pour contrôle complet du système. Toutes les réponses sont en JSON.

## Lecture (GET)

| Endpoint | Description |
|----------|-------------|
| `GET /api/status` | État global du système (scheduler, MIDI, safety, power, WiFi) |
| `GET /api/instruments` | Liste de tous les instruments configurés |
| `GET /api/actuators` | Liste de tous les actionneurs |
| `GET /api/buses` | État des bus I²C (PCA détectés, fréquences, OE) |
| `GET /api/wifi` | Configuration WiFi (SSID, hostname, mode) |
| `GET /api/midi` | Configuration des transports MIDI |
| `GET /api/routing` | Routage MIDI (notes, CC, courbes vélocité) |
| `GET /api/power` | Budget énergie et statistiques consommation |
| `GET /api/safety` | Limites de sécurité et état courant |
| `GET /api/calibrate/status` | État de la calibration en cours |
| `GET /api/calibrate/results` | Résultats de calibration (latences mesurées) |
| `GET /api/test/status` | État du test en cours (sweep/burst/stress) |
| `GET /api/test/log` | Journal d'événements des tests |
| `GET /api/logs` | Journal système (128 dernières entrées) |

## Écriture configuration (POST JSON)

| Endpoint | Description |
|----------|-------------|
| `POST /api/instrument` | Créer ou modifier un instrument |
| `POST /api/actuator` | Créer ou modifier un actionneur |
| `POST /api/wifi` | Modifier la configuration WiFi |
| `POST /api/midi` | Modifier les transports MIDI |
| `POST /api/routing` | Modifier le routage MIDI (notes, CC) |
| `POST /api/power/budget` | Modifier le budget énergie et polyphonie |
| `POST /api/safety` | Modifier les limites de sécurité |
| `POST /api/bus/pwm` | Modifier la fréquence PWM d'un bus |

## Actions (POST)

| Endpoint | Description |
|----------|-------------|
| `POST /api/killswitch` | Activer/désactiver l'arrêt d'urgence (coupe OE) |
| `POST /api/test/actuator` | Tester un actionneur individuellement |
| `POST /api/scan/i2c` | Scanner les bus I²C (détection PCA9685) |
| `POST /api/config/save` | Sauvegarder la configuration sur flash (LittleFS) |
| `POST /api/config/defaults` | Réinitialiser la configuration par défaut |
| `POST /api/calibrate` | Lancer une calibration acoustique |
| `POST /api/calibrate/apply` | Appliquer les résultats de calibration |
| `POST /api/calibrate/stop` | Arrêter la calibration en cours |
| `POST /api/test/sweep` | Lancer un test sweep (balayage séquentiel) |
| `POST /api/test/burst` | Lancer un test burst (rafale sur un actionneur) |
| `POST /api/test/stress` | Lancer un test stress (charge maximale) |
| `POST /api/test/stop` | Arrêter le test en cours |
| `POST /api/test/log/clear` | Effacer le journal de tests |
| `POST /api/logs/clear` | Effacer le journal système |

## Suppression (DELETE)

| Endpoint | Description |
|----------|-------------|
| `DELETE /api/instrument` | Supprimer un instrument |
| `DELETE /api/actuator` | Supprimer un actionneur |

## WebSocket

**Endpoint** : `/ws`

Broadcast automatique de l'état complet toutes les 200 ms :

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

### Commande WebSocket (client → serveur)

Test rapide d'un actionneur :
```json
{ "cmd": "test", "id": 3, "vel": 100 }
```
