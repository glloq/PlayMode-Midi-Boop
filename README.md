# PlayMode Midi B∞p — Contrôleur MIDI No-Code

Midi B∞p transforme un ESP32 en contrôleur MIDI physique : il pilote des servos et solénoïdes pour jouer des notes sur de vrais instruments (percussions, piano mécanique, cloches, etc.). Chaque actionneur est assigné à une note, configurable depuis une interface web no-code — aucune ligne de code nécessaire.

---

## Architecture matérielle

### ESP32-WROOM-32D

* 2 cœurs FreeRTOS :
  * **Core 0** : WiFi, Web UI, MIDI parsing, Power Manager
  * **Core 1** : Scheduler temps réel, PCA/I²C, Safety Manager
* 2 bus I²C matériels (I2C0 + I2C1), fréquence 400 kHz

### Bus I²C et PCA9685

* Jusqu'à **4 PCA9685 par bus** (adresses 0x40–0x43), soit **64 actionneurs max**
* Fréquences PWM configurables par bus :
  * 50 Hz (servos standard)
  * 200 Hz / 1000 Hz (solénoïdes)
* **OE (Output Enable)** par bus via GPIO — coupure matérielle instantanée (kill switch)
* Pull-up 2.2k–4.7k recommandés, longueur bus < 50 cm

### Actionneurs

* **Servos** — 4 modes de comportement :
  * Frappe (hit), Alterné (toggle), Gratter (scratch), Touche (key-hold)
  * Paramètres : angle repos, amplitude, vitesse, sens de frappe
* **Solénoïdes** — 2 modes :
  * Frappe (impulsion 5–50 ms)
  * Hit-and-hold (PWM initial + rampe vers PWM maintien)
* Chaque actionneur a son propre profil : latence mécanique, vitesse, amplitude

### Micro INMP441 (optionnel)

* Connecté en I²S (WS=GPIO15, SCK=GPIO14, SD=GPIO32)
* Sampling 16 kHz, détection d'onset acoustique
* Mesure automatique de la latence réelle de chaque actionneur

---

## Architecture logicielle

### Modules implémentés

| Module | Fichiers | Rôle |
|--------|----------|------|
| **MIDI Transport** | `midi_transport.h/.cpp` | 3 entrées : Serial (31250 baud, GPIO4), UDP brut (port 5004), RTP-MIDI/AppleMIDI v3 |
| **MIDI Parser** | `midi_parser.h/.cpp` | Parsing byte-by-byte avec running status |
| **Jitter Buffer** | `jitter_buffer.h/.cpp` | Buffer temporel configurable (10–80 ms) pour synchronisation réseau |
| **MIDI Dispatcher** | `midi_dispatcher.h/.cpp` | Routage canal→instrument→note→actionneur, CC routing, courbes vélocité, compensation latence |
| **Scheduler** | `scheduler.h/.cpp` | File de priorité µs sur Core 1 (FreeRTOS, tick ≤ 1 ms), dispatch vers Actuator Engine |
| **Actuator Engine** | `actuator_engine.h/.cpp` | Exécution des comportements servo/solénoïde, mise à jour PWM via PCA9685 |
| **PCA Driver** | `pca_driver.h/.cpp` | Driver multi-bus I²C, scan automatique, batch write |
| **Safety Manager** | `safety_manager.h/.cpp` | Limites duty cycle (80%), fréquence (50 Hz), watchdog (5 s), polyphonie, estimation courant, kill switch, dégradation |
| **Power Manager** | `power_manager.h/.cpp` | Budget énergie global/par-bus/par-instrument, limitation polyphonie, rejet intelligent par vélocité |
| **Calibrator** | `calibrator.h/.cpp` | Calibration acoustique I²S : mesure ambient, trigger, onset detection, moyennage sur 3 essais |
| **Test Manager** | `test_manager.h/.cpp` | Tests actionneurs : sweep, burst, stress, log événements |
| **Config Manager** | `config_manager.h/.cpp` | Persistance JSON sur LittleFS (ArduinoJson), sauvegarde/rollback/défauts |
| **Log Manager** | `log_manager.h/.cpp` | Buffer circulaire thread-safe (128 entrées), 6 catégories (Système, MIDI, Scheduler, Safety, Power, Calibration) |
| **WiFi Manager** | `wifi_manager.h/.cpp` | Mode STA + fallback AP automatique, mDNS (`hostname.local`) |
| **Web Server** | `web_server.h/.cpp` | API REST (25+ endpoints) + WebSocket temps réel, ESPAsyncWebServer |
| **Web UI** | `web_ui.h` | Interface HTML/CSS/JS embarquée (PROGMEM), thème sombre |

### Flux de données

```
MIDI In (Serial/UDP/RTP) → Parser → Jitter Buffer → Dispatcher → Scheduler (Core 1) → Actuator Engine → PCA9685 → Actionneur
                                                                       ↑
                                                          Latency compensation
                                                          (calibration acoustique)
```

* Safety Manager surveille en continu (courant, fréquence, duty cycle, watchdog)
* Power Manager contrôle la polyphonie et rejette les événements si budget dépassé
* WebSocket broadcast l'état toutes les 200 ms

---

## Modèle de données

### InstrumentConfig

```cpp
struct InstrumentConfig {
    char name[32];               // Nom de l'instrument
    uint8_t midi_channel;        // Canal MIDI (0-15)
    uint8_t bus_id;              // Bus I²C dédié
    uint8_t actuator_ids[64];    // IDs des actionneurs associés
    uint8_t midi_notes[64];      // Note MIDI pour chaque actionneur (0xFF = non mappé)
    uint8_t actuator_count;      // Nombre d'actionneurs
    uint16_t default_latency_ms; // Latence par défaut
    bool auto_calibration;       // Calibration auto activée
    bool enabled;
};
```

* Jusqu'à **8 instruments** simultanés sur le même ESP32
* Chaque instrument a son canal MIDI, son bus I²C, ses CC et son profil de calibration
* Le scheduler global gère tous les instruments en parallèle

### ActuatorConfig

```cpp
struct ActuatorConfig {
    uint8_t id;
    ActuatorType type;           // ACT_SERVO ou ACT_SOLENOID
    uint8_t bus_id;              // Bus I²C (0 ou 1)
    uint8_t pca_address;         // Adresse PCA9685 (0x40-0x43)
    uint8_t pca_channel;         // Canal PCA (0-15)
    uint8_t behavior;            // Mode de comportement

    // Servo
    uint16_t angle_initial;      // Angle repos (°)
    uint16_t amplitude;          // Amplitude mouvement (°)
    uint16_t speed_ms;           // Durée mouvement (ms)
    uint16_t angle_b;            // Angle B (mode alterné)
    bool hit_reverse;            // Sens de frappe

    // Solénoïde
    uint16_t pulse_ms;           // Durée frappe (ms)
    uint16_t pwm_initial;        // PWM attaque (0-4095)
    uint16_t pwm_hold;           // PWM maintien (0-4095)
    uint16_t ramp_ms;            // Rampe attaque→maintien (ms)

    uint16_t latency_ms;         // Latence mécanique mesurée
    bool enabled;
};
```

---

## Interface Web

### Navigation

L'interface est organisée en **4 onglets** + une page **Réglages** (engrenage) :

| Page | Contenu |
|------|---------|
| **Instrument** | Liste des instruments, piano(s) virtuels interactifs |
| **MIDI** | Transports MIDI (Serial/UDP/RTP), jitter buffer, messages reçus en temps réel |
| **Actionneurs** | Table de tous les actionneurs, CC routing par instrument |
| **Calibration** | Calibration acoustique (micro I²S), tests sweep/burst |
| **Réglages** ⚙ | Monitoring, polyphonie & sécurité, journal système, WiFi, bus I²C, sauvegarde |

### Fonctionnalités UI

* **Page Bienvenue** — affichée au premier démarrage (0 instruments), guide en 3 étapes : Créer → Connecter → Jouer
* **Wizard de création** — assistant 4 étapes : identité, type, notes MIDI, résumé
* **Piano(s) virtuels** — un clavier interactif par instrument, notes MIDI visuelles, scroll tactile
* **Monitoring temps réel** — WebSocket : polyphonie (barre), MIDI reçus/routés, scheduler, WiFi
* **Modals thémés** — confirmations et alertes intégrées au thème sombre (remplacent les dialogues navigateur)
* **Toasts** — notifications temporaires (succès, erreur, avertissement)
* **Sections dépliables** — réglages avancés masqués par défaut (limites sécurité, bus I²C, hit-and-hold)
* **Thème sombre** — palette GitHub-style, responsive mobile/tablette/desktop

### API REST

25+ endpoints pour contrôle complet :

* `GET/POST/DELETE /api/instrument` — CRUD instruments
* `GET/POST/DELETE /api/actuator` — CRUD actionneurs
* `GET/POST /api/midi` — configuration transports MIDI
* `GET/POST /api/routing` — routage MIDI (notes, CC)
* `GET/POST /api/power/budget` — budget énergie et polyphonie
* `GET/POST /api/safety` — limites de sécurité
* `POST /api/killswitch` — arrêt d'urgence
* `GET/POST /api/calibrate` — calibration acoustique
* `POST /api/test/sweep|burst|stress` — tests actionneurs
* `GET /api/logs` — journal système
* `POST /api/config/save|defaults` — persistance flash

WebSocket sur `/ws` — broadcast état complet toutes les 200 ms.

---

## Sécurité et protection

| Protection | Détail |
|-----------|--------|
| **Kill switch** | Désactive les pins OE des deux bus — coupure matérielle immédiate |
| **Limite courant** | Estimation temps réel (servo 250 mA actif, solénoïde 500 mA), max global 5000 mA |
| **Dégradation** | Rejet automatique des événements au seuil de 4000 mA |
| **Polyphonie** | Limite globale (défaut 12) et par instrument, rejet intelligent par vélocité |
| **Duty cycle** | Max 80% par actionneur |
| **Fréquence** | Max 50 Hz de déclenchement par actionneur |
| **Watchdog** | Timeout 5 s par actionneur — coupe automatiquement si bloqué |
| **Logs** | Buffer circulaire 128 entrées, filtrable par niveau (DEBUG→ERROR) et catégorie |

---

## Build

### Prérequis

* [PlatformIO](https://platformio.org/) (recommandé) ou Arduino IDE
* ESP32-WROOM-32D

### Configuration

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
```

### Dépendances

| Bibliothèque | Usage |
|--------------|-------|
| `bblanchon/ArduinoJson@^7` | Sérialisation JSON (config, API) |
| `adafruit/Adafruit PWM Servo Driver` | Pilotage PCA9685 |
| `me-no-dev/ESPAsyncWebServer@^1.2.3` | Serveur HTTP async + WebSocket |
| `me-no-dev/AsyncTCP@^1.1.1` | TCP async pour ESPAsyncWebServer |
| `lathoub/AppleMIDI@^3` | RTP-MIDI / AppleMIDI |

### Flash

```bash
pio run -t upload
```

L'interface web est accessible sur `http://play-mode.local` (ou l'IP de l'ESP32).

---

## Scalabilité

* 2 bus I²C × 4 PCA × 16 canaux = **128 sorties PWM** (64 actionneurs actifs max)
* Jusqu'à 8 instruments simultanés, multi-canaux
* Configuration complète no-code depuis le navigateur
* Toute la config persistée en JSON sur LittleFS
