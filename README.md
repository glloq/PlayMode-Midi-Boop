# PlayMode Midi B∞p – No-Code MIDI Controller

Midi B∞p lets you control servos and solenoids to play MIDI notes over a network or Wi‑Fi. The interface is fully no-code and modular: each actuator can be assigned to a single note, making it easy to connect and play a wide range of simple instruments. Designed for fun, creative, and intuitive experimentation, it turns any setup into a playable, networked MIDI instrument.


## 1️⃣ Objectifs généraux

Le projet consiste à créer un **contrôleur MIDI physique automatisé**, capable de piloter des instruments simples (percussions, piano mécanique, cloches, flûtes à action simple) via des **actionneurs (servos, solénoïdes)**, avec :

* **1 actionneur = 1 note MIDI**
* Possibilité de **multi-instruments / multi-canaux**
* Contrôle des **notes et CC**
* **Synchronisation temps réel** et compensation des latences mécaniques
* **Auto-calibration acoustique optionnelle**
* **Interface web no-code** pour configuration et monitoring
* Architecture modulable selon nombre d’actionneurs et instruments

---

## 2️⃣ Architecture matérielle

### 2.1 ESP32-WROOM-32D

* Coeur du système
* 2 cœurs FreeRTOS :

  * Core 0 : WiFi, Web UI, MIDI parsing
  * Core 1 : scheduler temps réel, PCA/I²C, sécurité
* Gestion multi-bus I²C (I2C0 + I2C1)

### 2.2 Bus I²C et PCA9685

* PCA9685 → pilote servos et solénoïdes
* **1 bus par famille d’actionneurs** recommandé :

  * Bus 0 → Servos (50 Hz)
  * Bus 1 → Solénoïdes (200–1000 Hz)
* OE (Output Enable) par bus, pilotable via GPIO, pour activer/désactiver les sorties
* Possibilité d’ajouter jusqu’à 4 PCA supplémentaires par bus
* Pull-up 2.2k–4.7k par bus
* Longueur bus < 50 cm idéal

### 2.3 Actionneurs

* Servos :

  * Note ou CC
  * Modes : frappe, alterné, gratter, touche
* Solénoïdes :

  * Note simple → frappe (5–50 ms)
  * Note sustain → hit-and-hold (PWM continu)
* Chaque actionneur a son **profil latence / vitesse / amplitude**

### 2.4 Micro optionnel INMP441

* Optionnel pour auto-calibration acoustique
* Connecté via I²S
* Détection onset pour calculer latence réelle de chaque actionneur
* Activation configurable depuis UI

---

## 3️⃣ Architecture logicielle

### 3.1 Modules principaux

1. **MIDI Input Layer**

   * RTP-MIDI / UDP / Serial
   * Jitter buffer 20–50 ms
   * Conversion MIDI → events actionneurs

2. **Event Normalizer**

   * Mapping note → actionneur
   * Mapping CC → servo
   * Courbes vélocité
   * Gestion des modes (frappe, alterné…)

3. **Latency Engine**

   * Stock latence spécifique par actionneur
   * Compense les différences pour synchroniser toutes les notes simultanées
   * Intègre auto-calibration si micro actif

4. **Real-Time Scheduler**

   * Cœur critique sur Core 1
   * Priority queue µs pour événements
   * Tick ≤ 1 ms
   * Dispatch vers Actuator Engine
   * Compatible multi-instrument

5. **Actuator Engine**

   * Interface universelle actionneur
   * Gestion des modes : frappe, alterné, gratter, touche, hit-and-hold
   * Mise à jour PWM via PCA9685 (batch write)

6. **Safety Manager**

   * Limites duty cycle / fréquence / température
   * Watchdog actionneur
   * Kill switch global

7. **Power Manager**

   * Estimation consommation
   * Limitation polyphonie si nécessaire
   * Alimentation séparée logique / puissance

8. **Audio Calibration (optionnel)**

   * RMS + onset detection
   * Calcul latence réelle pour chaque actionneur
   * Profil instrument configurable

9. **Config Manager**

   * JSON versionné (SPIFFS / LittleFS)
   * Sauvegarde / rollback / safe defaults
   * Paramètres bus, instruments, actionneurs

10. **Web UI / UX**

    * Interface no-code
    * Épurée et modulable
    * Sections :

      * Instruments (multi-canaux)
      * Actionneurs par instrument
      * Mapping notes MIDI + CC
      * Paramètres bus / PCA / OE
      * Calibration
      * Monitoring temps réel (notes actives, latences, consommation)
      * Visualisation piano(s) avec notes MIDI associées
      * Logs et état sécurité

---

## 4️⃣ Modèle “Instrument” multi-canaux

```cpp
struct Instrument {
    String name;                  // Nom
    uint8_t midi_channel;         // Canal MIDI assigné
    uint8_t bus_id;               // I²C bus dédié
    std::vector<uint16_t> actuators; // Actionneurs associés
    uint16_t default_latency_ms;  // Latence initiale
    bool auto_calibration;        // Active ou non
};
```

* Chaque instrument peut être activé/désactivé
* Possibilité d’ajouter plusieurs instruments sur le même ESP32
* Chaque instrument peut avoir ses propres CC, profil, et auto-calibration
* Scheduler global gère simultanément tous les instruments

---

## 5️⃣ UI/UX No-Code

### Principes

* **Épurée** : pas de menus complexes
* **Interactive** : piano(s) visuels pour chaque instrument
* **Paramétrable** : choix bus, actionneur, profil instrument
* **Temps réel** : état notes et CC visibles instantanément
* **Calibration guidée** : bouton “auto-calibrer” pour chaque instrument

### Composants essentiels

* Tableau instruments / canaux
* Timeline MIDI / latence
* Piano virtuel avec notes associées
* Indicateur de charge bus / PCA
* Boutons de test actionneur individuel
* Mode maintenance / sécurité

---

## 6️⃣ Flux opérationnel

```text
MIDI In → Parser → Normalizer → Latency Engine → Scheduler → Actuator Engine → PCA9685 → Actionneur
```

* Audio calibration en parallèle (si micro actif)
* Safety Manager surveille en continu
* Power Manager limite surconsommation
* Web UI reflète en temps réel tous les états

---

## 7️⃣ Sécurité et robustesse

* OE pin par bus pour coupure rapide des sorties
* Protection contre surcharge solénoïdes / servos
* Watchdog mécanique et PWM
* Mode maintenance et safe boot
* Logs complets : queue, latence, faults, alimentation

---

## 8️⃣ Scalabilité

* Multi-bus I²C (2 bus matériel + PCA multiples)
* Multi-instrument configurable
* Extension future vers multi-ESP32 possible
* Paramétrage full no-code pour adapter nombre d’actionneurs, types, bus et instruments

---

## 9️⃣ Contraintes à respecter

* Scheduler temps réel strict ≤1 ms tick
* Compensation latence indispensable pour notes simultanées
* I²C batch write pour performance
* Séparation alimentation logique / puissance
* UI intuitive et responsive pour tests et réglages
* Prévoir logs et monitoring pour debug et calibration
* Auto-calibration acoustique optionnelle pour synchronisation parfaite

---

## 10️⃣ Étapes de conception recommandées

1. **Base scheduler + actuator engine**
2. **Gestion PCA batch + multi-bus**
3. **MIDI parsing + jitter buffer**
4. **Multi-instrument + mapping notes / CC**
5. **Safety + power manager**
6. **Web UI + piano(s) visuels**
7. **Auto-calibration acoustique**
8. **Test industriel / maintenance mode**
9. **Logs + monitoring temps réel**
10. 


---

✅ **Bénéfices du design proposé**

* Modularité maximale
* Adaptabilité à tous types d’instruments simples
* Interface épurée et no-code friendly
* Prête pour extension multi-instrument et multi-bus
* Temps réel stable et latence compensée
* Sécurité électrique et mécanique assurée


Veux‑tu que je fasse ce schéma ?
