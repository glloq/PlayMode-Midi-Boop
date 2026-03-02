# Architecture logicielle

## Modules (15 modules + entrée principale)

| Module | Fichiers | Rôle |
|--------|----------|------|
| **Main** | `play-mode.ino` | Point d'entrée, setup() 12 étapes, loop() Core 0, LED status (5 états), test harness |
| **MIDI Transport** | `midi_transport.h/.cpp` | 3 entrées : Serial (31250 baud, GPIO4), UDP brut (port 5004), RTP-MIDI/AppleMIDI v3 |
| **MIDI Parser** | `midi_parser.h/.cpp` | Parsing byte-by-byte avec running status |
| **Jitter Buffer** | `jitter_buffer.h/.cpp` | Buffer temporel configurable (10–80 ms, défaut 30 ms) pour synchronisation réseau |
| **MIDI Dispatcher** | `midi_dispatcher.h/.cpp` | Routage canal→instrument→note→actionneur, CC routing, courbes vélocité, compensation latence |
| **Scheduler** | `scheduler.h/.cpp` | File de priorité µs sur Core 1 (FreeRTOS, tick ≤ 1 ms, 128 événements max), dispatch vers Actuator Engine |
| **Actuator Engine** | `actuator_engine.h/.cpp` | Exécution des comportements servo/solénoïde, mise à jour PWM via PCA9685 |
| **PCA Driver** | `pca_driver.h/.cpp` | Driver multi-bus I²C, scan automatique, batch write |
| **Safety Manager** | `safety_manager.h/.cpp` | Limites duty cycle (80%), fréquence (50 Hz), watchdog (5 s), estimation courant, kill switch, dégradation |
| **Power Manager** | `power_manager.h/.cpp` | Budget énergie global (6000 mA) / par-bus (servo 3000 mA, solénoïde 4000 mA) / par-instrument, limitation polyphonie, rejet intelligent par vélocité |
| **Calibrator** | `calibrator.h/.cpp` | Calibration acoustique I²S : mesure ambient, trigger, onset detection, moyennage sur 3 essais |
| **Test Manager** | `test_manager.h/.cpp` | Tests actionneurs : sweep, burst, stress, journal d'événements (64 entrées) |
| **Config Manager** | `config_manager.h/.cpp` | Persistance JSON sur LittleFS (ArduinoJson v7), sauvegarde/rollback/défauts |
| **Log Manager** | `log_manager.h/.cpp` | Buffer circulaire thread-safe (128 entrées), 5 niveaux (DEBUG → CRITICAL), 7 catégories |
| **WiFi Manager** | `wifi_manager.h/.cpp` | Mode STA + fallback AP automatique, mDNS (`hostname.local`), captive portal |
| **Web Server** | `web_server.h/.cpp` | API REST (38+ endpoints) + WebSocket temps réel, ESPAsyncWebServer |
| **Web UI** | `web_ui.h` | Interface HTML/CSS/JS embarquée (PROGMEM), thème sombre, ~3000 lignes |

## Fichiers de configuration et types

| Fichier | Rôle |
|---------|------|
| `config.h` | ~150 constantes globales (#define) : GPIO, fréquences, limites, tailles buffers |
| `types.h` | Structures de données partagées : ActuatorConfig, InstrumentConfig, SchedulerEvent, PowerBudget, etc. |
| `midi_types.h` | Types MIDI : MidiMessage, MidiInputConfig, WiFiConfig, énumérations transport/message |

## Flux de données

```
MIDI In (Serial/UDP/RTP) → Parser → Jitter Buffer → Dispatcher → Scheduler (Core 1) → Actuator Engine → PCA9685 → Actionneur
                                                                       ↑
                                                          Latency compensation
                                                          (calibration acoustique)
```

* Safety Manager surveille en continu (courant, fréquence, duty cycle, watchdog)
* Power Manager contrôle la polyphonie et rejette les événements si budget dépassé
* WebSocket broadcast l'état toutes les 200 ms

## Dual-core FreeRTOS

| Core | Rôle | Modules |
|------|------|---------|
| **Core 0** | WiFi, Web, MIDI | WiFi Manager, Web Server, MIDI Transport/Parser, MIDI Dispatcher, Power Manager, Calibrator, Test Manager, Log Manager |
| **Core 1** | Temps réel | Scheduler (priorité 5, 8 KB stack, tick ≤ 1 ms), Actuator Engine, PCA Driver, Safety Manager |

## Séquence d'initialisation (setup)

L'ESP32 exécute 12 étapes au démarrage :

1. **Logger** — journal système (avant tout le reste)
2. **Config Manager** — montage LittleFS, chargement configuration JSON
3. **PCA Driver** — initialisation dual-bus I²C (scan PCA9685)
4. **Actionneurs** — chargement depuis config, enregistrement dans scheduler
5. **Activation bus** — OE = LOW sur les deux bus PCA
6. **Safety Manager** — démarrage surveillance sécurité
7. **Scheduler** — tâche FreeRTOS sur Core 1 (priorité 5, 8 KB stack)
8. **WiFi Manager** — connexion STA avec fallback AP automatique + mDNS
9. **MIDI Transport** — démarrage des 3 entrées MIDI + jitter buffer
10. **Power Manager** — budget énergie, polyphonie, rejet intelligent
11. **Web Server** — API REST + WebSocket (si WiFi actif)
12. **Calibrateur** — microphone I²S (optionnel, si matériel présent)

## Boucle principale (loop — Core 0)

1. Mise à jour LED status
2. Maintenance WiFi (reconnexion, DNS captive portal)
3. Poll des entrées MIDI (remplit le jitter buffer)
4. Dispatch des messages prêts (jitter buffer → dispatcher → scheduler)
5. Mise à jour Power Manager (estimation courant)
6. Mise à jour Calibrateur acoustique
7. Mise à jour Test Manager
8. Détection changements d'état (kill switch, WiFi, dégradation) → log
9. Broadcast WebSocket (toutes les 200 ms)
10. Affichage série (toutes les 5 s)

## LED Status ESP32

| État | Comportement | Signification |
|------|-------------|---------------|
| `LED_BOOT` | Clignotement rapide (100 ms) | Démarrage en cours |
| `LED_AP` | Clignotement lent (500 ms) | Mode AP, en attente de configuration |
| `LED_STA` | Allumé fixe | Connecté au WiFi en mode Station |
| `LED_ERROR` | Double flash (2 s) | Erreur critique |
| `LED_OFF` | Éteint | Système arrêté |
