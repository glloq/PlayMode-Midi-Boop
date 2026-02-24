#include "actuator_engine.h"

// ============================================================================
// PlayMode Midi B∞p — Moteur d'actionneurs (implémentation)
// ============================================================================

// Référence externe au scheduler pour planifier des événements de retour
extern QueueHandle_t g_scheduler_queue;

ActuatorEngine::ActuatorEngine(PCADriver& pca) : _pca(pca) {}

void ActuatorEngine::processEvent(ActuatorConfig& actuator, const SchedulerEvent& event) {
    if (!actuator.enabled) return;

    if (actuator.type == ACT_SERVO) {
        switch ((ServoBehavior)actuator.behavior) {
            case SERVO_FRAPPE:
                servoFrappe(actuator, event);
                break;
            case SERVO_ALTERNE:
                servoAlterne(actuator, event);
                break;
            case SERVO_GRATTER:
                servoGratter(actuator, event);
                break;
            case SERVO_TOUCHE:
                servoTouche(actuator, event);
                break;
        }
    } else if (actuator.type == ACT_SOLENOID) {
        switch ((SolenoidBehavior)actuator.behavior) {
            case SOL_FRAPPE:
                solenoidFrappe(actuator, event);
                break;
            case SOL_HIT_AND_HOLD:
                solenoidHitAndHold(actuator, event);
                break;
        }
    }

    // Mettre à jour le timestamp du dernier déclenchement
    actuator.state.last_trigger_us = (uint32_t)esp_timer_get_time();
}

void ActuatorEngine::initActuator(ActuatorConfig& actuator) {
    actuator.state.active = false;
    actuator.state.alternate_state = false;
    actuator.state.scratch_direction = true;
    actuator.state.last_trigger_us = 0;
    actuator.state.trigger_count = 0;

    if (actuator.type == ACT_SERVO) {
        // Positionner le servo à l'angle initial
        setServoAngle(actuator, actuator.angle_initial);
    } else {
        // Solénoïde au repos (PWM = 0)
        setSolenoidPWM(actuator, 0);
    }
}

void ActuatorEngine::resetActuator(ActuatorConfig& actuator) {
    initActuator(actuator);
}

void ActuatorEngine::resetAll(ActuatorConfig actuators[], uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        resetActuator(actuators[i]);
    }
}

// ============================================================================
// Servo — Frappe : A → A+X → retour à A
// ============================================================================
void ActuatorEngine::servoFrappe(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Calculer amplitude selon vélocité
        uint16_t amp = velocityToAmplitude(act, event.velocity);
        uint16_t target_angle = act.angle_initial + amp;
        if (target_angle > SERVO_MAX_ANGLE) target_angle = SERVO_MAX_ANGLE;

        // Mouvement vers position de frappe
        setServoAngle(act, target_angle);
        act.state.active = true;

        // Planifier le retour à la position initiale
        scheduleReturn(act, act.speed_ms, act.angle_initial);

    } else if (event.action == ACTION_POSITION_SET) {
        // Retour à la position spécifiée (position initiale)
        setServoAngle(act, event.value);
        act.state.active = false;
    }
}

// ============================================================================
// Servo — Alterné : A → B → A → B (toggle à chaque note)
// ============================================================================
void ActuatorEngine::servoAlterne(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        uint16_t target;
        if (act.state.alternate_state) {
            target = act.angle_initial;
        } else {
            target = act.angle_b;
        }
        act.state.alternate_state = !act.state.alternate_state;

        setServoAngle(act, target);
        act.state.active = true;
    }
}

// ============================================================================
// Servo — Gratter : A → +X ou A → -X (alternance de direction)
// ============================================================================
void ActuatorEngine::servoGratter(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        uint16_t amp = velocityToAmplitude(act, event.velocity);
        uint16_t target;

        if (act.state.scratch_direction) {
            target = act.angle_initial + amp;
            if (target > SERVO_MAX_ANGLE) target = SERVO_MAX_ANGLE;
        } else {
            int16_t t = (int16_t)act.angle_initial - (int16_t)amp;
            target = (t < SERVO_MIN_ANGLE) ? SERVO_MIN_ANGLE : (uint16_t)t;
        }
        act.state.scratch_direction = !act.state.scratch_direction;

        setServoAngle(act, target);
        act.state.active = true;

        // Retour à la position initiale
        scheduleReturn(act, act.speed_ms, act.angle_initial);

    } else if (event.action == ACTION_POSITION_SET) {
        setServoAngle(act, event.value);
        act.state.active = false;
    }
}

// ============================================================================
// Servo — Touche : maintien décalé sur note on, retour sur note off
// ============================================================================
void ActuatorEngine::servoTouche(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Décaler de ±δ (amplitude) depuis position initiale
        uint16_t target = act.angle_initial + act.amplitude;
        if (target > SERVO_MAX_ANGLE) target = SERVO_MAX_ANGLE;

        setServoAngle(act, target);
        act.state.active = true;

    } else if (event.action == ACTION_NOTE_OFF) {
        // Retour à position initiale
        setServoAngle(act, act.angle_initial);
        act.state.active = false;
    }
}

// ============================================================================
// Solénoïde — Frappe : pulse courte (5-50 ms selon vélocité)
// ============================================================================
void ActuatorEngine::solenoidFrappe(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Activer le solénoïde à pleine puissance
        setSolenoidPWM(act, SOLENOID_PWM_MAX);
        act.state.active = true;

        // Planifier la désactivation après la durée de pulse
        uint16_t pulse = velocityToPulseMs(event.velocity);
        scheduleReturn(act, pulse, 0);

    } else if (event.action == ACTION_PWM_SET) {
        // Désactivation (retour programmé)
        setSolenoidPWM(act, event.value);
        act.state.active = false;
    }
}

// ============================================================================
// Solénoïde — Hit-and-Hold : PWM initial → réduction → maintien
// ============================================================================
void ActuatorEngine::solenoidHitAndHold(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Phase 1 : activation PWM initial (pleine puissance)
        setSolenoidPWM(act, act.pwm_initial);
        act.state.active = true;

        // Planifier la transition vers PWM hold après la rampe
        scheduleReturn(act, act.ramp_ms, act.pwm_hold);

    } else if (event.action == ACTION_PWM_SET) {
        // Phase 2 : passer au PWM de maintien
        setSolenoidPWM(act, event.value);

    } else if (event.action == ACTION_NOTE_OFF) {
        // Relâchement : couper le solénoïde
        setSolenoidPWM(act, 0);
        act.state.active = false;
    }
}

// ============================================================================
// Utilitaires
// ============================================================================

void ActuatorEngine::setServoAngle(ActuatorConfig& act, uint16_t angle) {
    uint16_t pwm = _pca.angleToPWM(angle);
    _pca.setActuatorPWM(act, pwm);
    act.state.current_position = pwm;
}

void ActuatorEngine::setSolenoidPWM(ActuatorConfig& act, uint16_t pwm) {
    _pca.setActuatorPWM(act, pwm);
    act.state.current_position = pwm;
}

uint16_t ActuatorEngine::velocityToPulseMs(uint8_t velocity) {
    // Mapper vélocité MIDI (0-127) → durée pulse (min-max ms)
    return map(velocity, 0, 127, SOLENOID_MIN_PULSE_MS, SOLENOID_MAX_PULSE_MS);
}

uint16_t ActuatorEngine::velocityToAmplitude(const ActuatorConfig& act, uint8_t velocity) {
    // Mapper vélocité MIDI (0-127) → amplitude (0 à act.amplitude)
    return map(velocity, 0, 127, 0, act.amplitude);
}

void ActuatorEngine::scheduleReturn(ActuatorConfig& act, uint32_t delay_ms, uint16_t target_value) {
    if (g_scheduler_queue == NULL) return;

    SchedulerEvent return_event;
    return_event.trigger_time_us = (uint32_t)esp_timer_get_time() + (delay_ms * 1000);
    return_event.actuator_id = act.id;
    return_event.velocity = 0;
    return_event.value = target_value;
    return_event.priority = 1;

    // Déterminer le type d'action de retour
    if (act.type == ACT_SERVO) {
        return_event.action = ACTION_POSITION_SET;
    } else {
        return_event.action = ACTION_PWM_SET;
    }

    // Envoyer dans la queue (non bloquant)
    xQueueSend(g_scheduler_queue, &return_event, 0);
}
