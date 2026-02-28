#ifndef PCA_DRIVER_H
#define PCA_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode — Driver PCA9685 multi-bus I²C
// ============================================================================

// Nombre max de PCA total (2 bus × 4 PCA)
#define PCA_TOTAL_MAX (2 * PCA_MAX_PER_BUS)

class PCADriver {
public:
    PCADriver();

    // Initialisation des bus I²C et scan des PCA
    bool begin();

    // Scan un bus I²C pour détecter les PCA9685
    uint8_t scanBus(uint8_t bus_id);

    // Configuration de la fréquence PWM pour un bus
    void setFrequency(uint8_t bus_id, uint16_t freq_hz);

    // Écriture PWM directe sur un canal
    void setPWM(uint8_t bus_id, uint8_t pca_address, uint8_t channel, uint16_t value);

    // Écriture PWM par ID d'actionneur (résolu via config)
    void setActuatorPWM(const ActuatorConfig& actuator, uint16_t pwm_value);

    // Conversion angle (degrés) → valeur PWM pour servo
    uint16_t angleToPWM(uint16_t angle_degrees);

    // Active/désactive les sorties d'un bus via OE pin
    void enableBus(uint8_t bus_id, bool enable);

    // Kill switch global — désactive toutes les sorties
    void killAll();

    // Retourne la config d'un bus
    BusConfig& getBusConfig(uint8_t bus_id);

    // Nombre de PCA détectés sur un bus
    uint8_t getPCACount(uint8_t bus_id);

    // Vérifie si un PCA est présent à une adresse sur un bus
    bool isPCAPresent(uint8_t bus_id, uint8_t address);

private:
    BusConfig _buses[2];
    Adafruit_PWMServoDriver* _drivers[PCA_TOTAL_MAX];
    uint8_t _driver_count;
    uint8_t _bus_driver_start[2];  // Index dans _drivers[] où commence chaque bus

    // Retrouve le driver pour un bus + adresse
    Adafruit_PWMServoDriver* getDriver(uint8_t bus_id, uint8_t pca_address);

    // Initialise un bus I²C
    bool initBus(uint8_t bus_id);

    // Référence au bus Wire correspondant
    TwoWire& getWire(uint8_t bus_id);
};

#endif // PCA_DRIVER_H
