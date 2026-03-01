# PlayMode Midi B∞p — Contrôleur MIDI No-Code (v0.9)

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
  * Bus 0 : OE = GPIO 25
  * Bus 1 : OE = GPIO 26
* Pull-up 2.2k–4.7k recommandés, longueur bus < 50 cm

### Actionneurs

* **Servos** — 4 modes de comportement :
  * Frappe (hit), Alterné (toggle), Gratter (scratch), Touche (key-hold)
  * Paramètres : angle repos, amplitude, vitesse, sens de frappe
* **Solénoïdes** — 2 modes :
  * Frappe (impulsion 5–50 ms, durée min/max selon vélocité)
  * Hit-and-hold (PWM initial + rampe vers PWM maintien jusqu'à note off)
* Chaque actionneur a son propre profil : latence mécanique, vitesse, amplitude

### Micro INMP441 (optionnel)

* Connecté en I²S (WS=GPIO15, SCK=GPIO14, SD=GPIO32)
* Sampling 16 kHz, détection d'onset acoustique
* Mesure automatique de la latence réelle de chaque actionneur (3 essais, moyennage)

---

## Architecture logicielle

### Modules (15 modules + entrée principale)

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
| **Log Manager** | `log_manager.h/.cpp` | Buffer circulaire thread-safe (128 entrées), 5 niveaux (DEBUG → CRITICAL), 7 catégories (Système, MIDI, Scheduler, Safety, Power, Calibration, Test) |
| **WiFi Manager** | `wifi_manager.h/.cpp` | Mode STA + fallback AP automatique, mDNS (`hostname.local`), captive portal |
| **Web Server** | `web_server.h/.cpp` | API REST (38+ endpoints) + WebSocket temps réel, ESPAsyncWebServer |
| **Web UI** | `web_ui.h` | Interface HTML/CSS/JS embarquée (PROGMEM), thème sombre, ~3000 lignes |

### Fichiers de configuration et types

| Fichier | Rôle |
|---------|------|
| `config.h` | 150 constantes globales (#define) : GPIO, fréquences, limites, tailles buffers |
| `types.h` | Structures de données partagées : ActuatorConfig, InstrumentConfig, SchedulerEvent, PowerBudget, etc. |
| `midi_types.h` | Types MIDI : MidiMessage, MidiInputConfig, WiFiConfig, énumérations transport/message |

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
    bool enabled;                // Instrument actif
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
    uint16_t pulse_min_ms;       // Durée frappe min (ms) — vélocité 0
    uint16_t pulse_ms;           // Durée frappe max (ms) — vélocité 127
    uint16_t pwm_initial;        // PWM attaque (0-4095)
    uint16_t pwm_hold;           // PWM maintien (0-4095)
    uint16_t ramp_ms;            // Rampe attaque→maintien (ms)

    uint16_t latency_ms;         // Latence mécanique mesurée
    bool enabled;

    ActuatorState state;         // État runtime (non sauvegardé)
};
```

### Structures de routage MIDI

```cpp
struct NoteMapping {             // Mapping note MIDI → actionneur
    uint8_t midi_note;           // Note MIDI (0-127)
    uint8_t actuator_id;         // ID actionneur cible
    uint8_t behavior_override;   // Comportement spécifique (0xFF = défaut actuator)
    bool enabled;
};

struct CCMapping {               // Mapping CC MIDI → paramètre actionneur
    uint8_t cc_number;           // Numéro CC (0-127)
    uint8_t actuator_id;         // ID actionneur cible
    CCTarget target;             // CC_TARGET_POSITION / AMPLITUDE / SPEED / PWM_HOLD
    uint16_t range_min;          // Valeur min de sortie
    uint16_t range_max;          // Valeur max de sortie
    bool enabled;
};

struct VelocityCurvePoint {      // Courbe de vélocité (5 points)
    uint8_t input;               // Vélocité MIDI entrée (0-127)
    uint8_t output;              // Vélocité mappée sortie (0-127)
};
```

---

## Interface Web

### Navigation

L'interface est organisée en **3 onglets principaux** + un onglet **Calibration** conditionnel + une page **Réglages** (engrenage) :

| Page | ID | Contenu |
|------|----|---------|
| **Bienvenue** | `page-welcome` | Affichée si 0 instruments : guide 3 étapes Créer → Connecter → Jouer |
| **Instrument** | `page-instrument` | Liste des instruments, wizard de création 4 étapes, piano(s) virtuels interactifs |
| **MIDI** | `page-midi` | Transports MIDI (Serial/UDP/RTP) avec cartes dédiées, jitter buffer (slider), messages reçus en temps réel |
| **Actionneurs** | `page-actuators` | Table de tous les actionneurs, CC routing par instrument |
| **Calibration** | `page-calibration` | Calibration acoustique (micro I²S), tests sweep/burst/stress — *onglet masqué par défaut, visible si pertinent* |
| **Réglages** ⚙ | `page-settings` | Monitoring (4 cartes), polyphonie & sécurité, journal système filtrable, WiFi, bus I²C, sauvegarde |

### Fonctionnalités UI

* **Page Bienvenue** — affichée au premier démarrage (0 instruments), guide en 3 étapes : Créer → Connecter → Jouer
* **Wizard de création** — assistant 4 étapes : identité, type, notes MIDI, résumé
* **Création manuelle** — formulaire complet avec modal thémé
* **Piano(s) virtuels** — un clavier interactif par instrument, notes MIDI visuelles, scroll tactile
* **Monitoring temps réel** — WebSocket : 4 cartes (MIDI, Polyphonie avec barre, Scheduler, WiFi)
* **Modals thémés** — confirmations et alertes intégrées au thème sombre (`appConfirm()` / `appAlert()`) avec icônes contextuelles, support danger/primary
* **Toasts** — notifications temporaires (succès, erreur, avertissement)
* **Sections dépliables** — réglages avancés masqués par défaut (limites sécurité, bus I²C, hit-and-hold)
* **Journal système** — logs filtrables par niveau (DEBUG → CRITICAL) et par catégorie (7 catégories), auto-scroll
* **Thème sombre** — palette GitHub-style (#0d1117 bg, #58a6ff accent), responsive mobile/tablette/desktop

### LED Status ESP32

| État | Comportement | Signification |
|------|-------------|---------------|
| `LED_BOOT` | Clignotement rapide (100 ms) | Démarrage en cours |
| `LED_AP` | Clignotement lent (500 ms) | Mode Access Point, en attente de configuration |
| `LED_STA` | Allumé fixe | Connecté au WiFi en mode Station |
| `LED_ERROR` | Double flash (2 s) | Erreur critique |
| `LED_OFF` | Éteint | Système arrêté |

### API REST

38+ endpoints pour contrôle complet :

#### Lecture (GET)

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

#### Écriture configuration (POST JSON)

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

#### Actions (POST)

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

#### Suppression (DELETE)

| Endpoint | Description |
|----------|-------------|
| `DELETE /api/instrument` | Supprimer un instrument |
| `DELETE /api/actuator` | Supprimer un actionneur |

#### WebSocket

* `/ws` — broadcast de l'état complet toutes les 200 ms (scheduler, MIDI, safety, power, actionneurs)

---

## Sécurité et protection

| Protection | Détail |
|-----------|--------|
| **Kill switch** | Désactive les pins OE des deux bus — coupure matérielle immédiate |
| **Limite courant (Safety)** | Estimation temps réel (servo 250 mA actif, solénoïde 500 mA), max global 5000 mA |
| **Budget énergie (Power)** | Budget global 6000 mA, bus servo 3000 mA, bus solénoïde 4000 mA |
| **Dégradation** | Rejet automatique des événements au seuil de 80% du budget (4000 mA safety / % power) |
| **Polyphonie** | Limite globale (défaut 12) et par instrument (défaut 4), rejet intelligent par vélocité |
| **Duty cycle** | Max 80% par actionneur |
| **Fréquence** | Max 50 Hz de déclenchement par actionneur |
| **Watchdog** | Timeout 5 s par actionneur — coupe automatiquement si bloqué |
| **Logs** | Buffer circulaire 128 entrées, 5 niveaux (DEBUG, INFO, WARN, ERROR, CRITICAL), 7 catégories |

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
src_dir = play-mode
```

### Dépendances

| Bibliothèque | Version | Usage |
|--------------|---------|-------|
| `bblanchon/ArduinoJson` | ^7 | Sérialisation JSON (config, API REST) |
| `adafruit/Adafruit PWM Servo Driver Library` | ^2 | Pilotage PCA9685 |
| `me-no-dev/ESPAsyncWebServer` | ^1.2.3 | Serveur HTTP async + WebSocket |
| `me-no-dev/AsyncTCP` | ^1.1.1 | TCP async pour ESPAsyncWebServer |
| `lathoub/AppleMIDI` | ^3 | RTP-MIDI / AppleMIDI |

### Flash

```bash
pio run -t upload
```

L'interface web est accessible sur `http://play-mode.local` (ou l'IP de l'ESP32).

---

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

---

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

---

## Scalabilité

* 2 bus I²C × 4 PCA × 16 canaux = **128 sorties PWM** (64 actionneurs actifs max)
* Jusqu'à 8 instruments simultanés, multi-canaux
* Configuration complète no-code depuis le navigateur
* Toute la config persistée en JSON sur LittleFS (version 7)
* Test harness intégré (compilation conditionnelle `ENABLE_TEST_HARNESS`)
