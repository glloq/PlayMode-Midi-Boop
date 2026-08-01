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
      _kill_requested(false),
      _rearm_requested(false),
      _rescan_requested(false),
      _ack_requested(false),
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
    _kill_requested = false;
    _rearm_requested = false;
    _rescan_requested = false;
    _ack_requested = false;

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
                                          uint8_t behavior, uint8_t velocity,
                                          bool assume_active) const {
    if (!assume_active && !actuator.state.active) return 0;

    if (actuator.type == ACT_SERVO) {
        return POWER_SERVO_ACTIVE_MA;
    }
    if (actuator.type == ACT_SOLENOID) {
        if (assume_active) {
            // AUDIT FIX (P1.1): use the EFFECTIVE behaviour (per-note override),
            // not the actuator default — a note overriding to hit-and-hold must
            // be estimated at full power, not as a light strike.
            if (behavior == SOL_HIT_AND_HOLD) return POWER_SOLENOID_FULL_MA;
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
    // Kill (active or just requested) or a latched fault blocks everything
    // immediately.
    if (_global_state.kill_switch_active || _global_state.fault_latched ||
        _kill_requested.load()) return false;

    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return false;

    // Only NOTE_ON is subject to the budget checks; NOTE_OFF / returns / safe-off
    // are always allowed (they release resources).
    if (event.action != ACTION_NOTE_ON) return true;

    if (!checkFrequency(id)) {
        _actuator_safety[id].rate_limited = true;
        return false;
    }
    if (!checkDutyCycle(id, actuator)) {
        _actuator_safety[id].duty_limited = true;
        return false;
    }

    // AUDIT FIX (P1.2): a retrigger of an already-active actuator is NOT a new
    // voice — do not count it against polyphony or the current budget.
    bool new_voice = !actuator.state.active;
    uint8_t effective_behavior = (event.behavior_override != 0xFF)
                               ? event.behavior_override : actuator.behavior;

    if (new_voice) {
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

        // Current budget (global + physical bus). observe() runs before the next
        // admit(), so the running totals already include prior events this tick.
        // AUDIT FIX (P1.3): admission allows total == cap (reject only strictly
        // above); update() likewise treats over-current as strictly above the
        // cap, so the boundary is consistent.
        uint16_t projected = estimateCurrent(actuator, effective_behavior,
                                             event.velocity, true);
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
                              const SchedulerEvent& event) {
    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return;

    uint8_t instrument_index = event.instrument_index;
    uint8_t effective_behavior = (event.behavior_override != 0xFF)
                               ? event.behavior_override : actuator.behavior;

    if (!was_active && is_active) {
        uint16_t ma = estimateCurrent(actuator, effective_behavior, event.velocity, true);
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

        uint16_t ma = estimateCurrent(*act, act->behavior, 0, false);
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

    // AUDIT FIX (P0.1/P1.3): the instantaneous flag tracks the live value
    // (strictly above the cap = overload, consistent with admission), but the
    // LATCHED fault is set on a rising edge and is NEVER cleared here — only an
    // explicit acknowledge clears it. So a fault cannot be silently forgotten
    // once the kill switch brings the current back down.
    bool over_now = (total_ma > _budget.global_max_ma);
    _global_state.over_current = over_now;
    if (over_now && !_global_state.fault_latched) {
        _global_state.fault_latched = true;
        Serial.printf("[RESOURCE] CURRENT OVERLOAD: %umA > %umA — FAULT LATCHED\n",
                      total_ma, _budget.global_max_ma);
        activateKillSwitch();
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
    // AUDIT FIX (P0.1): refuse to re-arm while a fault is latched — it must be
    // acknowledged first. (processPendingRequests already guards this, but keep
    // the invariant here too for any direct caller.)
    if (_global_state.fault_latched) {
        Serial.println("[RESOURCE] Re-arm blocked — fault still latched");
        return;
    }
    _global_state.kill_switch_active = false;
    for (uint8_t b = 0; b < 2; b++) _pca.enableBus(b, _pca.getBusConfig(b).enabled);
    Serial.println("[RESOURCE] Kill switch cleared — enabled outputs re-armed");
}

bool ResourceManager::isKillSwitchActive() const { return _global_state.kill_switch_active; }

// AUDIT FIX (P0.3): independent flags — a request never overwrites another.
void ResourceManager::requestKillSwitch()  { _kill_requested = true; }
void ResourceManager::requestRearm()       { _rearm_requested = true; }
void ResourceManager::requestRescan()      { _rescan_requested = true; }
void ResourceManager::requestAcknowledge() { _ack_requested = true; }

void ResourceManager::requestBusFrequency(uint8_t bus_id, uint16_t hz) {
    if (bus_id == 0) _req_freq_bus0 = hz;
    else if (bus_id == 1) _req_freq_bus1 = hz;
}

void ResourceManager::latchHardwareFault(const char* reason) {
    _global_state.fault_latched = true;
    Serial.printf("[RESOURCE] HARDWARE FAULT LATCHED: %s — KILL SWITCH\n",
                  reason ? reason : "(unknown)");
    activateKillSwitch();
}

void ResourceManager::processPendingRequests(ActuatorConfig* actuators[], uint8_t count) {
    _cached_actuators      = actuators;
    _cached_actuator_count = count;

    // AUDIT FIX (P0.3): strict priority — kill first, then rescan, then
    // acknowledge, then re-arm ONLY if nothing else is pending and no fault is
    // latched. A re-arm can no longer swallow a kill.
    bool kill   = _kill_requested.exchange(false);
    bool rescan = _rescan_requested.exchange(false);
    bool ack    = _ack_requested.exchange(false);
    bool rearm  = _rearm_requested.exchange(false);

    if (kill) {
        activateKillSwitch();
    } else if (rescan) {
        doRescan();
    } else {
        if (ack) {
            // Acknowledge clears a latched fault only when the cause is gone.
            // It does NOT re-arm — a separate arm action is required.
            if (_global_state.over_current) {
                Serial.println("[RESOURCE] Acknowledge refused — over-current still present");
            } else {
                _global_state.fault_latched = false;
                Serial.println("[RESOURCE] Fault acknowledged (still disarmed — arm to resume)");
            }
        }
        if (rearm) {
            if (_global_state.fault_latched)
                Serial.println("[RESOURCE] Re-arm refused — fault not acknowledged");
            else
                deactivateKillSwitch();
        }
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
    bool was_armed = !_global_state.kill_switch_active;

    // AUDIT FIX (P0.6): change the prescaler with the bus disabled, then RECOMPUTE
    // every affected actuator's rest position at the NEW frequency before
    // re-enabling. A stale PWM register that meant a valid 50 Hz servo pulse
    // does not mean the same pulse width at 200/1000 Hz, so re-enabling without
    // recomputing could slam a servo into its end stop.
    _pca.enableBus(bus_id, false);
    _pca.setFrequency(bus_id, hz);

    if (_cached_actuators != nullptr) {
        for (uint8_t i = 0; i < _cached_actuator_count; i++) {
            ActuatorConfig* act = _cached_actuators[i];
            if (!act || act->bus_id != bus_id) continue;
            if (act->type == ACT_SERVO) {
                _pca.setActuatorPWM(*act, _pca.angleToPWM(act->angle_initial, bus_id));
                act->state.current_position = _pca.angleToPWM(act->angle_initial, bus_id);
            } else {
                _pca.setActuatorPWM(*act, 0);
                act->state.current_position = 0;
            }
            act->state.active = false;
        }
    }

    // Re-enable only if the system is armed AND the bus is enabled in config.
    if (was_armed && _pca.getBusConfig(bus_id).enabled) _pca.enableBus(bus_id, true);
    Serial.printf("[RESOURCE] Bus %d PWM frequency set to %d Hz, rest positions recomputed (Core 1)\n",
                  bus_id, hz);
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
    // AUDIT FIX (P1.4): roll an expired window synchronously, HERE, before the
    // count check — do not wait for ResourceManager::update(). The scheduler
    // drains every ready event before update() runs, so a burst of notes with
    // the same timestamp, arriving just after the 1 s window expired, would all
    // pass the (skipped) count check and bypass the rate limiter entirely.
    // Resetting here means the fresh window's counter gates the very next events
    // in the same batch.
    if ((now_us - state.window_start_us) >= 1000000) resetWindow(actuator_id);
    if (state.trigger_count_window >= _max_freq_hz) return false;
    return true;
}

bool ResourceManager::checkDutyCycle(uint8_t actuator_id, const ActuatorConfig& actuator) {
    if (actuator_id >= MAX_ACTUATORS) return false;
    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    // AUDIT FIX (P1.4): roll an expired window here as well (see checkFrequency).
    if ((now_us - state.window_start_us) >= 1000000) resetWindow(actuator_id);
    uint32_t elapsed_us = now_us - state.window_start_us;
    if (elapsed_us == 0) elapsed_us = 1;
    uint8_t duty = (uint8_t)((state.active_time_us * 100UL) / elapsed_us);
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
        // AUDIT FIX (P0.2): only declare the actuator inactive if the hardware
        // stop actually reached the PCA. If it failed (driver missing / I²C
        // error), the output may still be driven — keep it "active" and latch a
        // hardware fault (which cuts OE globally) instead of silently lying.
        bool stopped;
        if (actuator.type == ACT_SOLENOID) {
            stopped = _pca.setActuatorPWM(actuator, 0);
        } else {
            stopped = _pca.setActuatorPWM(actuator,
                        _pca.angleToPWM(actuator.angle_initial, actuator.bus_id));
        }
        if (stopped) {
            actuator.state.active = false;
            Serial.printf("[RESOURCE] Watchdog actuator %d: forced OFF after %dms\n",
                          actuator_id, elapsed_ms);
        } else {
            Serial.printf("[RESOURCE] Watchdog actuator %d: HW stop FAILED\n", actuator_id);
            latchHardwareFault("watchdog safe-off write failed");
        }
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
