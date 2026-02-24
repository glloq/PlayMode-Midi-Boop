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
#define SCHEDULER_QUEUE_SIZE    64      // Taille file d'attente événements
#define SCHEDULER_MAX_EVENTS    128     // Max événements dans la priority queue

// --- Actionneurs ---
#define MAX_ACTUATORS           32      // Max actionneurs total (2 bus × 4 PCA × 16 ch = 128 théorique)
#define MAX_INSTRUMENTS         8       // Max instruments simultanés

// --- Sécurité ---
#define SAFETY_MAX_DUTY_CYCLE   80      // % max duty cycle par actionneur
#define SAFETY_MAX_FREQ_HZ      50      // Fréquence max de déclenchement
#define SAFETY_WATCHDOG_MS      5000    // Timeout watchdog actionneur

// --- Série ---
#define SERIAL_BAUD_RATE        115200

// --- Config fichier ---
#define CONFIG_FILE_PATH        "/config.json"
#define CONFIG_VERSION          1

#endif // CONFIG_H
