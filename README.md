# PlayMode Midi B∞p

**Contrôleur MIDI no-code pour ESP32** — pilote des servos et solénoïdes pour jouer de vrais instruments (percussions, piano mécanique, cloches, xylophone…). Toute la configuration se fait depuis une interface web, sans écrire une seule ligne de code.

## Capacités

* **3 entrées MIDI** simultanées : câble DIN/TRS (Serial), WiFi UDP, WiFi RTP-MIDI (Apple MIDI)
* **64 actionneurs max** (servos + solénoïdes) sur 2 bus I²C indépendants
* **8 instruments** simultanés, chacun sur son canal MIDI
* **6 comportements** : 4 servo (frappe, alterné, gratter, touche) + 2 solénoïde (frappe, hit-and-hold)
* **Calibration acoustique** automatique via micro I²S (mesure la latence réelle de chaque actionneur)
* **Interface web** embarquée avec piano(s) virtuels, monitoring temps réel, wizard de création
* **Sécurité** intégrée : kill switch matériel, limites courant/fréquence/duty cycle, watchdog, dégradation gracieuse
* **Configuration persistée** en JSON sur flash (LittleFS) — survit aux redémarrages

## Branchements (Pinout ESP32)

```
              ┌──────────────────────┐
              │    ESP32-WROOM-32D   │
              │                      │
   I²C Bus 0 │  GPIO 21 ── SDA      │  Bus servos (50 Hz)
   (servos)   │  GPIO 22 ── SCL      │  Jusqu'à 4× PCA9685
              │  GPIO 25 ── OE       │  (adresses 0x40-0x43)
              │                      │
   I²C Bus 1 │  GPIO 16 ── SDA      │  Bus solénoïdes (200 Hz)
   (solénoïd) │  GPIO 17 ── SCL      │  Jusqu'à 4× PCA9685
              │  GPIO 26 ── OE       │  (adresses 0x40-0x43)
              │                      │
   MIDI In    │  GPIO  4 ── RX       │  Serial MIDI 31250 baud
              │                      │
   Micro I²S  │  GPIO 15 ── WS       │  INMP441 (optionnel)
   (optionnel) │  GPIO 14 ── SCK      │  Calibration acoustique
              │  GPIO 32 ── SD       │  16 kHz sampling
              │                      │
   LED        │  GPIO  2 ── Status   │  LED intégrée
              └──────────────────────┘
```

### Détail des connexions

| Fonction | GPIO | Direction | Notes |
|----------|------|-----------|-------|
| I²C0 SDA | 21 | I/O | Bus servos — pull-up 2.2k–4.7k recommandé |
| I²C0 SCL | 22 | Output | Bus servos — 400 kHz |
| I²C0 OE | 25 | Output | Output Enable bus 0 (LOW = actif, HIGH = coupé) |
| I²C1 SDA | 16 | I/O | Bus solénoïdes — pull-up 2.2k–4.7k recommandé |
| I²C1 SCL | 17 | Output | Bus solénoïdes — 400 kHz |
| I²C1 OE | 26 | Output | Output Enable bus 1 (LOW = actif, HIGH = coupé) |
| MIDI RX | 4 | Input | Serial2 RX — 31250 baud, circuit opto recommandé |
| I²S WS | 15 | Output | Word Select micro INMP441 (L/R → GND = left) |
| I²S SCK | 14 | Output | Bit Clock micro INMP441 |
| I²S SD | 32 | Input | Data micro INMP441 |
| LED Status | 2 | Output | LED intégrée ESP32 |

### PCA9685

Chaque bus I²C supporte jusqu'à 4 modules PCA9685 (adresses 0x40 à 0x43), soit **16 canaux PWM par module**.

| Bus | Usage | Fréquence PWM | OE | Max actionneurs |
|-----|-------|---------------|-----|-----------------|
| Bus 0 | Servos | 50 Hz | GPIO 25 | 64 (4 PCA × 16 ch) |
| Bus 1 | Solénoïdes | 200 Hz (configurable) | GPIO 26 | 64 (4 PCA × 16 ch) |

L'**OE (Output Enable)** permet une coupure matérielle instantanée de tous les actionneurs d'un bus (kill switch).

## Premiers pas

### Build & Flash

```bash
# PlatformIO (recommandé)
pio run -t upload

# Moniteur série
pio device monitor
```

### Connexion

1. Au premier démarrage, l'ESP32 crée un point d'accès WiFi **PlayMode-XXXX**
2. Connectez-vous au hotspot et ouvrez `http://192.168.4.1`
3. La page d'accueil vous guide en 3 étapes : **Créer → Connecter → Jouer**
4. Configurez votre WiFi dans Réglages pour passer en mode Station (`http://play-mode.local`)

### Dépendances

| Bibliothèque | Version | Usage |
|--------------|---------|-------|
| ArduinoJson | ^7 | Configuration JSON |
| Adafruit PWM Servo Driver | ^2 | Pilotage PCA9685 |
| ESPAsyncWebServer | ^1.2.3 | Serveur HTTP + WebSocket |
| AsyncTCP | ^1.1.1 | TCP async |
| AppleMIDI | ^3 | RTP-MIDI |

## Documentation

| Document | Contenu |
|----------|---------|
| [Architecture](docs/architecture.md) | Modules, flux de données, dual-core, séquence boot, boucle principale |
| [Modèle de données](docs/modele-donnees.md) | Structures C++ (instruments, actionneurs, routage MIDI), sécurité |
| [API REST](docs/api-rest.md) | 38+ endpoints HTTP + WebSocket (liste complète, format JSON) |
| [Interface web](docs/interface-web.md) | Navigation UI, pages, fonctionnalités, design system |
| [Calibration & Tests](docs/calibration-tests.md) | Calibration acoustique I²S, tests sweep/burst/stress, workflow |
| [Plan UI](docs/plan-ui.md) | Historique du refactoring UI (3 onglets + Réglages) |
