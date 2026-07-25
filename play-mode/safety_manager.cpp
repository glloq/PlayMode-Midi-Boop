#include "safety_manager.h"
#include "scheduler.h"

// ============================================================================
// PlayMode — Safety Manager + Power Manager (implementation)
// ============================================================================

// Default state for invalid IDs
const ActuatorSafetyState SafetyManager::_default_safety_state = {};

SafetyManager::SafetyManager(PCADriver& pca)
    : _pca(pca),
      _scheduler(nullptr),
      _kill_requested(false),
      _rearm_requested(false),
      _rescan_requested(false),
      _max_duty_cycle(SAFETY_MAX_DUTY_CYCLE),
      _max_freq_hz(SAFETY_MAX_FREQ_HZ),
      _watchdog_ms(SAFETY_WATCHDOG_MS),
      _max_polyphony(SAFETY_MAX_POLYPHONY),
      _max_total_current_ma(SAFETY_MAX_TOTAL_CURRENT_MA),
      _last_check_us(0),
      _cached_actuators(nullptr),
      _cached_actuator_count(0) {
    memset(_actuator_safety, 0, sizeof(_actuator_safety));
    memset(&_global_state, 0, sizeof(_global_state));
}

void SafetyManager::begin() {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        memset(&_actuator_safety[i], 0, sizeof(ActuatorSafetyState));
        _actuator_safety[i].window_start_us = now_us;
    }

    memset(&_global_state, 0, sizeof(SafetyState));
    _last_check_us = now_us;

    Serial.println("[SAFETY] Safety Manager initialized");
    Serial.printf("[SAFETY] Limits: duty=%d%%, freq=%dHz, watchdog=%dms, polyphony=%d, current=%dmA\n",
                  _max_duty_cycle, _max_freq_hz, _watchdog_ms,
                  _max_polyphony, _max_total_current_ma);
}

void SafetyManager::setScheduler(Scheduler* scheduler) {
    _scheduler = scheduler;
}

// ============================================================================
// AUDIT FIX (P0.1/P0.2): cross-task requests. The web task only raises a flag;
// the scheduler task (Core 1) does the real work in processPendingRequests().
// ============================================================================

void SafetyManager::requestKillSwitch() {
    // Latch the intent immediately so checkEvent() starts blocking new
    // executions on the very next event, before the hardware is even cut.
    _kill_requested = true;
}

void SafetyManager::requestRearm() {
    _rearm_requested = true;
}

void SafetyManager::requestRescan() {
    _rescan_requested = true;
}

void SafetyManager::processPendingRequests(ActuatorConfig* actuators[], uint8_t count) {
    // Cache the array so activateKillSwitch()/resetActuatorStates() can reach it.
    _cached_actuators      = actuators;
    _cached_actuator_count = count;

    // Rescan implies a kill (outputs disabled, registers cleared) first.
    if (_rescan_requested.exchange(false)) {
        doRescan();
    }
    if (_kill_requested.exchange(false)) {
        activateKillSwitch();
    }
    if (_rearm_requested.exchange(false)) {
        deactivateKillSwitch();
    }
}

void SafetyManager::doRescan() {
    // 1. Kill: OE high, registers cleared, queues flushed, states reset.
    activateKillSwitch();
    // 2/3/4/5. Rebuild the drivers and restore per-bus PWM frequencies (the
    // driver applies its stored pwm_frequency to each recreated PCA).
    _pca.rescanAll();
    // 6. Outputs stay disabled (kill latched) until a manual re-arm.
    Serial.println("[SAFETY] I2C rescan complete (Core 1) — outputs remain disabled");
}

// ============================================================================
// Pre-event check
// ============================================================================

bool SafetyManager::checkEvent(const ActuatorConfig& actuator, const SchedulerEvent& event) {
    // Kill switch active OR a kill has just been requested (from the web) =
    // block everything immediately, even before the hardware is physically cut.
    if (_global_state.kill_switch_active || _kill_requested.load()) return false;

    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return false;

    // Only NOTE_ON events are subject to frequency/duty/polyphony limits
    if (event.action == ACTION_NOTE_ON) {
        // Check trigger frequency
        if (!checkFrequency(id)) {
            _actuator_safety[id].rate_limited = true;
            Serial.printf("[SAFETY] Actuator %d: frequency limited\n", id);
            return false;
        }

        // Check duty cycle
        if (!checkDutyCycle(id, actuator)) {
            _actuator_safety[id].duty_limited = true;
            Serial.printf("[SAFETY] Actuator %d: duty cycle limited\n", id);
            return false;
        }

        // AUDIT FIX (P0.3): reserve resources PER EVENT. The scheduler executes
        // every due event before update() recomputes the aggregates, so a burst
        // of same-timestamp NOTE_ONs would otherwise all read the same stale
        // counters and all be admitted. We therefore project this activation's
        // cost onto the live counters right here; update() reconciles them
        // authoritatively from the real actuator states each cycle.
        uint16_t projected_ma = estimateActuatorCurrent(actuator);
        if (projected_ma == 0) {
            // estimateActuatorCurrent() returns 0 for an inactive actuator;
            // estimate from the type so the reservation is meaningful.
            projected_ma = (actuator.type == ACT_SOLENOID)
                         ? SAFETY_SOLENOID_CURRENT_MA : SAFETY_SERVO_CURRENT_MA;
        }

        // Check global polyphony (projected)
        if (_global_state.active_actuator_count + 1 > _max_polyphony) {
            // Graceful degradation: reject low-priority notes
            if (event.priority > 0) {
                Serial.printf("[SAFETY] Max polyphony reached (%d), event rejected\n",
                              _max_polyphony);
                return false;
            }
        }

        // Check total estimated current (projected)
        if (_global_state.total_estimated_current_ma + projected_ma > _max_total_current_ma) {
            Serial.printf("[SAFETY] Max current reached (%u+%umA > %umA), event rejected\n",
                          _global_state.total_estimated_current_ma, projected_ma,
                          _max_total_current_ma);
            return false;
        }

        // Reserve the projected cost so the next event this tick sees it.
        _global_state.total_estimated_current_ma += projected_ma;
        _global_state.active_actuator_count++;
    }

    // Update counters
    // AUDIT FIX: only NOTE_ON events count as triggers for the
    // rate limiter. NOTE_OFF and ACTION_PWM_SET (solenoid returns) must
    // not inflate trigger_count_window.
    if (event.action == ACTION_NOTE_ON) {
        _actuator_safety[id].trigger_count_window++;
    }
    _actuator_safety[id].last_activity_us = (uint32_t)esp_timer_get_time();
    _actuator_safety[id].watchdog_triggered = false;
    _actuator_safety[id].rate_limited = false;
    _actuator_safety[id].duty_limited = false;

    return true;
}

// ============================================================================
// Periodic monitoring
// ============================================================================

void SafetyManager::update(ActuatorConfig* actuators[], uint8_t count) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // Only check at the configured interval
    if ((now_us - _last_check_us) < (SAFETY_CHECK_INTERVAL_MS * 1000)) {
        return;
    }
    _last_check_us = now_us;

    // AUDIT FIX: remember the actuator array so activateKillSwitch() can reset
    // every runtime state, even when the kill is triggered from the web task.
    _cached_actuators      = actuators;
    _cached_actuator_count = count;

    // Update the current estimate
    updateCurrentEstimate(actuators, count);

    // Check watchdogs for each registered actuator
    for (uint8_t i = 0; i < count; i++) {
        if (actuators[i] != nullptr && actuators[i]->enabled) {
            checkWatchdog(actuators[i]->id, *actuators[i]);
        }
    }

    // AUDIT FIX: graceful degradation with hysteresis to avoid flapping
    // around the threshold when current oscillates.
    if (_global_state.degradation_active) {
        if (_global_state.total_estimated_current_ma <= SAFETY_DEGRADATION_RELEASE_MA) {
            _global_state.degradation_active = false;
        }
    } else if (_global_state.total_estimated_current_ma >= SAFETY_DEGRADATION_THRESHOLD_MA) {
        _global_state.degradation_active = true;
        Serial.printf("[SAFETY] Graceful degradation activated (%dmA)\n",
                      _global_state.total_estimated_current_ma);
    }

    // Check for critical current overload (latches kill switch)
    if (_global_state.total_estimated_current_ma >= _max_total_current_ma) {
        if (!_global_state.over_current) {
            _global_state.over_current = true;
            Serial.printf("[SAFETY] CURRENT OVERLOAD: %dmA > %dmA — KILL SWITCH\n",
                          _global_state.total_estimated_current_ma, _max_total_current_ma);
            activateKillSwitch();
        }
    } else {
        _global_state.over_current = false;
    }

    // AUDIT FIX: the over-current kill switch is latched — it stays active
    // until it is cleared manually from the web UI. Automatic recovery was
    // removed on purpose (re-energising into a persistent fault is unsafe).

    // Reset expired counting windows (1 second)
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        if ((now_us - _actuator_safety[i].window_start_us) >= 1000000) {
            resetWindow(i);
        }
    }
}

// ============================================================================
// Kill switch
// ============================================================================

void SafetyManager::activateKillSwitch() {
    // AUDIT FIX (P0.6): full emergency-stop sequence.
    // 1. Latch the kill switch so no further events can be accepted/executed.
    _global_state.kill_switch_active = true;
    // 2. Cut the outputs (OE high) AND clear every PCA PWM register, so a later
    //    re-arm cannot resurrect stale drive values.
    _pca.killAll();
    // 3. Flush the scheduler queues — any queued NOTE_ON / return event would
    //    otherwise sit waiting and could re-drive an output on re-arm.
    if (_scheduler != nullptr) _scheduler->clearQueue();
    // 4. Reset every runtime state to "inactive" so the current estimate
    //    reflects the physical reality (nothing is actuating).
    resetActuatorStates();
    // 5/6. The latch stays engaged; outputs are only re-enabled by an explicit
    //    deactivateKillSwitch() (manual reset from the web UI).
    Serial.println("[SAFETY] KILL SWITCH ACTIVATED — outputs cut, registers cleared, "
                   "queues flushed (manual reset required)");
}

void SafetyManager::deactivateKillSwitch() {
    _global_state.kill_switch_active = false;
    _global_state.over_current       = false;
    _pca.enableBus(0, true);
    _pca.enableBus(1, true);
    Serial.println("[SAFETY] Kill switch deactivated — outputs re-enabled");
}

bool SafetyManager::isKillSwitchActive() const {
    return _global_state.kill_switch_active;
}

// ============================================================================
// Accessors
// ============================================================================

const ActuatorSafetyState& SafetyManager::getActuatorSafetyState(uint8_t actuator_id) const {
    if (actuator_id >= MAX_ACTUATORS) return _default_safety_state;
    return _actuator_safety[actuator_id];
}

const SafetyState& SafetyManager::getGlobalState() const {
    return _global_state;
}

uint32_t SafetyManager::getEstimatedCurrentMA() const {
    return _global_state.total_estimated_current_ma;
}

uint8_t SafetyManager::getActiveActuatorCount() const {
    return _global_state.active_actuator_count;
}

bool SafetyManager::isDegradationActive() const {
    return _global_state.degradation_active;
}

// ============================================================================
// Configuration runtime
// ============================================================================

void SafetyManager::setMaxDutyCycle(uint8_t percent) {
    _max_duty_cycle = percent;
}

void SafetyManager::setMaxFrequency(uint16_t hz) {
    _max_freq_hz = hz;
}

void SafetyManager::setWatchdogTimeout(uint16_t ms) {
    _watchdog_ms = ms;
}

void SafetyManager::setMaxPolyphony(uint8_t max) {
    _max_polyphony = max;
}

void SafetyManager::setMaxTotalCurrent(uint16_t ma) {
    _max_total_current_ma = ma;
}

// ============================================================================
// Internal methods
// ============================================================================

bool SafetyManager::checkFrequency(uint8_t actuator_id) {
    if (actuator_id >= MAX_ACTUATORS) return false;

    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // If the window has expired, it will be reset in update()
    // Calculate the number of triggers per second in the current window
    uint32_t elapsed_us = now_us - state.window_start_us;
    if (elapsed_us == 0) elapsed_us = 1;

    // If within the same window, check the trigger count
    if (elapsed_us < 1000000) {
        // Estimate the projected frequency over 1 second
        if (state.trigger_count_window >= _max_freq_hz) {
            return false;
        }
    }

    return true;
}

bool SafetyManager::checkDutyCycle(uint8_t actuator_id, const ActuatorConfig& actuator) {
    if (actuator_id >= MAX_ACTUATORS) return false;

    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint32_t elapsed_us = now_us - state.window_start_us;
    if (elapsed_us == 0) elapsed_us = 1;

    // Calculate the current duty cycle (%)
    // active_time_us = cumulative active time in the window
    uint8_t duty = 0;
    if (elapsed_us < 1000000) {
        duty = (uint8_t)((state.active_time_us * 100UL) / elapsed_us);
    }

    return duty < _max_duty_cycle;
}

void SafetyManager::checkWatchdog(uint8_t actuator_id, ActuatorConfig& actuator) {
    if (actuator_id >= MAX_ACTUATORS) return;
    if (!actuator.state.active) return;

    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint32_t elapsed_ms = (now_us - state.last_activity_us) / 1000;

    if (elapsed_ms >= _watchdog_ms) {
        // Watchdog expired: force actuator deactivation
        state.watchdog_triggered = true;
        actuator.state.active = false;

        // Cut the PWM output directly
        // AUDIT FIX: servos were omitted — return to rest position.
        if (actuator.type == ACT_SOLENOID) {
            _pca.setActuatorPWM(actuator, 0);
        } else if (actuator.type == ACT_SERVO) {
            _pca.setActuatorPWM(actuator, _pca.angleToPWM(actuator.angle_initial, actuator.bus_id));
        }

        Serial.printf("[SAFETY] Watchdog actuator %d: forced OFF after %dms\n",
                      actuator_id, elapsed_ms);
    }
}

uint16_t SafetyManager::estimateActuatorCurrent(const ActuatorConfig& actuator) {
    if (!actuator.state.active) return 0;

    if (actuator.type == ACT_SERVO) {
        return SAFETY_SERVO_CURRENT_MA;
    }

    if (actuator.type == ACT_SOLENOID) {
        // Differentiate full power vs hold
        if (actuator.state.current_position >= actuator.pwm_initial / 2) {
            return SAFETY_SOLENOID_CURRENT_MA;
        } else {
            return SAFETY_SOLENOID_HOLD_MA;
        }
    }

    return 0;
}

void SafetyManager::updateCurrentEstimate(ActuatorConfig* actuators[], uint8_t count) {
    uint32_t total_ma = 0;
    uint8_t active_count = 0;

    for (uint8_t i = 0; i < count; i++) {
        if (actuators[i] != nullptr && actuators[i]->enabled) {
            uint16_t current = estimateActuatorCurrent(*actuators[i]);
            _actuator_safety[actuators[i]->id].estimated_current_ma = current;

            total_ma += current;
            if (actuators[i]->state.active) {
                active_count++;

                // Update the active time in the window
                uint8_t id = actuators[i]->id;
                if (id < MAX_ACTUATORS) {
                    _actuator_safety[id].active_time_us +=
                        SAFETY_CHECK_INTERVAL_MS * 1000;
                }
            }
        }
    }

    _global_state.total_estimated_current_ma = total_ma;
    _global_state.active_actuator_count = active_count;
}

void SafetyManager::resetWindow(uint8_t actuator_id) {
    if (actuator_id >= MAX_ACTUATORS) return;

    _actuator_safety[actuator_id].window_start_us = (uint32_t)esp_timer_get_time();
    _actuator_safety[actuator_id].trigger_count_window = 0;
    _actuator_safety[actuator_id].active_time_us = 0;
    _actuator_safety[actuator_id].rate_limited = false;
    _actuator_safety[actuator_id].duty_limited = false;
}

// AUDIT FIX: mark every registered actuator inactive. Called when the kill
// switch cuts the outputs so the current estimate reflects the physical
// reality (OE disabled → nothing is actuating).
void SafetyManager::resetActuatorStates() {
    if (_cached_actuators == nullptr) return;
    for (uint8_t i = 0; i < _cached_actuator_count; i++) {
        if (_cached_actuators[i] != nullptr) {
            _cached_actuators[i]->state.active = false;
        }
    }
}
