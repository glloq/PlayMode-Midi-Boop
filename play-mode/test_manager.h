#ifndef TEST_MANAGER_H
#define TEST_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "scheduler.h"
#include "config_manager.h"

// ============================================================================
// PlayMode — Industrial Test Manager (Phase 8)
// ============================================================================
//
// Drives test sequences on actuators without external MIDI.
// Three test modes:
//
//   Sweep  : iterates through all active actuators one by one
//   Burst  : rapid burst on a single actuator
//   Stress : triggers all actuators simultaneously (polyphony test)
//
// Events are injected directly into the Scheduler, bypassing MIDI.
// The Safety Manager and Power Manager remain active.
//
// Circular log of the last 64 events for audit.
//

enum TestMode : uint8_t {
    TEST_IDLE    = 0,   // Waiting
    TEST_SWEEP   = 1,   // Sequential scan of all actuators
    TEST_BURST   = 2,   // Burst on one actuator
    TEST_STRESS  = 3,   // All actuators simultaneously
};

// Test event log entry
struct TestLogEntry {
    uint32_t timestamp_ms;      // millis()
    uint8_t  actuator_id;       // Actuator ID
    uint8_t  velocity;          // Velocity sent
    TestMode mode;              // Test mode
    bool     scheduled;         // true = event sent to scheduler
};

class TestManager {
public:
    TestManager(Scheduler& scheduler, ConfigManager& config);

    // --- Start tests ---

    // Sequential sweep: tests each active actuator at regular intervals
    // loop=true → repeat indefinitely until stop()
    bool startSweep(uint8_t velocity      = TEST_DEFAULT_VELOCITY,
                    uint16_t interval_ms  = TEST_DEFAULT_INTERVAL_MS,
                    uint16_t hold_ms      = TEST_DEFAULT_HOLD_MS,
                    bool loop             = false);

    // Burst: N rapid strikes on one actuator
    bool startBurst(uint8_t actuator_id,
                    uint8_t  count        = TEST_BURST_DEFAULT_COUNT,
                    uint8_t  velocity     = TEST_DEFAULT_VELOCITY,
                    uint16_t interval_ms  = TEST_BURST_DEFAULT_INTVL_MS);

    // Stress: triggers all active actuators simultaneously
    bool startStress(uint8_t velocity = TEST_DEFAULT_VELOCITY,
                     uint16_t hold_ms = TEST_DEFAULT_HOLD_MS);

    // Immediate stop
    void stop();

    // --- Main loop — call each loop() ---
    void update();

    // --- State accessors ---
    TestMode getMode()               const;
    bool     isRunning()             const;
    uint8_t  getProgress()           const;  // 0-100 %
    uint8_t  getCurrentActuatorId()  const;
    uint32_t getEventsSent()         const;
    uint32_t getTestsRun()           const;  // Number of completed sweeps/bursts

    // --- Circular log ---
    uint8_t              getLogCount()             const;
    const TestLogEntry&  getLogEntry(uint8_t idx)  const;  // 0 = most recent
    void                 clearLog();

private:
    Scheduler&     _scheduler;
    ConfigManager& _config;

    TestMode _mode;
    bool     _loop;

    // Current parameters
    uint8_t  _velocity;
    uint16_t _interval_ms;
    uint16_t _hold_ms;

    // Sweep state
    uint8_t  _sweep_act_idx;     // Current index in actuator array
    uint32_t _sweep_last_ms;     // millis() of last sweep trigger

    // Burst state
    uint8_t  _burst_act_id;
    uint8_t  _burst_count;
    uint8_t  _burst_remaining;
    uint16_t _burst_interval_ms;
    uint32_t _burst_last_ms;

    // Counters
    uint32_t _events_sent;
    uint32_t _tests_run;

    // Circular log (TEST_LOG_SIZE entries)
    TestLogEntry _log[TEST_LOG_SIZE];
    uint8_t      _log_head;     // Next write position
    uint8_t      _log_count;    // Valid entries (0..TEST_LOG_SIZE)

    // --- Internal methods ---
    bool    triggerActuator(uint8_t act_id);   // Returns false if scheduler is full
    void    scheduleNoteOff(uint8_t act_id, uint16_t delay_ms);
    void    logEvent(uint8_t act_id, uint8_t velocity, bool scheduled);
    uint8_t getEnabledActuatorCount() const;
    bool    getActuatorIdByIndex(uint8_t idx, uint8_t& id_out) const;
};

#endif // TEST_MANAGER_H
