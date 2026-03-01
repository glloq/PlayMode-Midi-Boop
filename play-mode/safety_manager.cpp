#include "safety_manager.h"

// ============================================================================
// PlayMode — Safety Manager + Power Manager (implémentation)
// ============================================================================

// État par défaut pour les IDs invalides
const ActuatorSafetyState SafetyManager::_default_safety_state = {};

SafetyManager::SafetyManager(PCADriver& pca)
    : _pca(pca),
      _max_duty_cycle(SAFETY_MAX_DUTY_CYCLE),
      _max_freq_hz(SAFETY_MAX_FREQ_HZ),
      _watchdog_ms(SAFETY_WATCHDOG_MS),
      _max_polyphony(SAFETY_MAX_POLYPHONY),
      _max_total_current_ma(SAFETY_MAX_TOTAL_CURRENT_MA),
      _last_check_us(0) {
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

    Serial.println("[SAFETY] Safety Manager initialisé");
    Serial.printf("[SAFETY] Limites : duty=%d%%, freq=%dHz, watchdog=%dms, polyphonie=%d, courant=%dmA\n",
                  _max_duty_cycle, _max_freq_hz, _watchdog_ms,
                  _max_polyphony, _max_total_current_ma);
}

// ============================================================================
// Vérification pré-événement
// ============================================================================

bool SafetyManager::checkEvent(const ActuatorConfig& actuator, const SchedulerEvent& event) {
    // Kill switch actif = tout bloquer
    if (_global_state.kill_switch_active) return false;

    uint8_t id = actuator.id;
    if (id >= MAX_ACTUATORS) return false;

    // Seuls les NOTE_ON sont soumis aux limites de fréquence/duty/polyphonie
    if (event.action == ACTION_NOTE_ON) {
        // Vérifier fréquence de déclenchement
        if (!checkFrequency(id)) {
            _actuator_safety[id].rate_limited = true;
            Serial.printf("[SAFETY] Actionneur %d : fréquence limitée\n", id);
            return false;
        }

        // Vérifier duty cycle
        if (!checkDutyCycle(id, actuator)) {
            _actuator_safety[id].duty_limited = true;
            Serial.printf("[SAFETY] Actionneur %d : duty cycle limité\n", id);
            return false;
        }

        // Vérifier polyphonie globale
        if (_global_state.active_actuator_count >= _max_polyphony) {
            // Dégradation gracieuse : refuser les notes de basse priorité
            if (event.priority > 0) {
                Serial.printf("[SAFETY] Polyphonie max atteinte (%d), événement refusé\n",
                              _max_polyphony);
                return false;
            }
        }

        // Vérifier le courant total estimé
        if (_global_state.total_estimated_current_ma >= _max_total_current_ma) {
            Serial.printf("[SAFETY] Courant max atteint (%dmA), événement refusé\n",
                          _global_state.total_estimated_current_ma);
            return false;
        }
    }

    // Mettre à jour les compteurs
    // AUDIT FIX : seuls les NOTE_ON comptent comme déclenchements pour le
    // rate limiter. Les NOTE_OFF et ACTION_PWM_SET (retours solénoïde) ne
    // doivent pas gonfler trigger_count_window.
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
// Monitoring périodique
// ============================================================================

void SafetyManager::update(ActuatorConfig* actuators[], uint8_t count) {
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // Vérifier seulement à l'intervalle configuré
    if ((now_us - _last_check_us) < (SAFETY_CHECK_INTERVAL_MS * 1000)) {
        return;
    }
    _last_check_us = now_us;

    // Mettre à jour l'estimation de courant
    updateCurrentEstimate(actuators, count);

    // Vérifier les watchdogs pour chaque actionneur enregistré
    for (uint8_t i = 0; i < count; i++) {
        if (actuators[i] != nullptr && actuators[i]->enabled) {
            checkWatchdog(actuators[i]->id, *actuators[i]);
        }
    }

    // Vérifier le seuil de dégradation gracieuse
    if (_global_state.total_estimated_current_ma >= SAFETY_DEGRADATION_THRESHOLD_MA) {
        if (!_global_state.degradation_active) {
            _global_state.degradation_active = true;
            Serial.printf("[SAFETY] Dégradation gracieuse activée (%dmA)\n",
                          _global_state.total_estimated_current_ma);
        }
    } else {
        _global_state.degradation_active = false;
    }

    // Vérifier le dépassement de courant critique
    if (_global_state.total_estimated_current_ma >= _max_total_current_ma) {
        if (!_global_state.over_current) {
            _global_state.over_current = true;
            Serial.printf("[SAFETY] DÉPASSEMENT COURANT : %dmA > %dmA — KILL SWITCH\n",
                          _global_state.total_estimated_current_ma, _max_total_current_ma);
            activateKillSwitch();
        }
    } else {
        _global_state.over_current = false;
    }

    // Réinitialiser les fenêtres de comptage expirées (1 seconde)
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
    _global_state.kill_switch_active = true;
    _pca.killAll();
    Serial.println("[SAFETY] KILL SWITCH ACTIVÉ — toutes les sorties coupées");
}

void SafetyManager::deactivateKillSwitch() {
    _global_state.kill_switch_active = false;
    _pca.enableBus(0, true);
    _pca.enableBus(1, true);
    Serial.println("[SAFETY] Kill switch désactivé — sorties réactivées");
}

bool SafetyManager::isKillSwitchActive() const {
    return _global_state.kill_switch_active;
}

// ============================================================================
// Accesseurs
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
// Méthodes internes
// ============================================================================

bool SafetyManager::checkFrequency(uint8_t actuator_id) {
    if (actuator_id >= MAX_ACTUATORS) return false;

    ActuatorSafetyState& state = _actuator_safety[actuator_id];
    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // Si la fenêtre a expiré, elle sera réinitialisée dans update()
    // Calculer le nombre de triggers par seconde dans la fenêtre courante
    uint32_t elapsed_us = now_us - state.window_start_us;
    if (elapsed_us == 0) elapsed_us = 1;

    // Si on est dans la même fenêtre, vérifier le nombre de déclenchements
    if (elapsed_us < 1000000) {
        // Estimer la fréquence projetée sur 1 seconde
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

    // Calculer le duty cycle courant (%)
    // active_time_us = temps cumulé actif dans la fenêtre
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
        // Watchdog expiré : forcer la désactivation de l'actionneur
        state.watchdog_triggered = true;
        actuator.state.active = false;

        // Couper la sortie PWM directement
        // AUDIT FIX : les servos étaient omis — retour à la position de repos.
        if (actuator.type == ACT_SOLENOID) {
            _pca.setActuatorPWM(actuator, 0);
        } else if (actuator.type == ACT_SERVO) {
            _pca.setActuatorPWM(actuator, _pca.angleToPWM(actuator.angle_initial));
        }

        Serial.printf("[SAFETY] Watchdog actionneur %d : forcé à OFF après %dms\n",
                      actuator_id, elapsed_ms);
    }
}

uint16_t SafetyManager::estimateActuatorCurrent(const ActuatorConfig& actuator) {
    if (!actuator.state.active) return 0;

    if (actuator.type == ACT_SERVO) {
        return SAFETY_SERVO_CURRENT_MA;
    }

    if (actuator.type == ACT_SOLENOID) {
        // Différencier pleine puissance vs maintien
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

                // Mettre à jour le temps actif dans la fenêtre
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
