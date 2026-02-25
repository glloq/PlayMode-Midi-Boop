#include "power_manager.h"

// ============================================================================
// PlayMode Midi B∞p — Power Manager (Phase 5) — Implémentation
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

    // Valeurs par défaut si non configurées
    if (_budget.global_max_ma == 0)
        _budget.global_max_ma = POWER_GLOBAL_MAX_MA;
    if (_budget.servo_bus_max_ma == 0)
        _budget.servo_bus_max_ma = POWER_SERVO_BUS_MAX_MA;
    if (_budget.solenoid_bus_max_ma == 0)
        _budget.solenoid_bus_max_ma = POWER_SOLENOID_BUS_MAX_MA;
    if (_budget.global_max_polyphony == 0)
        _budget.global_max_polyphony = POWER_MAX_POLYPHONY;

    // Polyphonie par instrument non configurée → utiliser la valeur globale
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        if (_budget.instrument_max_polyphony[i] == 0) {
            _budget.instrument_max_polyphony[i] = _budget.global_max_polyphony;
        }
    }

    memset(&_stats, 0, sizeof(_stats));
    memset(_actuator_allocated_ma, 0, sizeof(_actuator_allocated_ma));
    memset(_actuator_tracked, false, sizeof(_actuator_tracked));

    _last_update_us = (uint32_t)esp_timer_get_time();

    Serial.println("[POWER] Power Manager initialisé");
    Serial.printf("[POWER] Budget : global=%umA, servos=%umA, solénoïdes=%umA, polyphonie=%d\n",
                  _budget.global_max_ma, _budget.servo_bus_max_ma,
                  _budget.solenoid_bus_max_ma, _budget.global_max_polyphony);
}

// ============================================================================
// Décision d'activation
// ============================================================================

bool PowerManager::canActivate(const ActuatorConfig& actuator,
                                uint8_t instrument_index,
                                uint8_t velocity) {
    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return false;
    if (!actuator.enabled)   return false;

    // --- Vérification polyphonie globale ---
    if (_stats.global_active_count >= _budget.global_max_polyphony) {
        if (!_budget.smart_rejection) {
            // Rejet strict : refuser toute note supplémentaire
            _stats.total_rejected++;
            Serial.printf("[POWER] Polyphonie globale max (%d) — note refusée\n",
                          _budget.global_max_polyphony);
            return false;
        }
        // Rejet intelligent : accepter seulement si vélocité ≥ 100 (haute priorité)
        if (velocity < 100) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Polyphonie max + smart rejection — vélocité %d refusée\n",
                          velocity);
            return false;
        }
    }

    // --- Vérification polyphonie par instrument ---
    if (instrument_index < MAX_INSTRUMENTS) {
        uint8_t inst_active = _stats.instrument_active_count[instrument_index];
        uint8_t inst_max    = _budget.instrument_max_polyphony[instrument_index];
        if (inst_active >= inst_max) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Polyphonie instrument %d max (%d) — note refusée\n",
                          instrument_index, inst_max);
            return false;
        }
    }

    // --- Vérification budget courant ---
    uint16_t needed_ma = estimateCurrent(actuator, velocity);

    // Budget global
    if (_stats.total_estimated_ma + needed_ma > _budget.global_max_ma) {
        _stats.total_rejected++;
        Serial.printf("[POWER] Budget global dépassé (%u + %u > %u mA) — note refusée\n",
                      _stats.total_estimated_ma, needed_ma, _budget.global_max_ma);
        return false;
    }

    // Budget par bus
    if (actuator.bus_id == 0) {
        // Bus 0 = servos
        if (_stats.servo_bus_ma + needed_ma > _budget.servo_bus_max_ma) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Budget bus servo dépassé (%u + %u > %u mA)\n",
                          _stats.servo_bus_ma, needed_ma, _budget.servo_bus_max_ma);
            return false;
        }
    } else if (actuator.bus_id == 1) {
        // Bus 1 = solénoïdes
        if (_stats.solenoid_bus_ma + needed_ma > _budget.solenoid_bus_max_ma) {
            _stats.total_rejected++;
            Serial.printf("[POWER] Budget bus solénoïde dépassé (%u + %u > %u mA)\n",
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

    // Allouer le courant à cet actionneur
    _actuator_allocated_ma[id] = ma;
    _actuator_tracked[id]      = true;

    // Mettre à jour les compteurs
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

    // Libérer le courant
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
// Mise à jour périodique
// ============================================================================

void PowerManager::update(ActuatorConfig* actuators[], uint8_t count) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    if ((now_us - _last_update_us) < (POWER_UPDATE_INTERVAL_MS * 1000UL))
        return;

    _last_update_us = now_us;

    // Resynchroniser les compteurs depuis l'état réel des actionneurs.
    // Cela corrige d'éventuelles désynchronisations (watchdog, kill switch, etc.).
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
            // Actionneur actif : estimer la consommation depuis son état courant
            uint16_t ma = 0;

            if (act->type == ACT_SERVO) {
                ma = POWER_SERVO_ACTIVE_MA;
            } else if (act->type == ACT_SOLENOID) {
                // Distinguer pleine puissance vs maintien (hit-and-hold)
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

            // Note : l'index instrument n'est pas disponible ici (non stocké dans ActuatorConfig)
            // Les compteurs par instrument sont maintenus par notify*() uniquement.
        } else {
            // Actionneur inactif : libérer l'allocation si elle est encore comptabilisée
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
// Accesseurs statistiques
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
// Configuration runtime
// ============================================================================

void PowerManager::setGlobalMaxMA(uint32_t ma) {
    _budget.global_max_ma = ma;
    Serial.printf("[POWER] Budget global mis à jour : %u mA\n", ma);
}

void PowerManager::setServoBusMaxMA(uint32_t ma) {
    _budget.servo_bus_max_ma = ma;
}

void PowerManager::setSolenoidBusMaxMA(uint32_t ma) {
    _budget.solenoid_bus_max_ma = ma;
}

void PowerManager::setGlobalMaxPolyphony(uint8_t max) {
    _budget.global_max_polyphony = max;
    Serial.printf("[POWER] Polyphonie globale max : %d\n", max);
}

void PowerManager::setInstrumentMaxPolyphony(uint8_t instrument_index, uint8_t max) {
    if (instrument_index < MAX_INSTRUMENTS)
        _budget.instrument_max_polyphony[instrument_index] = max;
}

// ============================================================================
// Méthodes internes
// ============================================================================

uint16_t PowerManager::estimateCurrent(const ActuatorConfig& actuator,
                                        uint8_t velocity) const {
    if (actuator.type == ACT_SERVO) {
        // Consommation servo légèrement modulée par la vélocité
        // (amplitude plus forte → plus de couple → plus de courant)
        uint16_t base = POWER_SERVO_ACTIVE_MA;
        uint16_t extra = (uint16_t)((velocity * 50UL) / 127);  // jusqu'à +50 mA
        return base + extra;
    }

    if (actuator.type == ACT_SOLENOID) {
        if (actuator.behavior == SOL_HIT_AND_HOLD) {
            // Frappe initiale pleine puissance, puis maintien
            return POWER_SOLENOID_FULL_MA;
        }
        // Frappe simple : courant proportionnel à la vélocité
        uint16_t base  = POWER_SOLENOID_HOLD_MA;
        uint16_t delta = POWER_SOLENOID_FULL_MA - POWER_SOLENOID_HOLD_MA;
        return (uint16_t)(base + (uint32_t)(delta * velocity) / 127);
    }

    return 0;
}

void PowerManager::recalculateBusTotals() {
    uint32_t servo_ma    = 0;
    uint32_t solenoid_ma = 0;

    // Cette méthode est un recalcul interne depuis _actuator_allocated_ma.
    // Elle est appelée si nécessaire mais update() est la source principale.
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        if (_actuator_tracked[i]) {
            // bus_id n'est pas accessible directement ici sans accès aux configs.
            // Le recalcul complet se fait dans update() avec les ActuatorConfig.
            (void)servo_ma;
            (void)solenoid_ma;
        }
    }
}

void PowerManager::updateDerivedStats() {
    // Pourcentage du budget utilisé
    if (_budget.global_max_ma > 0) {
        _stats.budget_used_percent =
            (uint8_t)((_stats.total_estimated_ma * 100UL) / _budget.global_max_ma);
        if (_stats.budget_used_percent > 100) _stats.budget_used_percent = 100;
    } else {
        _stats.budget_used_percent = 0;
    }

    // Dégradation gracieuse
    _stats.degradation_active =
        (_stats.budget_used_percent >= POWER_DEGRADATION_THRESHOLD_PCT);

    // Dépassement soft limit
    _stats.budget_exceeded =
        (_stats.total_estimated_ma > _budget.global_max_ma);
}
