#include "resource_manager.h"
#include "scheduler.h"

// ============================================================================
// PlayMode — Resource Manager (unified Safety + Power) — implementation
// ============================================================================

const ActuatorSafetyState ResourceManager::_default_safety_state = {};

ResourceManager::ResourceManager(PCADriver& pca)
    : _pca(pca),
      _scheduler(nullptr),
      _max_duty_cycle(SAFETY_MAX_DUTY_CYCLE),
      _max_freq_hz(SAFETY_MAX_FREQ_HZ),
      _watchdog_ms(SAFETY_WATCHDOG_MS),
      _last_check_us(0),
      _requested_state(REQ_NONE),
      _req_freq_bus0(0),
      _req_freq_bus1(0),
      _cached_actuators(nullptr),
      _cached_actuator_count(0) {
    memset(&_budget, 0, sizeof(_budget));
    memset(&_global_state, 0, sizeof(_global_state));
    memset(&_stats, 0, sizeof(_stats));
    memset(_actuator_safety, 0, sizeof(_actuator_safety));
    memset(_alloc_ma, 0, sizeof(_alloc_ma));
    memset(_alloc_bus, 0, sizeof(_alloc_bus));
    memset(_alloc_inst, 0xFF, sizeof(_alloc_inst));
    memset(_tracked, false, sizeof(_tracked));
    _bus_ma[0] = _bus_ma[1] = 0;
}

static uint32_t minNonZero(uint32_t a, uint32_t b, uint32_t deflt) {
    if (a == 0 && b == 0) return deflt;
    if (a == 0) return b;
    if (b == 0) return a;
    return (a < b) ? a : b;
}

void ResourceManager::begin(const PowerBudget& budget, const SafetyLimits& limits) {
    _budget = budget;

    // AUDIT FIX: unify the two conflicting current/polyphony limits into ONE
    // effective value (the more restrictive of the two persisted numbers).
    _budget.global_max_ma = minNonZero(budget.global_max_ma,
                                       limits.max_current_ma, POWER_GLOBAL_MAX_MA);
    if (_budget.servo_bus_max_ma == 0)    _budget.servo_bus_max_ma = POWER_SERVO_BUS_MAX_MA;
    if (_budget.solenoid_bus_max_ma == 0) _budget.solenoid_bus_max_ma = POWER_SOLENOID_BUS_MAX_MA;

    uint32_t poly = minNonZero(budget.global_max_polyphony,
                               limits.max_polyphony, POWER_MAX_POLYPHONY);
    if (poly > 255) poly = 255;
    _budget.global_max_polyphony = (uint8_t)poly;
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        if (_budget.instrument_max_polyphony[i] == 0)
            _budget.instrument_max_polyphony[i] = _budget.global_max_polyphony;
    }

    _max_duty_cycle = limits.max_duty_pct ? limits.max_duty_pct : SAFETY_MAX_DUTY_CYCLE;
    _max_freq_hz    = limits.max_freq_hz  ? limits.max_freq_hz  : SAFETY_MAX_FREQ_HZ;
    _watchdog_ms    = limits.watchdog_ms  ? limits.watchdog_ms  : SAFETY_WATCHDOG_MS;

    uint32_t now_us = (uint32_t)esp_timer_get_time();
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        memset(&_actuator_safety[i], 0, sizeof(ActuatorSafetyState));
        _actuator_safety[i].window_start_us = now_us;
    }
    memset(&_global_state, 0, sizeof(SafetyState));
    memset(&_stats, 0, sizeof(_stats));
    memset(_alloc_ma, 0, sizeof(_alloc_ma));
    memset(_alloc_inst, 0xFF, sizeof(_alloc_inst));
    memset(_tracked, false, sizeof(_tracked));
    _bus_ma[0] = _bus_ma[1] = 0;
    _last_check_us = now_us;

    Serial.println("[RESOURCE] Resource Manager initialized (unified safety+power)");
    Serial.printf("[RESOURCE] Cap: %umA, buses %u/%umA, polyphony %d | duty=%d%% freq=%dHz wd=%dms\n",
                  _budget.global_max_ma, _budget.servo_bus_max_ma,
                  _budget.solenoid_bus_max_ma, _budget.global_max_polyphony,
                  _max_duty_cycle, _max_freq_hz, _watchdog_ms);
}

void ResourceManager::setScheduler(Scheduler* scheduler) { _scheduler = scheduler; }

// ============================================================================
// Current model (single source of truth)
// ============================================================================
uint16_t ResourceManager::estimateCurrent(const ActuatorConfig& actuator,
                                          uint8_t velocity, bool assume_active) const {
    if (!assume_active && !actuator.state.active) return 0;

    if (actuator.type == ACT_SERVO) {
        return POWER_SERVO_ACTIVE_MA;
    }
    if (actuator.type == ACT_SOLENOID) {
        if (assume_active) {
            // At admission: hit-and-hold starts at full power; a strike scales
            // with velocity between hold and full.
            if (actuator.behavior == SOL_HIT_AND_HOLD) return POWER_SOLENOID_FULL_MA;
            uint16_t base  = POWER_SOLENOID_HOLD_MA;
            uint32_t delta = (uint32_t)POWER_SOLENOID_FULL_MA - (uint32_t)POWER_SOLENOID_HOLD_MA;
            uint8_t v = (velocity > 127) ? 127 : velocity;
            return (uint16_t)(base + (delta * v) / 127UL);
        }
        // Reconcile from the live drive: full vs hold.
        if (actuator.state.current_position >= (uint16_t)(actuator.pwm_initial / 2))
            return POWER_SOLENOID_FULL_MA;
        return POWER_SOLENOID_HOLD_MA;
    }
    return 0;
}

// ============================================================================
// Admission
// ============================================================================
bool ResourceManager::admit(const ActuatorConfig& actuator,
                            const SchedulerEvent& event, uint8_t instrument_index) {
    // Kill (active or just requested) blocks everything immediately.
    if (_global_state.kill_switch_active ||
        _requested_state.load() == REQ_KILL) return false;

    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return false;

    // Only NOTE_ON is subject to the budget checks; NOTE_OFF / returns are
    // always allowed (they release resources).
    if (event.action != ACTION_NOTE_ON) return true;

    if (!checkFrequency(id)) {
        _actuator_safety[id].rate_limited = true;
        return false;
    }
    if (!checkDutyCycle(id, actuator)) {
        _actuator_safety[id].duty_limited = true;
        return false;
    }

    // Global polyphony — HARD limit (no voice stealing).
    if (_global_state.active_actuator_count + 1 > _budget.global_max_polyphony) {
        _stats.total_rejected++;
        return false;
    }
    // Per-instrument polyphony.
    if (instrument_index < MAX_INSTRUMENTS) {
        if (_stats.instrument_active_count[instrument_index] >=
            _budget.instrument_max_polyphony[instrument_index]) {
            _stats.total_rejected++;
            return false;
        }
    }

    // Current budget (global + physical bus). Because observe() runs before the
    // next admit(), the running totals already include prior events this tick.
    uint16_t projected = estimateCurrent(actuator, event.velocity, true);
    if (_global_state.total_estimated_current_ma + projected > _budget.global_max_ma) {
        _stats.total_rejected++;
        return false;
    }
    uint32_t bus_cap = (actuator.bus_id == 0) ? _budget.servo_bus_max_ma
                                              : _budget.solenoid_bus_max_ma;
    if (actuator.bus_id < 2 && _bus_ma[actuator.bus_id] + projected > bus_cap) {
        _stats.total_rejected++;
        return false;
    }

    // Rate-limiter bookkeeping.
    _actuator_safety[id].trigger_count_window++;
    _actuator_safety[id].last_activity_us = (uint32_t)esp_timer_get_time();
    _actuator_safety[id].watchdog_triggered = false;
    _actuator_safety[id].rate_limited = false;
    _actuator_safety[id].duty_limited = false;
    return true;
}

// ============================================================================
// Execution observation
// ============================================================================
void ResourceManager::observe(const ActuatorConfig& actuator,
                              bool was_active, bool is_active,
                              uint8_t instrument_index, uint8_t velocity) {
    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return;

    if (!was_active && is_active) {
        uint16_t ma = estimateCurrent(actuator, velocity, true);
        _alloc_ma[id]   = ma;
        _alloc_bus[id]  = actuator.bus_id;
        _alloc_inst[id] = (instrument_index < MAX_INSTRUMENTS) ? instrument_index : 0xFF;
        _tracked[id]    = true;

        _global_state.total_estimated_current_ma += ma;
        if (actuator.bus_id < 2) _bus_ma[actuator.bus_id] += ma;
        _global_state.active_actuator_count++;
        if (instrument_index < MAX_INSTRUMENTS)
            _stats.instrument_active_count[instrument_index]++;
    } else if (was_active && !is_active) {
        if (_tracked[id]) {
            uint16_t ma = _alloc_ma[id];
            uint32_t& tot = _global_state.total_estimated_current_ma;
            tot = (tot >= ma) ? tot - ma : 0;
            uint8_t b = _alloc_bus[id];
            if (b < 2) _bus_ma[b] = (_bus_ma[b] >= ma) ? _bus_ma[b] - ma : 0;
            if (_global_state.active_actuator_count > 0) _global_state.active_actuator_count--;
            uint8_t in = _alloc_inst[id];
            if (in < MAX_INSTRUMENTS && _stats.instrument_active_count[in] > 0)
                _stats.instrument_active_count[in]--;
            _alloc_ma[id]   = 0;
            _alloc_inst[id] = 0xFF;
            _tracked[id]    = false;
        }
    }
    syncDerivedStats();
}

// ============================================================================
// Periodic reconciliation
// ============================================================================
void ResourceManager::update(ActuatorConfig* actuators[], uint8_t count) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    if ((now_us - _last_check_us) < (SAFETY_CHECK_INTERVAL_MS * 1000)) return;
    _last_check_us = now_us;

    _cached_actuators      = actuators;
    _cached_actuator_count = count;

    uint32_t total_ma = 0, bus0 = 0, bus1 = 0;
    uint8_t  active_count = 0;
    uint8_t  inst_active[MAX_INSTRUMENTS] = {};

    for (uint8_t i = 0; i < count; i++) {
        ActuatorConfig* act = actuators[i];
        if (!act || !act->enabled) continue;
        uint8_t id = act->id;
        if (id >= MAX_ACTUATORS) continue;   // AUDIT FIX (P0.2): bounds-check

        uint16_t ma = estimateCurrent(*act, 0, false);
        _actuator_safety[id].estimated_current_ma = ma;
        total_ma += ma;

        if (act->state.active) {
            active_count++;
            _actuator_safety[id].active_time_us += SAFETY_CHECK_INTERVAL_MS * 1000;
            if (act->bus_id == 0) bus0 += ma; else if (act->bus_id == 1) bus1 += ma;
            uint8_t in = _alloc_inst[id];
            if (in < MAX_INSTRUMENTS) inst_active[in]++;
            _tracked[id]   = true;
            _alloc_ma[id]  = ma;
            _alloc_bus[id] = act->bus_id;
        } else if (_tracked[id]) {
            _tracked[id]    = false;
            _alloc_ma[id]   = 0;
            _alloc_inst[id] = 0xFF;
        }
    }

    _global_state.total_estimated_current_ma = total_ma;
    _global_state.active_actuator_count      = active_count;
    _bus_ma[0] = bus0;
    _bus_ma[1] = bus1;
    memcpy(_stats.instrument_active_count, inst_active, sizeof(inst_active));

    // Watchdogs.
    for (uint8_t i = 0; i < count; i++) {
        if (actuators[i] != nullptr && actuators[i]->enabled)
            checkWatchdog(actuators[i]->id, *actuators[i]);
    }

    // Degradation hysteresis on % of the unified cap.
    uint8_t pct = (_budget.global_max_ma > 0)
                ? (uint8_t)((total_ma * 100UL) / _budget.global_max_ma) : 0;
    if (_global_state.degradation_active) {
        if (pct <= POWER_DEGRADATION_RELEASE_PCT) _global_state.degradation_active = false;
    } else if (pct >= POWER_DEGRADATION_THRESHOLD_PCT) {
        _global_state.degradation_active = true;
        Serial.printf("[RESOURCE] Graceful degradation active (%u%%, %umA)\n", pct, total_ma);
    }

    // Over-current latch.
    if (total_ma >= _budget.global_max_ma) {
        if (!_global_state.over_current) {
            _global_state.over_current = true;
            Serial.printf("[RESOURCE] CURRENT OVERLOAD: %umA >= %umA — KILL SWITCH\n",
                          total_ma, _budget.global_max_ma);
            activateKillSwitch();
        }
    } else {
        _global_state.over_current = false;
    }

    // Reset expired 1-second windows.
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        if ((now_us - _actuator_safety[i].window_start_us) >= 1000000) resetWindow(i);
    }

    syncDerivedStats();
}

void ResourceManager::syncDerivedStats() {
    _stats.total_estimated_ma  = _global_state.total_estimated_current_ma;
    _stats.servo_bus_ma        = _bus_ma[0];
    _stats.solenoid_bus_ma     = _bus_ma[1];
    _stats.global_active_count = _global_state.active_actuator_count;
    _stats.degradation_active  = _global_state.degradation_active;

    if (_budget.global_max_ma > 0) {
        uint32_t pct = (_stats.total_estimated_ma * 100UL) / _budget.global_max_ma;
        _stats.budget_used_percent = (pct > 100) ? 100 : (uint8_t)pct;
    } else {
        _stats.budget_used_percent = 0;
    }
    _stats.budget_exceeded = (_stats.total_estimated_ma > _budget.global_max_ma);
}

// ============================================================================
// Kill switch + cross-task requests
// ============================================================================
void ResourceManager::activateKillSwitch() {
    _global_state.kill_switch_active = true;
    _pca.killAll();
    if (_scheduler != nullptr) _scheduler->clearQueue();
    resetActuatorStates();
    Serial.println("[RESOURCE] KILL SWITCH — outputs cut, registers cleared, queues flushed");
}

void ResourceManager::deactivateKillSwitch() {
    _global_state.kill_switch_active = false;
    _global_state.over_current       = false;
    for (uint8_t b = 0; b < 2; b++) _pca.enableBus(b, _pca.getBusConfig(b).enabled);
    Serial.println("[RESOURCE] Kill switch cleared — enabled outputs re-armed");
}

bool ResourceManager::isKillSwitchActive() const { return _global_state.kill_switch_active; }

void ResourceManager::requestKillSwitch() { _requested_state = REQ_KILL; }
void ResourceManager::requestRearm()      { _requested_state = REQ_REARM; }
void ResourceManager::requestRescan()     { _requested_state = REQ_RESCAN; }

void ResourceManager::requestBusFrequency(uint8_t bus_id, uint16_t hz) {
    if (bus_id == 0) _req_freq_bus0 = hz;
    else if (bus_id == 1) _req_freq_bus1 = hz;
}

void ResourceManager::processPendingRequests(ActuatorConfig* actuators[], uint8_t count) {
    _cached_actuators      = actuators;
    _cached_actuator_count = count;

    uint8_t req = _requested_state.exchange(REQ_NONE);
    switch (req) {
        case REQ_KILL:   activateKillSwitch(); break;
        case REQ_RESCAN: doRescan();           break;
        case REQ_REARM:
            if (_global_state.over_current)
                Serial.println("[RESOURCE] Re-arm refused — over-current still latched");
            else
                deactivateKillSwitch();
            break;
        default: break;
    }

    uint16_t f0 = _req_freq_bus0.exchange(0);
    if (f0 != 0) applyBusFrequency(0, f0);
    uint16_t f1 = _req_freq_bus1.exchange(0);
    if (f1 != 0) applyBusFrequency(1, f1);
}

void ResourceManager::doRescan() {
    activateKillSwitch();
    _pca.rescanAll();
    Serial.println("[RESOURCE] I2C rescan complete (Core 1) — outputs remain disabled");
}

void ResourceManager::applyBusFrequency(uint8_t bus_id, uint16_t hz) {
    if (bus_id > 1) return;
    bool was_killed = _global_state.kill_switch_active;
    _pca.enableBus(bus_id, false);
    _pca.setFrequency(bus_id, hz);
    if (!was_killed && _pca.getBusConfig(bus_id).enabled) _pca.enableBus(bus_id, true);
    Serial.printf("[RESOURCE] Bus %d PWM frequency set to %d Hz (Core 1)\n", bus_id, hz);
}

// ============================================================================
// Accessors
// ============================================================================
const SafetyState& ResourceManager::getGlobalState() const { return _global_state; }
const PowerStats&  ResourceManager::getStats() const       { return _stats; }
const PowerBudget& ResourceManager::getBudget() const      { return _budget; }
uint32_t ResourceManager::getEstimatedCurrentMA() const    { return _global_state.total_estimated_current_ma; }
uint8_t  ResourceManager::getActiveActuatorCount() const   { return _global_state.active_actuator_count; }
bool     ResourceManager::isDegradationActive() const      { return _global_state.degradation_active; }
uint32_t ResourceManager::getTotalRejected() const         { return _stats.total_rejected; }

const ActuatorSafetyState& ResourceManager::getActuatorSafetyState(uint8_t actuator_id) const {
    if (actuator_id >= MAX_ACTUATORS) return _default_safety_state;
    return _actuator_safety[actuator_id];
}

// ============================================================================
// Runtime configuration
// ============================================================================
void ResourceManager::setGlobalMaxMA(uint32_t ma)       { _budget.global_max_ma = ma; }
void ResourceManager::setMaxTotalCurrent(uint32_t ma)   { _budget.global_max_ma = ma; }
void ResourceManager::setBusMaxMA(uint8_t bus_id, uint32_t ma) {
    if (bus_id == 0) _budget.servo_bus_max_ma = ma;
    else if (bus_id == 1) _budget.solenoid_bus_max_ma = ma;
}
void ResourceManager::setGlobalMaxPolyphony(uint8_t max) { _budget.global_max_polyphony = max; }
void ResourceManager::setMaxPolyphony(uint8_t max)       { _budget.global_max_polyphony = max; }
void ResourceManager::setInstrumentMaxPolyphony(uint8_t i, uint8_t max) {
    if (i < MAX_INSTRUMENTS) _budget.instrument_max_polyphony[i] = max;
}
void ResourceManager::setMaxDutyCycle(uint8_t percent) { _max_duty_cycle = percent; }
void ResourceManager::setMaxFrequency(uint16_t hz)     { _max_freq_hz = hz; }
void ResourceManager::setWatchdogTimeout(uint16_t ms)  { _watchdog_ms = ms; }
void ResourceManager::setSmartRejection(bool on)       { _budget.smart_rejection = on; }

// ============================================================================
// Internal helpers (frequency / duty / watchdog / windows)
// ============================================================================
bool ResourceManager::checkFrequency(uint8_t actuator_id) {
    if (actuator_id >= MAX_ACTUATORS) return false;
    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint32_t elapsed_us = now_us - state.window_start_us;
    if (elapsed_us == 0) elapsed_us = 1;
    if (elapsed_us < 1000000) {
        if (state.trigger_count_window >= _max_freq_hz) return false;
    }
    return true;
}

bool ResourceManager::checkDutyCycle(uint8_t actuator_id, const ActuatorConfig& actuator) {
    if (actuator_id >= MAX_ACTUATORS) return false;
    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint32_t elapsed_us = now_us - state.window_start_us;
    if (elapsed_us == 0) elapsed_us = 1;
    uint8_t duty = 0;
    if (elapsed_us < 1000000) duty = (uint8_t)((state.active_time_us * 100UL) / elapsed_us);
    return duty < _max_duty_cycle;
}

void ResourceManager::checkWatchdog(uint8_t actuator_id, ActuatorConfig& actuator) {
    if (actuator_id >= MAX_ACTUATORS) return;
    if (!actuator.state.active) return;
    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint32_t elapsed_ms = (now_us - state.last_activity_us) / 1000;
    if (elapsed_ms >= _watchdog_ms) {
        state.watchdog_triggered = true;
        actuator.state.active = false;
        if (actuator.type == ACT_SOLENOID) {
            _pca.setActuatorPWM(actuator, 0);
        } else if (actuator.type == ACT_SERVO) {
            _pca.setActuatorPWM(actuator, _pca.angleToPWM(actuator.angle_initial, actuator.bus_id));
        }
        Serial.printf("[RESOURCE] Watchdog actuator %d: forced OFF after %dms\n",
                      actuator_id, elapsed_ms);
    }
}

void ResourceManager::resetWindow(uint8_t actuator_id) {
    if (actuator_id >= MAX_ACTUATORS) return;
    _actuator_safety[actuator_id].window_start_us = (uint32_t)esp_timer_get_time();
    _actuator_safety[actuator_id].trigger_count_window = 0;
    _actuator_safety[actuator_id].active_time_us = 0;
    _actuator_safety[actuator_id].rate_limited = false;
    _actuator_safety[actuator_id].duty_limited = false;
}

void ResourceManager::resetActuatorStates() {
    if (_cached_actuators == nullptr) return;
    for (uint8_t i = 0; i < _cached_actuator_count; i++) {
        if (_cached_actuators[i] != nullptr) _cached_actuators[i]->state.active = false;
    }
    // The outputs are physically off; drop all running allocations too.
    _global_state.total_estimated_current_ma = 0;
    _global_state.active_actuator_count = 0;
    _bus_ma[0] = _bus_ma[1] = 0;
    memset(_alloc_ma, 0, sizeof(_alloc_ma));
    memset(_alloc_inst, 0xFF, sizeof(_alloc_inst));
    memset(_tracked, false, sizeof(_tracked));
    memset(_stats.instrument_active_count, 0, sizeof(_stats.instrument_active_count));
    syncDerivedStats();
}
