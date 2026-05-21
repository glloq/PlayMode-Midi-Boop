#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// PlayMode — Global configuration
// ============================================================================

// --- I²C Bus 0 (Servos, 50 Hz) ---
#define I2C0_SDA_PIN        21
#define I2C0_SCL_PIN        22
#define I2C0_OE_PIN         25      // Output Enable for bus 0
#define I2C0_FREQUENCY      400000  // 400 kHz I²C clock

// --- I²C Bus 1 (Solenoids, 200-1000 Hz) ---
#define I2C1_SDA_PIN        16
#define I2C1_SCL_PIN        17
#define I2C1_OE_PIN         26      // Output Enable for bus 1
#define I2C1_FREQUENCY      400000  // 400 kHz I²C clock

// --- PCA9685 ---
#define PCA_BASE_ADDRESS    0x40    // PCA9685 base address
#define PCA_MAX_PER_BUS     4       // Max 4 PCA per bus
#define PCA_CHANNELS        16      // 16 channels per PCA
#define PCA_SERVO_FREQ      50      // 50 Hz for servos
#define PCA_SOLENOID_FREQ   200     // 200 Hz for solenoids (default)

// --- Servo PWM (for PCA9685 at 50 Hz) ---
#define SERVO_MIN_PULSE_US  500     // Min pulse (0°)
#define SERVO_MAX_PULSE_US  2500    // Max pulse (180°)
#define SERVO_MIN_ANGLE     0
#define SERVO_MAX_ANGLE     180

// --- Solenoid ---
#define SOLENOID_MIN_PULSE_MS   5   // Min strike pulse
#define SOLENOID_MAX_PULSE_MS   50  // Max strike pulse
#define SOLENOID_PWM_MAX        4095 // PWM max PCA9685 (12 bits)

// --- Scheduler ---
#define SCHEDULER_CORE          1       // Core 1 for real-time
#define SCHEDULER_PRIORITY      5       // FreeRTOS priority (high)
#define SCHEDULER_STACK_SIZE    8192    // Stack in bytes
#define SCHEDULER_TICK_MS       1       // Scheduler tick in ms
#define SCHEDULER_QUEUE_SIZE    128     // Event queue size
#define SCHEDULER_MAX_EVENTS    128     // Max events in the priority queue

// --- Actuators ---
#define MAX_ACTUATORS_PER_INSTRUMENT  (PCA_MAX_PER_BUS * PCA_CHANNELS) // 4 PCA × 16 ch = 64
#define MAX_ACTUATORS           128     // Max total actuators (2 bus × 4 PCA × 16 ch)
#define MAX_INSTRUMENTS         8       // Max simultaneous instruments

// --- Safety ---
#define SAFETY_MAX_DUTY_CYCLE   80      // % max duty cycle per actuator
#define SAFETY_MAX_FREQ_HZ      50      // Max trigger frequency
#define SAFETY_WATCHDOG_MS      5000    // Actuator watchdog timeout
#define SAFETY_CHECK_INTERVAL_MS    10      // Safety check frequency (ms)
#define SAFETY_MAX_TOTAL_CURRENT_MA 5000    // Max estimated total current (mA)
#define SAFETY_SERVO_CURRENT_MA     250     // Estimated current per active servo (mA)
#define SAFETY_SOLENOID_CURRENT_MA  500     // Estimated current per active solenoid (mA)
#define SAFETY_SOLENOID_HOLD_MA     150     // Estimated solenoid hold current (mA)
#define SAFETY_MAX_POLYPHONY        12      // Max simultaneously active actuators
#define SAFETY_DEGRADATION_THRESHOLD_MA 4000 // Graceful degradation threshold (mA)
#define SAFETY_DEGRADATION_RELEASE_MA   3500 // Graceful degradation release threshold (hysteresis)
#define SAFETY_KILL_RELEASE_MA          4000 // Kill switch auto-release threshold (hysteresis)
#define SAFETY_KILL_RELEASE_MIN_MS      5000 // Min time before automatic kill switch recovery (ms)

// --- Power Manager (Phase 5) ---
#define POWER_SERVO_BUS_MAX_MA          3000    // Max servo bus budget (mA)
#define POWER_SOLENOID_BUS_MAX_MA       4000    // Max solenoid bus budget (mA)
#define POWER_GLOBAL_MAX_MA             6000    // Total global budget (mA)
#define POWER_SERVO_IDLE_MA             30      // Servo idle current (mA, PCA logic)
#define POWER_SERVO_ACTIVE_MA           250     // Average active servo current (mA)
#define POWER_SOLENOID_FULL_MA          500     // Solenoid full power current (mA)
#define POWER_SOLENOID_HOLD_MA          150     // Solenoid hold current (mA)
#define POWER_UPDATE_INTERVAL_MS        50      // Power stats update interval (ms)
#define POWER_MAX_POLYPHONY             12      // Max global polyphony (default)
#define POWER_DEGRADATION_THRESHOLD_PCT 80      // Budget % threshold for graceful degradation
#define POWER_DEGRADATION_RELEASE_PCT   70      // Release threshold for graceful degradation (hysteresis)

// --- WiFi ---
#define WIFI_DEFAULT_HOSTNAME    "play-mode"
#define WIFI_CONNECT_TIMEOUT_MS  10000   // STA connection timeout (ms)
#define WIFI_RECONNECT_INTERVAL  5000    // Interval between reconnection attempts (ms)
#define WIFI_AP_FALLBACK         true    // Start in AP mode if STA fails

// --- MIDI Input ---
#define MIDI_SERIAL_BAUD         31250   // Standard MIDI baud rate
#define MIDI_SERIAL_RX_PIN       4       // GPIO for Serial MIDI input (Serial2 RX)
#define MIDI_UDP_PORT            5004    // UDP MIDI port
#define MIDI_RTP_PORT            5004    // RTP-MIDI port (AppleMIDI standard)
#define MIDI_JITTER_BUFFER_MS    30      // Default jitter buffer depth (ms)
#define MIDI_JITTER_BUFFER_MAX   100     // Max jitter buffer depth (ms)

// --- Jitter Buffer ---
#define JITTER_BUFFER_SIZE       64      // Max events in the jitter buffer
#define JITTER_BUFFER_MS         30      // Default jitter buffer delay (ms)
#define JITTER_BUFFER_MIN_MS     10      // Minimum configurable delay
#define JITTER_BUFFER_MAX_MS     80      // Maximum configurable delay

// --- Event Normalizer / Mapping ---
#define MAX_NOTE_MAPPINGS       64      // Max note mappings per instrument (sparse, 1 per actuator)
#define MAX_CC_MAPPINGS         32      // Max CC mappings per instrument
#define VELOCITY_CURVE_POINTS   5       // Velocity curve points

// --- MIDI Note Mapping ---
#define MIDI_NOTE_UNMAPPED       0xFF    // Sentinel value for unmapped note

// --- LED Status ---
#define LED_STATUS_PIN          2       // GPIO 2 — ESP32 built-in LED

// --- Serial ---
#define SERIAL_BAUD_RATE        115200

// --- Web Server (Phase 6) ---
#define WEB_SERVER_PORT          80      // Web server HTTP port
#define WEB_WS_BROADCAST_MS      200     // WebSocket stats broadcast interval (ms)
#define WEB_MAX_WS_CLIENTS       4       // Max simultaneous WebSocket clients
#define WEB_MAX_JSON_BODY        4096    // Max accepted size for JSON request bodies (bytes)
#define WEB_AUTH_HEADER          "X-PlayMode-Token" // Header carrying the auth token
#define WEB_AUTH_PARAM           "token" // Query/body parameter accepted as fallback

// --- Acoustic Calibrator (Phase 7) ---
#define CAL_I2S_PORT            0       // ESP32 I²S port (0 or 1)
#define CAL_I2S_WS_PIN          15      // Word Select / LRCK (INMP441 L/R → GND = left)
#define CAL_I2S_SCK_PIN         14      // Bit Clock / BCLK
#define CAL_I2S_SD_PIN          32      // Data In (microphone SD/DOUT)
#define CAL_SAMPLE_RATE         16000   // Sampling frequency (Hz)
#define CAL_DMA_BUF_COUNT       8       // Number of I²S DMA buffers
#define CAL_DMA_BUF_LEN         128     // DMA buffer length (samples)
#define CAL_READ_CHUNK          64      // Samples read per update() call
#define CAL_AMBIENT_MS          80      // Ambient noise measurement duration (ms)
#define CAL_MEASURE_WINDOW_MS   350     // Max post-trigger detection window (ms)
#define CAL_RETRIES             3       // Measurements for averaging per actuator
#define CAL_ONSET_MULTIPLIER    4       // Onset threshold = ambient_mean_abs × mult
#define CAL_ONSET_MIN_THRESHOLD 500     // Minimum absolute threshold (LSB on 24 bits)
#define CAL_INTER_RETRY_MS      600     // Delay between retries (ms)
#define CAL_TRIGGER_VELOCITY    110     // MIDI velocity for calibration strikes

// --- Test Manager (Phase 8) ---
#define TEST_DEFAULT_VELOCITY       100     // Default velocity for tests
#define TEST_DEFAULT_INTERVAL_MS    400     // Inter-actuator sweep interval (ms)
#define TEST_DEFAULT_HOLD_MS        120     // Default activation duration (ms)
#define TEST_BURST_DEFAULT_COUNT    5       // Default burst strike count
#define TEST_BURST_DEFAULT_INTVL_MS 150     // Burst inter-strike interval (ms)
#define TEST_LOG_SIZE               64      // Circular event log size

// --- Log Manager (Phase 9) ---
#define LOG_BUFFER_SIZE         128     // Max entries in the circular log
#define LOG_MAX_MSG_LEN         64      // Max log message length (incl. '\0')

// --- Config file ---
#define CONFIG_FILE_PATH        "/config.json"
#define CONFIG_VERSION          7       // v7: + real-time log manager (phase 9)

#endif // CONFIG_H
