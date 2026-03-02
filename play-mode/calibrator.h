#ifndef CALIBRATOR_H
#define CALIBRATOR_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "types.h"
#include "scheduler.h"
#include "config_manager.h"

// ============================================================================
// PlayMode — Acoustic Calibrator (Phase 7)
// ============================================================================
//
// Measures the actual mechanical latency of each actuator using a
// digital I2S microphone (INMP441 or compatible).
//
// Procedure for each actuator:
//   1. Ambient noise measurement → adaptive detection threshold
//   2. Flush I2S DMA buffer + trigger actuator
//   3. Streaming read of I2S samples post-trigger
//   4. Detection of first onset (|sample| > threshold)
//   5. Latency = samples_before_onset / sample_rate
//   6. Average over CAL_RETRIES valid measurements
//

enum CalibrationState : uint8_t {
    CAL_IDLE       = 0,   // Waiting
    CAL_AMBIENT    = 1,   // Ambient noise measurement (baseline)
    CAL_TRIGGERING = 2,   // Flush DMA + trigger
    CAL_RECORDING  = 3,   // Recording + onset detection
    CAL_PAUSING    = 4,   // Pause between retries / between actuators
    CAL_COMPLETE   = 5,   // Calibration completed successfully
    CAL_ERROR      = 6    // Error (timeout, no sound, init failed)
};

struct CalibrationResult {
    uint8_t  actuator_id;
    uint16_t measured_latency_ms;  // Average measured latency (ms)
    uint8_t  samples_taken;        // Number of valid measurements
    bool     success;              // Calibration succeeded (>=1 valid measurement)
    uint32_t timestamp_ms;         // millis() at time of completion
};

class Calibrator {
public:
    Calibrator(Scheduler& scheduler, ConfigManager& config);

    // --- Lifecycle ---
    bool begin();  // Initializes the I2S driver (call once in setup)
    void stop();   // Immediate stop, return to IDLE

    // --- Start ---
    bool startCalibration(uint8_t actuator_id);  // 1 specific actuator
    bool startCalibrationAll();                   // All active actuators

    // --- Main loop — call each loop() ---
    void update();

    // --- State reading ---
    CalibrationState getState()             const;
    bool             isRunning()            const;
    uint8_t          getProgress()          const;  // 0-100 %
    uint8_t          getCurrentActuatorId() const;
    bool             isMicReady()           const { return _i2s_ready; }

    // --- Results ---
    const CalibrationResult* getResult(uint8_t actuator_id) const;
    uint8_t                  getResultCount()               const;

    // Writes measured latencies into ActuatorConfig
    uint8_t applyResults();

private:
    Scheduler&     _scheduler;
    ConfigManager& _config;

    // State machine
    CalibrationState _state;
    bool             _i2s_ready;

    // Current actuator
    uint8_t  _cur_act_id;
    uint8_t  _cur_try;           // 0 .. CAL_RETRIES-1

    // Timing
    uint32_t _state_enter_us;    // esp_timer_get_time() on state entry
    uint32_t _trigger_time_us;   // NOTE_ON timestamp (esp_timer)

    // Detection threshold (computed during AMBIENT phase)
    int32_t  _onset_threshold;

    // Sample count since flush/trigger
    uint32_t _samples_after_trigger;

    // Accumulation for AMBIENT phase (mean absolute value)
    uint32_t _ambient_abs_sum;
    uint32_t _ambient_sample_count;

    // Latency measurement accumulation
    uint32_t _latency_sum_us;
    uint8_t  _latency_count;

    // Queue for startCalibrationAll() mode
    uint8_t _queue[MAX_ACTUATORS];
    uint8_t _queue_count;
    uint8_t _queue_idx;

    // Results
    CalibrationResult _results[MAX_ACTUATORS];
    uint8_t           _result_count;

    // I2S read buffer (DMA → RAM)
    int32_t  _i2s_buf[CAL_READ_CHUNK];
    uint32_t _chunk_samples;  // Valid samples in _i2s_buf after readChunk()

    // --- Internal methods ---
    bool    initI2S();
    void    deinitI2S();
    bool    probeMic();                // Reads samples to verify that a mic is actually connected
    void    flushI2S();
    bool    readChunk();               // Reads up to CAL_READ_CHUNK samples, returns true if >=1 sample read
    int32_t detectOnset() const;       // Returns index in _i2s_buf or -1
    void    triggerActuator();
    void    finishCurrent();           // Finalizes measurements and moves to next
    bool    advanceToNext();           // Returns true if a next actuator is available
    void    enterState(CalibrationState s);

    CalibrationResult*       findResult(uint8_t actuator_id);
    const CalibrationResult* findResult(uint8_t actuator_id) const;
};

#endif // CALIBRATOR_H
