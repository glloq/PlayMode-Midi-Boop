#ifndef ACTUATOR_ENGINE_H
#define ACTUATOR_ENGINE_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "pca_driver.h"

// ============================================================================
// PlayMode — Moteur d'actionneurs
// ============================================================================

class ActuatorEngine {
public:
    ActuatorEngine(PCADriver& pca);

    // Traite un événement scheduler pour un actionneur
    void processEvent(ActuatorConfig& actuator, const SchedulerEvent& event);

    // Initialise l'état d'un actionneur (position repos)
    void initActuator(ActuatorConfig& actuator);

    // Remet un actionneur en position repos
    void resetActuator(ActuatorConfig& actuator);

    // Remet tous les actionneurs en position repos
    void resetAll(ActuatorConfig actuators[], uint8_t count);

private:
    PCADriver& _pca;

    // --- Handlers par type/comportement ---

    // Servo — Frappe : A → A+X → return
    void servoFrappe(ActuatorConfig& act, const SchedulerEvent& event);

    // Servo — Alterné : A ↔ B toggle
    void servoAlterne(ActuatorConfig& act, const SchedulerEvent& event);

    // Servo — Gratter : A ± X alterné
    void servoGratter(ActuatorConfig& act, const SchedulerEvent& event);

    // Servo — Touche : A → A±δ sur note off
    void servoTouche(ActuatorConfig& act, const SchedulerEvent& event);

    // Solénoïde — Frappe : pulse 5-50ms selon vélocité
    void solenoidFrappe(ActuatorConfig& act, const SchedulerEvent& event);

    // Solénoïde — Hit-and-Hold : PWM initial → réduction → maintien
    void solenoidHitAndHold(ActuatorConfig& act, const SchedulerEvent& event);

    // Utilitaire : applique une position servo
    void setServoAngle(ActuatorConfig& act, uint16_t angle);

    // Utilitaire : applique un PWM solénoïde
    void setSolenoidPWM(ActuatorConfig& act, uint16_t pwm);

    // Utilitaire : calcule la durée de frappe solénoïde selon vélocité
    uint16_t velocityToPulseMs(uint8_t velocity);

    // Utilitaire : calcule l'amplitude servo selon vélocité
    uint16_t velocityToAmplitude(const ActuatorConfig& act, uint8_t velocity);

    // Planifie un événement de retour (ex: retour servo après frappe)
    void scheduleReturn(ActuatorConfig& act, uint32_t delay_ms, uint16_t target_value);
};

#endif // ACTUATOR_ENGINE_H
