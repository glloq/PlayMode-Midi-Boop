#include "power_manager.h"

// ============================================================================
// PlayMode — Power Manager (Phase 5) — Implementation
// ============================================================================

PowerManager::PowerManager()
    : _last_update_us(0) {
    memset(&_budget, 0, sizeof(_budget));
    memset(&_stats,  0, sizeof(_stats));
    memset(_actuator_allocated_ma, 0, sizeof(_actuator_allocated_ma));
    memset(_actuator_tracked, false, sizeof(_actuator_tracked));
}

void PowerManager::begin(const PowerBudget& budget) {
    _budget = budget;

    // Default values if not configured
    if (_budget.global_max_ma == 0)
        _budget.global_max_ma = POWER_GLOBAL_MAX_MA;
    if (_budget.servo_bus_max_ma == 0)
        _budget.servo_bus_max_ma = POWER_SERVO_BUS_MAX_MA;
    if (_budget.solenoid_bus_max_ma == 0)
        _budget.solenoid_bus_max_ma = POWER_SOLENOID_BUS_MAX_MA;
    if (_budget.global_max_polyphony == 0)
        _budget.global_max_polyphony = POWER_MAX_POLYPHONY;

    // Per-instrument polyphony not configured → use global value
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        if (_budget.instrument_max_polyphony[i] == 0) {
            _budget.instrument_max_polyphony[i] = _budget.global_max_polyphony;
        }
    }

    memset(&_stats, 0, sizeof(_stats));
    memset(_actuator_allocated_ma, 0, sizeof(_actuator_allocated_ma));
    memset(_actuator_tracked, false, sizeof(_actuator_tracked));

    _last_update_us = (uint32_t)esp_timer_get_time();

    Serial.println("[POWER] Power Manager initialized");
    Serial.printf("[POWER] Budget: global=%umA, servos=%umA, solenoids=%umA, polyphony=%d\n",
                  _budget.global_max_ma, _budget.servo_bus_max_ma,
                  _budget.solenoid_bus_max_ma, _budget.global_max_polyphony);
}

// ============================================================================
// Activation decision
// ============================================================================

bool PowerManager::canActivate(const ActuatorConfig& actuator,
                                uint8_t instrument_index,
                                uint8_t velocity) {
    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return false;
    if (!actuator.enabled)   return false;

    // --- Global polyphony check ---
    if (_stats.global_active_count >= _budget.global_max_polyphony) {
        if (!_budget.smart_rejection) {
            // Strict rejection: refuse any additional note
            _stats.total_rejected++;
            Serial.printf("[POWER] Max global polyphony (%d) — note rejected\n",
                          _budget.global_max_polyphony);
            return false;
        }
        // Smart rejection: accept only if velocity >= 100 (high priority)
        if (velocity < 100) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Max polyphony + smart rejection — velocity %d rejected\n",
                          velocity);
            return false;
        }
    }

    // --- Per-instrument polyphony check ---
    if (instrument_index < MAX_INSTRUMENTS) {
        uint8_t inst_active = _stats.instrument_active_count[instrument_index];
        uint8_t inst_max    = _budget.instrument_max_polyphony[instrument_index];
        if (inst_active >= inst_max) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Instrument %d max polyphony (%d) — note rejected\n",
                          instrument_index, inst_max);
            return false;
        }
    }

    // --- Current budget check ---
    uint16_t needed_ma = estimateCurrent(actuator, velocity);

    // Global budget
    if (_stats.total_estimated_ma + needed_ma > _budget.global_max_ma) {
        _stats.total_rejected++;
        Serial.printf("[POWER] Global budget exceeded (%u + %u > %u mA) — note rejected\n",
                      _stats.total_estimated_ma, needed_ma, _budget.global_max_ma);
        return false;
    }

    // Per-bus budget
    if (actuator.bus_id == 0) {
        // Bus 0 = servos
        if (_stats.servo_bus_ma + needed_ma > _budget.servo_bus_max_ma) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Servo bus budget exceeded (%u + %u > %u mA)\n",
                          _stats.servo_bus_ma, needed_ma, _budget.servo_bus_max_ma);
            return false;
        }
    } else if (actuator.bus_id == 1) {
        // Bus 1 = solenoids
        if (_stats.solenoid_bus_ma + needed_ma > _budget.solenoid_bus_max_ma) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Solenoid bus budget exceeded (%u + %u > %u mA)\n",
                          _stats.solenoid_bus_ma, needed_ma, _budget.solenoid_bus_max_ma);
            return false;
        }
    }

    return true;
}

void PowerManager::notifyActivation(const ActuatorConfig& actuator,
                                     uint8_t instrument_index,
                                     uint8_t velocity) {
    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return;

    uint16_t ma = estimateCurrent(actuator, velocity);

    // Allocate current for this actuator
    _actuator_allocated_ma[id] = ma;
    _actuator_tracked[id]      = true;

    // Update counters
    _stats.total_estimated_ma += ma;
    _stats.global_active_count++;

    if (actuator.bus_id == 0)
        _stats.servo_bus_ma += ma;
    else if (actuator.bus_id == 1)
        _stats.solenoid_bus_ma += ma;

    if (instrument_index < MAX_INSTRUMENTS)
        _stats.instrument_active_count[instrument_index]++;

    updateDerivedStats();
}

void PowerManager::notifyDeactivation(const ActuatorConfig& actuator,
                                       uint8_t instrument_index) {
    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return;
    if (!_actuator_tracked[id]) return;

    uint16_t ma = _actuator_allocated_ma[id];

    // Release the current
    if (_stats.total_estimated_ma >= ma)
        _stats.total_estimated_ma -= ma;
    else
        _stats.total_estimated_ma = 0;

    if (actuator.bus_id == 0) {
        if (_stats.servo_bus_ma >= ma) _stats.servo_bus_ma -= ma;
        else _stats.servo_bus_ma = 0;
    } else if (actuator.bus_id == 1) {
        if (_stats.solenoid_bus_ma >= ma) _stats.solenoid_bus_ma -= ma;
        else _stats.solenoid_bus_ma = 0;
    }

    if (_stats.global_active_count > 0)
        _stats.global_active_count--;

    if (instrument_index < MAX_INSTRUMENTS &&
        _stats.instrument_active_count[instrument_index] > 0)
        _stats.instrument_active_count[instrument_index]--;

    _actuator_allocated_ma[id] = 0;
    _actuator_tracked[id]      = false;

    updateDerivedStats();
}

// ============================================================================
// Periodic update
// ============================================================================

void PowerManager::update(ActuatorConfig* actuators[], uint8_t count) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    if ((now_us - _last_update_us) < (POWER_UPDATE_INTERVAL_MS * 1000UL))
        return;

    _last_update_us = now_us;

    // Resynchronize counters from the actual state of actuators.
    // This corrects potential desynchronizations (watchdog, kill switch, etc.).
    uint32_t total_ma    = 0;
    uint32_t servo_ma    = 0;
    uint32_t solenoid_ma = 0;
    uint8_t  active_cnt  = 0;

    uint8_t inst_active[MAX_INSTRUMENTS] = {};

    for (uint8_t i = 0; i < count; i++) {
        ActuatorConfig* act = actuators[i];
        if (!act || !act->enabled) continue;

        uint8_t id = act->id;
        if (id >= MAX_ACTUATORS) continue;

        if (act->state.active) {
            // Active actuator: estimate consumption from its current state
            uint16_t ma = 0;

            if (act->type == ACT_SERVO) {
                ma = POWER_SERVO_ACTIVE_MA;
            } else if (act->type == ACT_SOLENOID) {
                // Distinguish full power vs hold (hit-and-hold)
                if (act->state.current_position >= (uint16_t)(act->pwm_initial / 2)) {
                    ma = POWER_SOLENOID_FULL_MA;
                } else {
                    ma = POWER_SOLENOID_HOLD_MA;
                }
            }

            _actuator_allocated_ma[id] = ma;
            _actuator_tracked[id]      = true;

            total_ma += ma;
            active_cnt++;

            if (act->bus_id == 0)      servo_ma    += ma;
            else if (act->bus_id == 1) solenoid_ma += ma;

            // Note: instrument index is not available here (not stored in ActuatorConfig)
            // Per-instrument counters are maintained by notify*() only.
        } else {
            // Inactive actuator: release the allocation if it is still accounted for
            if (_actuator_tracked[id]) {
                _actuator_tracked[id]      = false;
                _actuator_allocated_ma[id] = 0;
            }
        }
    }

    _stats.total_estimated_ma  = total_ma;
    _stats.servo_bus_ma        = servo_ma;
    _stats.solenoid_bus_ma     = solenoid_ma;
    _stats.global_active_count = active_cnt;

    updateDerivedStats();
}

// ============================================================================
// Statistics accessors
// ============================================================================

const PowerStats& PowerManager::getStats() const {
    return _stats;
}

uint32_t PowerManager::getBudgetRemainingMA() const {
    if (_stats.total_estimated_ma >= _budget.global_max_ma) return 0;
    return _budget.global_max_ma - _stats.total_estimated_ma;
}

uint8_t PowerManager::getBudgetUsedPercent() const {
    return _stats.budget_used_percent;
}

bool PowerManager::isDegradationActive() const {
    return _stats.degradation_active;
}

const PowerBudget& PowerManager::getBudget() const {
    return _budget;
}

// ============================================================================
// Runtime configuration
// ============================================================================

void PowerManager::setGlobalMaxMA(uint32_t ma) {
    _budget.global_max_ma = ma;
    Serial.printf("[POWER] Global budget updated: %u mA\n", ma);
}

void PowerManager::setServoBusMaxMA(uint32_t ma) {
    _budget.servo_bus_max_ma = ma;
}

void PowerManager::setSolenoidBusMaxMA(uint32_t ma) {
    _budget.solenoid_bus_max_ma = ma;
}

void PowerManager::setGlobalMaxPolyphony(uint8_t max) {
    _budget.global_max_polyphony = max;
    Serial.printf("[POWER] Max global polyphony: %d\n", max);
}

void PowerManager::setInstrumentMaxPolyphony(uint8_t instrument_index, uint8_t max) {
    if (instrument_index < MAX_INSTRUMENTS)
        _budget.instrument_max_polyphony[instrument_index] = max;
}

// ============================================================================
// Internal methods
// ============================================================================

uint16_t PowerManager::estimateCurrent(const ActuatorConfig& actuator,
                                        uint8_t velocity) const {
    if (actuator.type == ACT_SERVO) {
        // Servo consumption slightly modulated by velocity
        // (higher amplitude → more torque → more current)
        uint16_t base = POWER_SERVO_ACTIVE_MA;
        uint16_t extra = (uint16_t)((velocity * 50UL) / 127);  // up to +50 mA
        return base + extra;
    }

    if (actuator.type == ACT_SOLENOID) {
        if (actuator.behavior == SOL_HIT_AND_HOLD) {
            // Initial strike at full power, then hold
            return POWER_SOLENOID_FULL_MA;
        }
        // Simple strike: current proportional to velocity
        // AUDIT FIX: promote to uint32_t BEFORE multiplication to avoid
        // a uint16_t overflow if delta or velocity grows in the future.
        uint16_t base  = POWER_SOLENOID_HOLD_MA;
        uint32_t delta = (uint32_t)POWER_SOLENOID_FULL_MA - (uint32_t)POWER_SOLENOID_HOLD_MA;
        return (uint16_t)(base + (delta * velocity) / 127UL);
    }

    return 0;
}

void PowerManager::updateDerivedStats() {
    // Percentage of budget used
    if (_budget.global_max_ma > 0) {
        _stats.budget_used_percent =
            (uint8_t)((_stats.total_estimated_ma * 100UL) / _budget.global_max_ma);
        if (_stats.budget_used_percent > 100) _stats.budget_used_percent = 100;
    } else {
        _stats.budget_used_percent = 0;
    }

    // AUDIT FIX: graceful degradation with hysteresis to prevent flapping
    // when usage oscillates around POWER_DEGRADATION_THRESHOLD_PCT.
    if (_stats.degradation_active) {
        if (_stats.budget_used_percent <= POWER_DEGRADATION_RELEASE_PCT) {
            _stats.degradation_active = false;
        }
    } else if (_stats.budget_used_percent >= POWER_DEGRADATION_THRESHOLD_PCT) {
        _stats.degradation_active = true;
    }

    // Soft limit exceeded
    _stats.budget_exceeded =
        (_stats.total_estimated_ma > _budget.global_max_ma);
}
