# Modèle de données

## InstrumentConfig

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

## ActuatorConfig

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

### Comportements servo (4 modes)

| Mode | Enum | Mouvement |
|------|------|-----------|
| **Frappe** | `SERVO_FRAPPE` | A → A+X → retour |
| **Alterné** | `SERVO_ALTERNE` | A → B → A → B |
| **Gratter** | `SERVO_GRATTER` | A → ±X alterné à chaque note |
| **Touche** | `SERVO_TOUCHE` | A → A±δ jusqu'à note off |

### Comportements solénoïde (2 modes)

| Mode | Enum | Fonctionnement |
|------|------|----------------|
| **Frappe** | `SOL_FRAPPE` | Impulsion 5–50 ms selon vélocité |
| **Hit-and-Hold** | `SOL_HIT_AND_HOLD` | PWM initial → rampe → maintien jusqu'à note off |

## Structures de routage MIDI

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

## Sécurité et protection

| Protection | Détail |
|-----------|--------|
| **Kill switch** | Désactive les pins OE des deux bus — coupure matérielle immédiate |
| **Limite courant (Safety)** | Estimation temps réel (servo 250 mA actif, solénoïde 500 mA), max global 5000 mA |
| **Budget énergie (Power)** | Budget global 6000 mA, bus servo 3000 mA, bus solénoïde 4000 mA |
| **Dégradation** | Rejet automatique des événements au seuil de 80% du budget |
| **Polyphonie** | Limite globale (défaut 12) et par instrument (défaut 4), rejet intelligent par vélocité |
| **Duty cycle** | Max 80% par actionneur |
| **Fréquence** | Max 50 Hz de déclenchement par actionneur |
| **Watchdog** | Timeout 5 s par actionneur — coupe automatiquement si bloqué |
| **Logs** | Buffer circulaire 128 entrées, 5 niveaux (DEBUG → CRITICAL), 7 catégories |

## Persistance

Configuration complète en JSON sur LittleFS (ArduinoJson v7, version config : 7) :
* Instruments, actionneurs, routage MIDI (notes + CC + courbes vélocité)
* WiFi (SSID, password, hostname), transports MIDI, jitter buffer
* Budget énergie, polyphonie, limites de sécurité
* Sauvegarde / rollback / réinitialisation via API REST ou UI
