#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// PlayMode Midi B∞p — Configuration globale
// ============================================================================

// --- I²C Bus 0 (Servos, 50 Hz) ---
#define I2C0_SDA_PIN        21
#define I2C0_SCL_PIN        22
#define I2C0_OE_PIN         25      // Output Enable pour bus 0
#define I2C0_FREQUENCY      400000  // 400 kHz I²C clock

// --- I²C Bus 1 (Solénoïdes, 200-1000 Hz) ---
#define I2C1_SDA_PIN        16
#define I2C1_SCL_PIN        17
#define I2C1_OE_PIN         26      // Output Enable pour bus 1
#define I2C1_FREQUENCY      400000  // 400 kHz I²C clock

// --- PCA9685 ---
#define PCA_BASE_ADDRESS    0x40    // Adresse de base PCA9685
#define PCA_MAX_PER_BUS     4       // Max 4 PCA par bus
#define PCA_CHANNELS        16      // 16 canaux par PCA
#define PCA_SERVO_FREQ      50      // 50 Hz pour servos
#define PCA_SOLENOID_FREQ   200     // 200 Hz pour solénoïdes (défaut)

// --- Servo PWM (pour PCA9685 à 50 Hz) ---
#define SERVO_MIN_PULSE_US  500     // Pulse min (0°)
#define SERVO_MAX_PULSE_US  2500    // Pulse max (180°)
#define SERVO_MIN_ANGLE     0
#define SERVO_MAX_ANGLE     180

// --- Solénoïde ---
#define SOLENOID_MIN_PULSE_MS   5   // Pulse min frappe
#define SOLENOID_MAX_PULSE_MS   50  // Pulse max frappe
#define SOLENOID_PWM_MAX        4095 // PWM max PCA9685 (12 bits)

// --- Scheduler ---
#define SCHEDULER_CORE          1       // Core 1 pour temps réel
#define SCHEDULER_PRIORITY      5       // Priorité FreeRTOS (haute)
#define SCHEDULER_STACK_SIZE    8192    // Stack en bytes
#define SCHEDULER_TICK_MS       1       // Tick scheduler en ms
#define SCHEDULER_QUEUE_SIZE    128     // Taille file d'attente événements
#define SCHEDULER_MAX_EVENTS    128     // Max événements dans la priority queue

// --- Actionneurs ---
#define MAX_ACTUATORS           32      // Max actionneurs total (2 bus × 4 PCA × 16 ch = 128 théorique)
#define MAX_INSTRUMENTS         8       // Max instruments simultanés

// --- Sécurité ---
#define SAFETY_MAX_DUTY_CYCLE   80      // % max duty cycle par actionneur
#define SAFETY_MAX_FREQ_HZ      50      // Fréquence max de déclenchement
#define SAFETY_WATCHDOG_MS      5000    // Timeout watchdog actionneur
#define SAFETY_CHECK_INTERVAL_MS    10      // Fréquence vérification sécurité (ms)
#define SAFETY_MAX_TOTAL_CURRENT_MA 5000    // Courant max total estimé (mA)
#define SAFETY_SERVO_CURRENT_MA     250     // Courant estimé par servo actif (mA)
#define SAFETY_SOLENOID_CURRENT_MA  500     // Courant estimé par solénoïde actif (mA)
#define SAFETY_SOLENOID_HOLD_MA     150     // Courant estimé solénoïde en maintien (mA)
#define SAFETY_MAX_POLYPHONY        12      // Max actionneurs actifs simultanément
#define SAFETY_DEGRADATION_THRESHOLD_MA 4000 // Seuil dégradation gracieuse (mA)

// --- Power Manager (Phase 5) ---
#define POWER_SERVO_BUS_MAX_MA          3000    // Budget max bus servos (mA)
#define POWER_SOLENOID_BUS_MAX_MA       4000    // Budget max bus solénoïdes (mA)
#define POWER_GLOBAL_MAX_MA             6000    // Budget global total (mA)
#define POWER_SERVO_IDLE_MA             30      // Courant servo au repos (mA, logique PCA)
#define POWER_SERVO_ACTIVE_MA           250     // Courant servo actif moyen (mA)
#define POWER_SOLENOID_FULL_MA          500     // Courant solénoïde pleine puissance (mA)
#define POWER_SOLENOID_HOLD_MA          150     // Courant solénoïde en maintien (mA)
#define POWER_UPDATE_INTERVAL_MS        50      // Intervalle mise à jour stats power (ms)
#define POWER_MAX_POLYPHONY             12      // Polyphonie max globale (par défaut)
#define POWER_DEGRADATION_THRESHOLD_PCT 80      // Seuil % budget pour dégradation gracieuse

// --- WiFi ---
#define WIFI_DEFAULT_HOSTNAME    "midi-boop"
#define WIFI_CONNECT_TIMEOUT_MS  10000   // Timeout connexion STA (ms)
#define WIFI_RECONNECT_INTERVAL  5000    // Intervalle entre tentatives reconnexion (ms)
#define WIFI_AP_FALLBACK         true    // Démarrer en AP si STA échoue

// --- MIDI Input ---
#define MIDI_SERIAL_BAUD         31250   // Baud rate MIDI standard
#define MIDI_SERIAL_RX_PIN       4       // GPIO pour entrée Serial MIDI (Serial2 RX)
#define MIDI_UDP_PORT            5004    // Port UDP MIDI
#define MIDI_RTP_PORT            5004    // Port RTP-MIDI (AppleMIDI standard)
#define MIDI_JITTER_BUFFER_MS    30      // Profondeur jitter buffer par défaut (ms)
#define MIDI_JITTER_BUFFER_MAX   100     // Profondeur max jitter buffer (ms)

// --- Jitter Buffer ---
#define JITTER_BUFFER_SIZE       64      // Max événements dans le jitter buffer
#define JITTER_BUFFER_MS         30      // Délai jitter buffer par défaut (ms)
#define JITTER_BUFFER_MIN_MS     10      // Délai minimum configurable
#define JITTER_BUFFER_MAX_MS     80      // Délai maximum configurable

// --- Event Normalizer / Mapping ---
#define MAX_NOTE_MAPPINGS       32      // Max mappings note par instrument (sparse, 1 par actuateur)
#define MAX_CC_MAPPINGS         32      // Max mappings CC par instrument
#define VELOCITY_CURVE_POINTS   5       // Points de la courbe de vélocité

// --- MIDI Note Mapping ---
#define MIDI_NOTE_UNMAPPED       0xFF    // Valeur sentinelle pour note non mappée

// --- LED Status ---
#define LED_STATUS_PIN          2       // GPIO 2 — LED intégrée ESP32

// --- Série ---
#define SERIAL_BAUD_RATE        115200

// --- Web Server (Phase 6) ---
#define WEB_SERVER_PORT          80      // Port HTTP du serveur web
#define WEB_WS_BROADCAST_MS      200     // Intervalle broadcast WebSocket stats (ms)
#define WEB_MAX_WS_CLIENTS       4       // Max clients WebSocket simultanés

// --- Calibrateur Acoustique (Phase 7) ---
#define CAL_I2S_PORT            0       // Port I²S ESP32 (0 ou 1)
#define CAL_I2S_WS_PIN          15      // Word Select / LRCK (INMP441 L/R → GND = left)
#define CAL_I2S_SCK_PIN         14      // Bit Clock / BCLK
#define CAL_I2S_SD_PIN          32      // Data In (SD/DOUT du microphone)
#define CAL_SAMPLE_RATE         16000   // Fréquence d'échantillonnage (Hz)
#define CAL_DMA_BUF_COUNT       8       // Nombre de buffers DMA I²S
#define CAL_DMA_BUF_LEN         128     // Longueur d'un buffer DMA (samples)
#define CAL_READ_CHUNK          64      // Samples lus par appel update()
#define CAL_AMBIENT_MS          80      // Durée mesure bruit ambiant (ms)
#define CAL_MEASURE_WINDOW_MS   350     // Fenêtre max de détection post-trigger (ms)
#define CAL_RETRIES             3       // Mesures pour moyennage par actionneur
#define CAL_ONSET_MULTIPLIER    4       // Seuil onset = ambient_mean_abs × mult
#define CAL_ONSET_MIN_THRESHOLD 500     // Seuil absolu minimum (LSB sur 24 bits)
#define CAL_INTER_RETRY_MS      600     // Délai entre essais (ms)
#define CAL_TRIGGER_VELOCITY    110     // Vélocité MIDI pour les frappes de calibration

// --- Test Manager (Phase 8) ---
#define TEST_DEFAULT_VELOCITY       100     // Vélocité par défaut pour les tests
#define TEST_DEFAULT_INTERVAL_MS    400     // Intervalle sweep inter-actionneurs (ms)
#define TEST_DEFAULT_HOLD_MS        120     // Durée d'activation par défaut (ms)
#define TEST_BURST_DEFAULT_COUNT    5       // Nombre de frappes burst par défaut
#define TEST_BURST_DEFAULT_INTVL_MS 150     // Intervalle inter-frappe burst (ms)
#define TEST_LOG_SIZE               64      // Taille du journal circulaire d'événements

// --- Log Manager (Phase 9) ---
#define LOG_BUFFER_SIZE         128     // Entrées max dans le journal circulaire
#define LOG_MAX_MSG_LEN         64      // Longueur max d'un message log (incl. '\0')

// --- Config fichier ---
#define CONFIG_FILE_PATH        "/config.json"
#define CONFIG_VERSION          7       // v7 : + log manager temps réel (phase 9)

#endif // CONFIG_H
