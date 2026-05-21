#include "actuator_engine.h"

// ============================================================================
// PlayMode — Actuator Engine (implementation)
// ============================================================================

// External reference to the scheduler for scheduling return events
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

    // Update the last trigger timestamp
    actuator.state.last_trigger_us = (uint32_t)esp_timer_get_time();
}

void ActuatorEngine::initActuator(ActuatorConfig& actuator) {
    actuator.state.active = false;
    actuator.state.alternate_state = false;
    actuator.state.scratch_direction = true;
    actuator.state.last_trigger_us = 0;
    actuator.state.trigger_count = 0;

    if (actuator.type == ACT_SERVO) {
        // Set the servo to its initial angle
        setServoAngle(actuator, actuator.angle_initial);
    } else {
        // Solenoid at rest (PWM = 0)
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
// Servo — Strike: A -> A+X -> return to A
// ============================================================================
void ActuatorEngine::servoFrappe(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Calculate amplitude based on velocity
        uint16_t amp = velocityToAmplitude(act, event.velocity);
        uint16_t target_angle;
        if (act.hit_reverse) {
            // Counter-clockwise: angle_initial - amplitude
            int16_t t = (int16_t)act.angle_initial - (int16_t)amp;
            target_angle = (t < SERVO_MIN_ANGLE) ? SERVO_MIN_ANGLE : (uint16_t)t;
        } else {
            // Clockwise: angle_initial + amplitude
            target_angle = act.angle_initial + amp;
            if (target_angle > SERVO_MAX_ANGLE) target_angle = SERVO_MAX_ANGLE;
        }

        // Move to strike position
        setServoAngle(act, target_angle);
        act.state.active = true;

        // Schedule return to initial position
        scheduleReturn(act, act.speed_ms, act.angle_initial);

    } else if (event.action == ACTION_POSITION_SET) {
        // Return to specified position (initial position)
        setServoAngle(act, event.value);
        act.state.active = false;
    }
}

// ============================================================================
// Servo — Alternate: A -> B -> A -> B (toggle on each note)
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
// Servo — Strum: A -> +X or A -> -X (alternating direction)
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

        // Return to initial position
        scheduleReturn(act, act.speed_ms, act.angle_initial);

    } else if (event.action == ACTION_POSITION_SET) {
        setServoAngle(act, event.value);
        act.state.active = false;
    }
}

// ============================================================================
// Servo — Key press: hold offset on note on, return on note off
// ============================================================================
void ActuatorEngine::servoTouche(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Offset by +/-delta (amplitude) from initial position
        uint16_t target;
        if (act.hit_reverse) {
            int16_t t = (int16_t)act.angle_initial - (int16_t)act.amplitude;
            target = (t < SERVO_MIN_ANGLE) ? SERVO_MIN_ANGLE : (uint16_t)t;
        } else {
            target = act.angle_initial + act.amplitude;
            if (target > SERVO_MAX_ANGLE) target = SERVO_MAX_ANGLE;
        }

        setServoAngle(act, target);
        act.state.active = true;

    } else if (event.action == ACTION_NOTE_OFF) {
        // Return to initial position
        setServoAngle(act, act.angle_initial);
        act.state.active = false;
    }
}

// ============================================================================
// Solenoid — Strike: short pulse (5-50 ms based on velocity)
// ============================================================================
void ActuatorEngine::solenoidFrappe(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Activate the solenoid at full power
        setSolenoidPWM(act, SOLENOID_PWM_MAX);
        act.state.active = true;

        // AUDIT FIX: use act.pulse_min_ms / act.pulse_ms (configurable per
        // actuator) instead of global constants SOLENOID_MIN/MAX_PULSE_MS.
        uint16_t min_pulse = (act.pulse_min_ms > 0) ? act.pulse_min_ms : SOLENOID_MIN_PULSE_MS;
        uint16_t max_pulse = (act.pulse_ms > 0) ? act.pulse_ms : SOLENOID_MAX_PULSE_MS;
        // AUDIT FIX: clamp velocity and ensure min <= max before mapping.
        uint8_t vel = (event.velocity > 127) ? 127 : event.velocity;
        if (min_pulse > max_pulse) { uint16_t t = min_pulse; min_pulse = max_pulse; max_pulse = t; }
        uint16_t pulse = (uint16_t)map(vel, 0, 127, min_pulse, max_pulse);
        scheduleReturn(act, pulse, 0);

    } else if (event.action == ACTION_PWM_SET) {
        // Deactivation (scheduled return)
        setSolenoidPWM(act, event.value);
        act.state.active = false;
    }
}

// ============================================================================
// Solenoid — Hit-and-Hold: initial PWM -> ramp down -> hold
// ============================================================================
void ActuatorEngine::solenoidHitAndHold(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Phase 1: initial PWM activation (full power)
        setSolenoidPWM(act, act.pwm_initial);
        act.state.active = true;

        // Schedule transition to hold PWM after ramp
        scheduleReturn(act, act.ramp_ms, act.pwm_hold);

    } else if (event.action == ACTION_PWM_SET) {
        // Phase 2: switch to hold PWM
        setSolenoidPWM(act, event.value);

    } else if (event.action == ACTION_NOTE_OFF) {
        // Release: turn off the solenoid
        setSolenoidPWM(act, 0);
        act.state.active = false;
    }
}

// ============================================================================
// Utilities
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

uint16_t ActuatorEngine::velocityToAmplitude(const ActuatorConfig& act, uint8_t velocity) {
    // AUDIT FIX: clamp velocity to [0,127] before mapping. Arduino `map()`
    // extrapolates linearly outside the source range, which would let an
    // out-of-spec MIDI velocity exceed the configured amplitude.
    if (velocity > 127) velocity = 127;
    return (uint16_t)map(velocity, 0, 127, 0, (long)act.amplitude);
}

void ActuatorEngine::scheduleReturn(ActuatorConfig& act, uint32_t delay_ms, uint16_t target_value) {
    if (g_scheduler_queue == NULL) return;

    SchedulerEvent return_event;
    return_event.trigger_time_us = (uint32_t)esp_timer_get_time() + (delay_ms * 1000);
    return_event.actuator_id = act.id;
    return_event.velocity = 0;
    return_event.value = target_value;
    return_event.priority = 1;

    // Determine the return action type
    if (act.type == ACT_SERVO) {
        return_event.action = ACTION_POSITION_SET;
    } else {
        return_event.action = ACTION_PWM_SET;
    }

    // Send to queue (non-blocking)
    xQueueSend(g_scheduler_queue, &return_event, 0);
}
