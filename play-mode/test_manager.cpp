#include "test_manager.h"
#include <esp_timer.h>
#include <string.h>

// ============================================================================
// PlayMode — Industrial Test Manager (implementation)
// ============================================================================

TestManager::TestManager(Scheduler& scheduler, ConfigManager& config)
    : _scheduler(scheduler)
    , _config(config)
    , _mode(TEST_IDLE)
    , _loop(false)
    , _velocity(TEST_DEFAULT_VELOCITY)
    , _interval_ms(TEST_DEFAULT_INTERVAL_MS)
    , _hold_ms(TEST_DEFAULT_HOLD_MS)
    , _sweep_act_idx(0)
    , _sweep_last_ms(0)
    , _burst_act_id(0)
    , _burst_count(TEST_BURST_DEFAULT_COUNT)
    , _burst_remaining(0)
    , _burst_interval_ms(TEST_BURST_DEFAULT_INTVL_MS)
    , _burst_last_ms(0)
    , _events_sent(0)
    , _tests_run(0)
    , _log_head(0)
    , _log_count(0)
{
    memset(_log, 0, sizeof(_log));
}

// ============================================================================
// Start
// ============================================================================

bool TestManager::startSweep(uint8_t velocity, uint16_t interval_ms,
                              uint16_t hold_ms, bool loop) {
    if (_mode != TEST_IDLE) return false;
    if (getEnabledActuatorCount() == 0) return false;

    _velocity     = velocity;
    _interval_ms  = interval_ms;
    _hold_ms      = hold_ms;
    _loop         = loop;
    _sweep_act_idx = 0;
    _sweep_last_ms = millis();

    _mode = TEST_SWEEP;

    Serial.printf("[TEST] Sweep started: vel=%d, interval=%dms, hold=%dms, loop=%s\n",
                  velocity, interval_ms, hold_ms, loop ? "yes" : "no");
    return true;
}

bool TestManager::startBurst(uint8_t actuator_id, uint8_t count,
                              uint8_t velocity, uint16_t interval_ms) {
    if (_mode != TEST_IDLE) return false;

    // Verify that the actuator exists
    bool found = false;
    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         total = _config.getActuatorCount();
    for (uint8_t i = 0; i < total; i++) {
        if (acts[i].id == actuator_id && acts[i].enabled) { found = true; break; }
    }
    if (!found) return false;

    _burst_act_id      = actuator_id;
    _burst_count       = count;
    _burst_remaining   = count;
    _velocity          = velocity;
    _burst_interval_ms = interval_ms;
    _burst_last_ms     = millis();
    _loop              = false;

    _mode = TEST_BURST;

    Serial.printf("[TEST] Burst started: act=%d, count=%d, vel=%d, interval=%dms\n",
                  actuator_id, count, velocity, interval_ms);
    return true;
}

bool TestManager::startStress(uint8_t velocity, uint16_t hold_ms) {
    if (_mode != TEST_IDLE) return false;
    if (getEnabledActuatorCount() == 0) return false;

    _velocity = velocity;
    _hold_ms  = hold_ms;
    _mode     = TEST_STRESS;

    Serial.printf("[TEST] Stress test : vel=%d, hold=%dms\n", velocity, hold_ms);

    // Trigger all actuators immediately
    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         total = _config.getActuatorCount();

    for (uint8_t i = 0; i < total; i++) {
        if (!acts[i].enabled) continue;
        triggerActuator(acts[i].id);
    }

    _tests_run++;
    _mode = TEST_IDLE;  // Stress is instantaneous
    Serial.printf("[TEST] Stress complete — %d actuators triggered\n",
                  getEnabledActuatorCount());
    return true;
}

void TestManager::stop() {
    _mode = TEST_IDLE;
    _loop = false;
    Serial.println("[TEST] Test stopped");
}

// ============================================================================
// update() — state machine
// ============================================================================

void TestManager::update() {
    if (_mode == TEST_IDLE || _mode == TEST_STRESS) return;

    uint32_t now = millis();

    switch (_mode) {

    // -------------------------------------------------------------------------
    case TEST_SWEEP: {
        if (now - _sweep_last_ms < _interval_ms) break;
        _sweep_last_ms = now;

        uint8_t act_id;
        if (!getActuatorIdByIndex(_sweep_act_idx, act_id)) {
            // Invalid index — end of sweep
            _tests_run++;
            Serial.printf("[TEST] Sweep #%lu complete (%d actuators)\n",
                          (unsigned long)_tests_run,
                          (int)_sweep_act_idx);

            if (_loop) {
                _sweep_act_idx = 0;  // Restart
            } else {
                _mode = TEST_IDLE;
            }
            break;
        }

        triggerActuator(act_id);
        _sweep_act_idx++;

        // Check if we reached the end
        if (!getActuatorIdByIndex(_sweep_act_idx, act_id)) {
            _tests_run++;
            Serial.printf("[TEST] Sweep #%lu complete (%d actuators)\n",
                          (unsigned long)_tests_run,
                          (int)_sweep_act_idx);
            if (_loop) {
                _sweep_act_idx = 0;
            } else {
                _mode = TEST_IDLE;
            }
        }
        break;
    }

    // -------------------------------------------------------------------------
    case TEST_BURST: {
        if (_burst_remaining == 0) {
            _tests_run++;
            Serial.printf("[TEST] Burst complete (%d strikes on act %d)\n",
                          (int)_burst_count, (int)_burst_act_id);
            _mode = TEST_IDLE;
            break;
        }

        if (now - _burst_last_ms < _burst_interval_ms) break;
        _burst_last_ms = now;

        triggerActuator(_burst_act_id);
        _burst_remaining--;
        break;
    }

    default:
        break;
    }
}

// ============================================================================
// Helpers
// ============================================================================

bool TestManager::triggerActuator(uint8_t act_id) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    SchedulerEvent on_evt = {};
    on_evt.trigger_time_us = now_us;
    on_evt.actuator_id     = act_id;
    on_evt.action          = ACTION_NOTE_ON;
    on_evt.velocity        = _velocity;
    on_evt.priority        = 0;

    bool ok = _scheduler.pushEvent(on_evt);

    if (ok) {
        scheduleNoteOff(act_id, _hold_ms);
        _events_sent++;
    }

    logEvent(act_id, _velocity, ok);
    return ok;
}

void TestManager::scheduleNoteOff(uint8_t act_id, uint16_t delay_ms) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    SchedulerEvent off_evt = {};
    off_evt.trigger_time_us = now_us + (uint32_t)delay_ms * 1000UL;
    off_evt.actuator_id     = act_id;
    off_evt.action          = ACTION_NOTE_OFF;
    off_evt.velocity        = 0;
    off_evt.priority        = 0;

    _scheduler.pushEvent(off_evt);
}

void TestManager::logEvent(uint8_t act_id, uint8_t velocity, bool scheduled) {
    TestLogEntry& entry = _log[_log_head];
    entry.timestamp_ms = millis();
    entry.actuator_id  = act_id;
    entry.velocity     = velocity;
    entry.mode         = _mode;
    entry.scheduled    = scheduled;

    _log_head = (_log_head + 1) % TEST_LOG_SIZE;
    if (_log_count < TEST_LOG_SIZE) _log_count++;
}

uint8_t TestManager::getEnabledActuatorCount() const {
    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         total = _config.getActuatorCount();
    uint8_t count = 0;
    for (uint8_t i = 0; i < total; i++) {
        if (acts[i].enabled) count++;
    }
    return count;
}

bool TestManager::getActuatorIdByIndex(uint8_t idx, uint8_t& id_out) const {
    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         total = _config.getActuatorCount();
    uint8_t found = 0;
    for (uint8_t i = 0; i < total; i++) {
        if (!acts[i].enabled) continue;
        if (found == idx) {
            id_out = acts[i].id;
            return true;
        }
        found++;
    }
    return false;
}

// ============================================================================
// Accessors
// ============================================================================

TestMode TestManager::getMode()              const { return _mode; }
bool     TestManager::isRunning()            const { return _mode != TEST_IDLE; }
uint32_t TestManager::getEventsSent()        const { return _events_sent; }
uint32_t TestManager::getTestsRun()          const { return _tests_run; }
uint8_t  TestManager::getLogCount()          const { return _log_count; }

uint8_t TestManager::getCurrentActuatorId() const {
    switch (_mode) {
        case TEST_SWEEP: {
            uint8_t id;
            if (getActuatorIdByIndex(_sweep_act_idx, id)) return id;
            return 0;
        }
        case TEST_BURST: return _burst_act_id;
        default:         return 0;
    }
}

uint8_t TestManager::getProgress() const {
    uint8_t total = getEnabledActuatorCount();
    if (total == 0) return 0;

    switch (_mode) {
        case TEST_SWEEP:
            return (uint8_t)((uint16_t)_sweep_act_idx * 100 / total);
        case TEST_BURST:
            return (uint8_t)(((uint16_t)(_burst_count - _burst_remaining) * 100)
                             / _burst_count);
        case TEST_IDLE:
            return 100;
        default:
            return 0;
    }
}

const TestLogEntry& TestManager::getLogEntry(uint8_t idx) const {
    // idx 0 = most recent entry
    int8_t pos = (int8_t)_log_head - 1 - (int8_t)idx;
    if (pos < 0) pos += TEST_LOG_SIZE;
    return _log[(uint8_t)pos % TEST_LOG_SIZE];
}

void TestManager::clearLog() {
    memset(_log, 0, sizeof(_log));
    _log_head  = 0;
    _log_count = 0;
}
