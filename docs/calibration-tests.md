# Calibration et Tests

> Voir aussi : [API REST](api-rest.md) — [Architecture](architecture.md) — [Modèle de données](modele-donnees.md)

---

## 1. Calibration acoustique (Phase 7 — `calibrator.h/.cpp`)

### Principe

Le calibrateur mesure la latence mécanique réelle de chaque actionneur grâce à un microphone I²S (INMP441).
Il compare le moment du déclenchement MIDI avec le moment de l'impact sonore détecté.

### Matériel requis

* Microphone INMP441 connecté en I²S :
  * WS (Word Select) = GPIO 15
  * SCK (Bit Clock) = GPIO 14
  * SD (Data) = GPIO 32
* Sampling : 16 kHz, DMA (8 buffers × 128 samples)

### Procédure par actionneur (automatique)

1. **Mesure bruit ambiant** (`CAL_AMBIENT`) — durée 80 ms
   * Calcul de la valeur absolue moyenne des échantillons
   * Seuil onset = moyenne × 4 (multiplicateur configurable)
   * Seuil minimum absolu : 500 LSB (24 bits)

2. **Flush DMA + déclenchement** (`CAL_TRIGGERING`)
   * Vidange du buffer DMA I²S pour éviter les données périmées
   * Envoi d'un NOTE_ON au scheduler (vélocité 110)

3. **Enregistrement + détection onset** (`CAL_RECORDING`)
   * Lecture streaming des échantillons I²S par chunks de 64 samples
   * Détection du premier sample dépassant le seuil
   * Fenêtre maximum de détection : 350 ms
   * Latence = nombre de samples avant onset / fréquence d'échantillonnage

4. **Pause inter-essais** (`CAL_PAUSING`) — 600 ms entre chaque essai

5. **Moyennage** : 3 mesures par actionneur, résultat = moyenne des mesures valides

### États de la machine

| État | Description |
|------|-------------|
| `CAL_IDLE` | En attente |
| `CAL_AMBIENT` | Mesure bruit ambiant (baseline) |
| `CAL_TRIGGERING` | Flush DMA + déclenchement actionneur |
| `CAL_RECORDING` | Enregistrement + détection onset |
| `CAL_PAUSING` | Pause inter-essais / inter-actionneurs |
| `CAL_COMPLETE` | Calibration terminée avec succès |
| `CAL_ERROR` | Erreur (timeout, pas de son, init échouée) |

### Modes de démarrage

* `startCalibration(actuator_id)` — calibrer un actionneur spécifique
* `startCalibrationAll()` — calibrer tous les actionneurs actifs (file d'attente interne)

### Résultats

Chaque résultat (`CalibrationResult`) contient :
* `actuator_id` — ID de l'actionneur
* `measured_latency_ms` — latence moyenne mesurée (ms)
* `samples_taken` — nombre de mesures valides (sur 3)
* `success` — true si ≥ 1 mesure valide
* `timestamp_ms` — horodatage de complétion

`applyResults()` écrit les latences mesurées directement dans les `ActuatorConfig`.

### API REST associée

| Endpoint | Description |
|----------|-------------|
| `GET /api/calibrate/status` | État de la calibration (state, progress, actuator courant) |
| `GET /api/calibrate/results` | Résultats de calibration (latences mesurées) |
| `POST /api/calibrate` | Lancer une calibration (body JSON : actuator_id ou "all") |
| `POST /api/calibrate/apply` | Appliquer les résultats aux actionneurs |
| `POST /api/calibrate/stop` | Arrêter la calibration en cours |

---

## 2. Tests actionneurs (Phase 8 — `test_manager.h/.cpp`)

### Principe

Le Test Manager pilote des séquences de test sans MIDI externe.
Les événements sont injectés directement dans le Scheduler (bypass MIDI).
Safety Manager et Power Manager restent actifs pendant les tests.

### Trois modes de test

| Mode | Description | Paramètres |
|------|-------------|------------|
| **Sweep** | Parcourt tous les actionneurs actifs un par un | vélocité (défaut 100), intervalle (400 ms), durée hold (120 ms), loop |
| **Burst** | Rafale rapide sur un seul actionneur | actuator_id, nombre de frappes (5), vélocité (100), intervalle (150 ms) |
| **Stress** | Déclenche tous les actionneurs simultanément | vélocité (100), durée hold (120 ms) |

### Journal d'événements

Buffer circulaire de 64 entrées (`TestLogEntry`) :
* `timestamp_ms` — horodatage millis()
* `actuator_id` — ID de l'actionneur
* `velocity` — vélocité envoyée
* `mode` — mode de test (sweep/burst/stress)
* `scheduled` — true si l'événement a été accepté par le scheduler

### API REST associée

| Endpoint | Description |
|----------|-------------|
| `GET /api/test/status` | État du test (mode, progress, actuator courant, events sent) |
| `GET /api/test/log` | Journal des événements de test |
| `POST /api/test/sweep` | Lancer un sweep (body JSON : velocity, interval_ms, hold_ms, loop) |
| `POST /api/test/burst` | Lancer un burst (body JSON : actuator_id, count, velocity, interval_ms) |
| `POST /api/test/stress` | Lancer un stress (body JSON : velocity, hold_ms) |
| `POST /api/test/stop` | Arrêter le test en cours |
| `POST /api/test/log/clear` | Effacer le journal de tests |
| `POST /api/test/actuator` | Tester un actionneur individuellement (body JSON) |

---

## 3. Comportements des actionneurs

### 3.1 Servomoteurs (4 comportements)

| Comportement | Enum | Description | Paramètres à calibrer |
|-------------|------|-------------|----------------------|
| **Frappe** | `SERVO_FRAPPE` | A → A+X → retour | Angle initial (A), amplitude (X), vitesse/durée, sens de frappe |
| **Alterné** | `SERVO_ALTERNE` | A → B → A → B | Angle A, Angle B, vitesse |
| **Gratter** | `SERVO_GRATTER` | A → ±X alterné à chaque note | Angle A, amplitude X, vitesse |
| **Touche** | `SERVO_TOUCHE` | A → A±δ jusqu'à note off | Angle A, delta, vitesse |

### 3.2 Solénoïdes (2 comportements)

| Comportement | Enum | Description | Paramètres à calibrer |
|-------------|------|-------------|----------------------|
| **Frappe** | `SOL_FRAPPE` | Activation 5–50 ms selon vélocité | Durée min/max (ms), scaling vélocité |
| **Hit-and-Hold** | `SOL_HIT_AND_HOLD` | PWM initial → rampe → maintien jusqu'à note off | PWM initial (0-4095), PWM hold (0-4095), rampe (ms) |

---

## 4. Workflow de création d'un instrument

### Via le Wizard (4 étapes)

1. **Identité** — nom de l'instrument + canal MIDI (0=Omni, 1-16)
2. **Type** — sélection type d'actionneurs (servo/solénoïde) + bus I²C
3. **Notes MIDI** — attribution des actionneurs et mapping notes sur piano virtuel
4. **Résumé** — vue récapitulative, confirmation et sauvegarde

### Via création manuelle

Modal complet avec tous les champs :
* Nom, canal MIDI, bus I²C, latence par défaut, calibration auto, activation

### Après création

1. Configurer les actionneurs individuellement (type, comportement, paramètres)
2. Mapper les notes MIDI (table ou piano virtuel)
3. Optionnel : configurer le CC routing (amplitude, vitesse, position, PWM)
4. Tester chaque actionneur (test individuel ou sweep)
5. Optionnel : calibration acoustique (si micro INMP441 connecté)
6. Sauvegarder la configuration (flash LittleFS)

### Persistance

Configuration complète en JSON sur LittleFS (ArduinoJson v7) :
* Instruments : nom, canal MIDI, bus I²C, actionneurs associés
* Actionneurs : type, PCA, canal, comportement, paramètres, latence calibrée
* Routage MIDI : notes, CC, courbes de vélocité
* WiFi : SSID, password, hostname
* MIDI : transports activés, ports, jitter buffer
* Power : budget énergie, polyphonie
* Version de configuration : 7
