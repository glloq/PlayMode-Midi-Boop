#ifndef ACTUATOR_ENGINE_H
#define ACTUATOR_ENGINE_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "pca_driver.h"

// ============================================================================
// PlayMode — Actuator Engine
// ============================================================================

class ActuatorEngine {
public:
    ActuatorEngine(PCADriver& pca);

    // Process a scheduler event for an actuator
    void processEvent(ActuatorConfig& actuator, const SchedulerEvent& event);

    // Initialize an actuator's state (rest position)
    void initActuator(ActuatorConfig& actuator);

    // Reset an actuator to rest position
    void resetActuator(ActuatorConfig& actuator);

    // Reset all actuators to rest position
    void resetAll(ActuatorConfig actuators[], uint8_t count);

private:
    PCADriver& _pca;

    // --- Handlers by type/behavior ---

    // Servo — Strike: A -> A+X -> return
    void servoFrappe(ActuatorConfig& act, const SchedulerEvent& event);

    // Servo — Alternate: A <-> B toggle
    void servoAlterne(ActuatorConfig& act, const SchedulerEvent& event);

    // Servo — Strum: A +/- X alternating
    void servoGratter(ActuatorConfig& act, const SchedulerEvent& event);

    // Servo — Key press: A -> A+/-delta on note off
    void servoTouche(ActuatorConfig& act, const SchedulerEvent& event);

    // Solenoid — Strike: 5-50ms pulse based on velocity
    void solenoidFrappe(ActuatorConfig& act, const SchedulerEvent& event);

    // Solenoid — Hit-and-Hold: initial PWM -> ramp down -> hold
    void solenoidHitAndHold(ActuatorConfig& act, const SchedulerEvent& event);

    // Utility: apply a servo position
    void setServoAngle(ActuatorConfig& act, uint16_t angle);

    // Utility: apply a solenoid PWM value
    void setSolenoidPWM(ActuatorConfig& act, uint16_t pwm);

    // Utility: calculate servo amplitude based on velocity
    uint16_t velocityToAmplitude(const ActuatorConfig& act, uint8_t velocity);

    // Schedule a return event (e.g., servo return after strike)
    void scheduleReturn(ActuatorConfig& act, uint32_t delay_ms, uint16_t target_value);
};

#endif // ACTUATOR_ENGINE_H
