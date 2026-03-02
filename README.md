# PlayMode Midi B∞p

**Contrôleur MIDI no-code pour ESP32** — pilote des servos et solénoïdes pour jouer de vrais instruments (percussions, piano mécanique, cloches, xylophone…). Toute la configuration se fait depuis une interface web, sans écrire une seule ligne de code.

## Capacités

* **3 entrées MIDI** simultanées : câble DIN/TRS (Serial), WiFi UDP, WiFi RTP-MIDI (Apple MIDI)
* **128 actionneurs max** (servos + solénoïdes) sur 2 bus I²C indépendants (64 par bus)
* **8 instruments** simultanés, chacun sur son canal MIDI
* **6 comportements** : 4 servo (frappe, alterné, gratter, touche) + 2 solénoïde (frappe, hit-and-hold)
* **Fréquence PWM configurable** par bus — permet d'utiliser 128 servos, 128 solénoïdes, ou un mix des deux
* **Calibration acoustique** automatique via micro I²S (mesure la latence réelle de chaque actionneur)
* **Interface web** embarquée avec piano(s) virtuels, monitoring temps réel, wizard de création
* **Sécurité** intégrée : kill switch matériel, limites courant/fréquence/duty cycle, watchdog, dégradation gracieuse
* **Configuration persistée** en JSON sur flash (LittleFS) — survit aux redémarrages

## Branchements (Pinout ESP32)

```
              ┌──────────────────────┐
              │    ESP32-WROOM-32D   │
              │                      │
   I²C Bus 0 │  GPIO 21 ── SDA      │  4× PCA9685 = 64 sorties PWM
              │  GPIO 22 ── SCL      │  (adresses 0x40–0x43)
              │  GPIO 25 ── OE       │  Kill switch matériel bus 0
              │                      │
   I²C Bus 1 │  GPIO 16 ── SDA      │  4× PCA9685 = 64 sorties PWM
              │  GPIO 17 ── SCL      │  (adresses 0x40–0x43)
              │  GPIO 26 ── OE       │  Kill switch matériel bus 1
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
| I²C0 SDA | 21 | I/O | Bus 0 — pull-up 2.2k–4.7k recommandé |
| I²C0 SCL | 22 | Output | Bus 0 — 400 kHz |
| I²C0 OE | 25 | Output | Output Enable bus 0 (LOW = actif, HIGH = coupé) |
| I²C1 SDA | 16 | I/O | Bus 1 — pull-up 2.2k–4.7k recommandé |
| I²C1 SCL | 17 | Output | Bus 1 — 400 kHz |
| I²C1 OE | 26 | Output | Output Enable bus 1 (LOW = actif, HIGH = coupé) |
| MIDI RX | 4 | Input | Serial2 RX — 31250 baud, circuit opto recommandé |
| I²S WS | 15 | Output | Word Select micro INMP441 (L/R → GND = left) |
| I²S SCK | 14 | Output | Bit Clock micro INMP441 |
| I²S SD | 32 | Input | Data micro INMP441 |
| LED Status | 2 | Output | LED intégrée ESP32 |

### Bus I²C et PCA9685

Chaque bus I²C supporte jusqu'à **4 modules PCA9685** (adresses 0x40 à 0x43), soit 64 sorties PWM par bus et **128 sorties PWM au total**.

| Bus | Fréquence PWM par défaut | OE | Sorties PWM |
|-----|--------------------------|-----|-------------|
| Bus 0 | 50 Hz (servos) | GPIO 25 | 64 (4 PCA × 16 ch) |
| Bus 1 | 200 Hz (solénoïdes) | GPIO 26 | 64 (4 PCA × 16 ch) |

**Fréquence configurable** — La fréquence PWM de chaque bus est modifiable depuis la page Réglages de l'interface web. Par défaut, le bus 0 est à 50 Hz (servos) et le bus 1 à 200 Hz (solénoïdes), mais rien n'empêche de mettre les deux bus à 50 Hz pour piloter **128 servos**, ou les deux à 200 Hz pour **128 solénoïdes**. La fréquence se change à chaud, sans reflasher le firmware.

**Pourquoi 4 PCA max par bus ?** Le PCA9685 communique en I²C à 400 kHz. Chaque mise à jour d'un canal PWM nécessite une transaction I²C (~30 octets). Avec 4 PCA (64 canaux) sur un même bus, une mise à jour complète de tous les canaux prend environ **5 ms** — ce qui reste compatible avec le tick scheduler de 1 ms pour les cas réels (mise à jour partielle). Au-delà de 4 PCA, la latence I²C cumulée dépasserait les contraintes temps réel du scheduler et causerait des retards audibles sur les déclenchements. La limite de 4 est un compromis entre nombre d'actionneurs et précision temporelle.

**OE (Output Enable)** — Chaque bus dispose d'un pin OE qui coupe instantanément toutes les sorties PWM du bus (kill switch matériel). En cas de surintensité ou d'urgence, le Safety Manager peut désactiver un bus entier en une seule opération GPIO.

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
