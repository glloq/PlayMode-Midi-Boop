#include "actuator_engine.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ============================================================================
// PlayMode — Actuator Engine (implementation)
// ============================================================================

// External reference to the scheduler for scheduling return events. The queue
// carries SchedulerCommand elements (see scheduler.cpp).
extern QueueHandle_t g_scheduler_queue;

ActuatorEngine::ActuatorEngine(PCADriver& pca)
    : _pca(pca), _current_behavior_override(0xFF) {}

void ActuatorEngine::processEvent(ActuatorConfig& actuator, const SchedulerEvent& event) {
    if (!actuator.enabled) return;

    // AUDIT FIX (P0.5): discard a stale deferred return/hold event. If the
    // actuator moved to a newer generation since this event was scheduled (e.g.
    // a NOTE_OFF cut a solenoid before its delayed "hold" fired), executing it
    // would re-energise an output the software believes is off.
    if (event.deferred && event.generation != actuator.state.generation) {
        return;
    }

    // AUDIT FIX (P1.4): every DIRECT (non-deferred) event opens a new
    // generation. This covers NOTE_ON, NOTE_OFF and a direct CC position/PWM
    // command — so a fresh CC position invalidates a pending auto-return, and a
    // retrigger invalidates the previous activation's return/hold. Deferred
    // returns/holds never bump it (they complete the current activation).
    if (!event.deferred) {
        actuator.state.generation++;
    }

    // AUDIT FIX (P0.8): behaviour-INDEPENDENT safe release. Used by the general
    // "all notes off" so a held output is released whatever behaviour (default
    // or per-note override) drove it. Only clears the active flag if the
    // hardware write actually succeeded (P0.2).
    if (event.action == ACTION_FORCE_SAFE_OFF) {
        bool ok = (actuator.type == ACT_SOLENOID)
                ? setSolenoidPWM(actuator, 0)
                : setServoAngle(actuator, actuator.angle_initial);
        if (ok) actuator.state.active = false;
        actuator.state.last_trigger_us = (uint32_t)esp_timer_get_time();
        return;
    }

    // AUDIT FIX (P1.6): direct servo positioning is behaviour-independent. Handle
    // ACTION_POSITION_SET here, before the behaviour switch, so it works for ALL
    // servo behaviours — including SERVO_TOUCHE, whose handler has no
    // position-set branch and would otherwise silently drop a CC position
    // command. This also covers the deferred auto-return events (which every
    // servo behaviour emits as ACTION_POSITION_SET), whose per-behaviour
    // branches all did exactly this. Solenoids fall through to their handlers.
    if (event.action == ACTION_POSITION_SET && actuator.type == ACT_SERVO) {
        if (setServoAngle(actuator, event.value)) actuator.state.active = false;
        actuator.state.last_trigger_us = (uint32_t)esp_timer_get_time();
        return;
    }

    // AUDIT FIX (P1.5): behaviour override in effect for this activation.
    _current_behavior_override = event.behavior_override;
    uint8_t effective_behavior = (event.behavior_override != 0xFF)
                               ? event.behavior_override : actuator.behavior;

    if (actuator.type == ACT_SERVO) {
        switch ((ServoBehavior)effective_behavior) {
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
        switch ((SolenoidBehavior)effective_behavior) {
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

        // Move to strike position — only mark active if the write reached HW.
        if (setServoAngle(act, target_angle)) act.state.active = true;

        // Schedule return to initial position
        scheduleReturn(act, act.speed_ms, act.angle_initial);

    } else if (event.action == ACTION_POSITION_SET) {
        // Return to specified position (initial position). Clear active only if
        // the write succeeded; otherwise leave it so the safety layer follows up.
        if (setServoAngle(act, event.value)) act.state.active = false;
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

        if (setServoAngle(act, target)) act.state.active = true;

        // AUDIT FIX (P2): ALTERNE holds its new A/B position — there is no
        // NOTE_OFF, so leaving state.active=true forever would trip the safety
        // watchdog. Mark the discrete move complete after speed_ms (the servo
        // stays at position); the deferred POSITION_SET below just clears the
        // active flag without moving anywhere new.
        scheduleReturn(act, act.speed_ms, target);

    } else if (event.action == ACTION_POSITION_SET) {
        // Movement-complete marker: reaffirm the position and clear active.
        if (setServoAngle(act, event.value)) act.state.active = false;
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

        if (setServoAngle(act, target)) act.state.active = true;

        // Return to initial position
        scheduleReturn(act, act.speed_ms, act.angle_initial);

    } else if (event.action == ACTION_POSITION_SET) {
        if (setServoAngle(act, event.value)) act.state.active = false;
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

        if (setServoAngle(act, target)) act.state.active = true;

    } else if (event.action == ACTION_NOTE_OFF) {
        // Return to initial position
        if (setServoAngle(act, act.angle_initial)) act.state.active = false;
    }
}

// ============================================================================
// Solenoid — Strike: short pulse (5-50 ms based on velocity)
// ============================================================================
void ActuatorEngine::solenoidFrappe(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Activate the solenoid at full power
        if (setSolenoidPWM(act, SOLENOID_PWM_MAX)) act.state.active = true;

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
        // Deactivation (scheduled return). Clear active only on a successful
        // write; if it failed the coil may still be driven, so leave it active
        // for the safety watchdog.
        if (setSolenoidPWM(act, event.value)) act.state.active = (event.value != 0);
    }
}

// ============================================================================
// Solenoid — Hit-and-Hold: initial PWM -> ramp down -> hold
// ============================================================================
void ActuatorEngine::solenoidHitAndHold(ActuatorConfig& act, const SchedulerEvent& event) {
    if (event.action == ACTION_NOTE_ON) {
        // Phase 1: initial PWM activation (full power)
        if (setSolenoidPWM(act, act.pwm_initial)) act.state.active = true;

        // Schedule transition to hold PWM after ramp
        scheduleReturn(act, act.ramp_ms, act.pwm_hold);

    } else if (event.action == ACTION_PWM_SET) {
        // Phase 2: switch to hold PWM (stays active)
        setSolenoidPWM(act, event.value);

    } else if (event.action == ACTION_NOTE_OFF) {
        // Release: turn off the solenoid. Clear active only on success.
        if (setSolenoidPWM(act, 0)) act.state.active = false;
    }
}

// ============================================================================
// Utilities
// ============================================================================

bool ActuatorEngine::setServoAngle(ActuatorConfig& act, uint16_t angle) {
    uint16_t pwm = _pca.angleToPWM(angle, act.bus_id);
    // AUDIT FIX: only record the new position if the hardware write succeeded.
    if (!_pca.setActuatorPWM(act, pwm)) return false;
    act.state.current_position = pwm;
    return true;
}

bool ActuatorEngine::setSolenoidPWM(ActuatorConfig& act, uint16_t pwm) {
    if (!_pca.setActuatorPWM(act, pwm)) return false;
    act.state.current_position = pwm;
    return true;
}

uint16_t ActuatorEngine::velocityToAmplitude(const ActuatorConfig& act, uint8_t velocity) {
    // AUDIT FIX: clamp velocity to [0,127] before mapping. Arduino `map()`
    // extrapolates linearly outside the source range, which would let an
    // out-of-spec MIDI velocity exceed the configured amplitude.
    if (velocity > 127) velocity = 127;
    return (uint16_t)map(velocity, 0, 127, 0, (long)act.amplitude);
}

void ActuatorEngine::scheduleReturn(ActuatorConfig& act, uint32_t delay_ms, uint16_t target_value) {
    SchedulerEvent return_event = {};
    return_event.trigger_time_us = (uint32_t)esp_timer_get_time() + (delay_ms * 1000);
    return_event.actuator_id = act.id;
    return_event.velocity = 0;
    return_event.value = target_value;
    return_event.priority = 1;
    // AUDIT FIX (P0.5): tag as a deferred event and stamp the current
    // generation so it is dropped if the actuator is retriggered / released
    // before it fires. (P1.5) carry the same behaviour override so the return
    // routes to the same behaviour handler as its NOTE_ON.
    return_event.deferred = true;
    return_event.generation = act.state.generation;
    return_event.behavior_override = _current_behavior_override;

    // Determine the return action type
    if (act.type == ACT_SERVO) {
        return_event.action = ACTION_POSITION_SET;
    } else {
        return_event.action = ACTION_PWM_SET;
    }

    // AUDIT FIX (P0.7): a lost return event leaves a solenoid energised or a
    // servo deflected until the watchdog eventually fires. Verify the enqueue
    // and, if the queue is full, apply the return target immediately (fail
    // safe) so the actuator can never stay stuck on.
    bool sent = false;
    if (g_scheduler_queue != NULL) {
        SchedulerCommand cmd;
        cmd.event_count = 1;
        cmd.events[0] = return_event;
        sent = xQueueSend(g_scheduler_queue, &cmd, 0) == pdTRUE;
    }
    if (!sent) {
        Serial.printf("[ENGINE] Return queue full for actuator %d — applying "
                      "safe return immediately\n", act.id);
        // AUDIT FIX (P0.2): only clear the active flag if the hardware write
        // actually succeeded; otherwise leave it active so the ResourceManager
        // watchdog follows up (and can latch a hardware fault).
        bool ok = (act.type == ACT_SERVO) ? setServoAngle(act, target_value)
                                          : setSolenoidPWM(act, target_value);
        if (ok) {
            if (act.type == ACT_SERVO || target_value == 0) act.state.active = false;
        }
        // Bump generation so any later duplicate of this return is ignored.
        act.state.generation++;
    }
}
