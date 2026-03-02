#ifndef PCA_DRIVER_H
#define PCA_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode — PCA9685 multi-bus I²C driver
// ============================================================================

// Max total PCA count (2 buses x 4 PCA)
#define PCA_TOTAL_MAX (2 * PCA_MAX_PER_BUS)

class PCADriver {
public:
    PCADriver();

    // Initialize I²C buses and scan for PCA chips
    bool begin();

    // Scan an I²C bus to detect PCA9685 chips
    uint8_t scanBus(uint8_t bus_id);

    // Set PWM frequency for a bus
    void setFrequency(uint8_t bus_id, uint16_t freq_hz);

    // Direct PWM write to a channel
    void setPWM(uint8_t bus_id, uint8_t pca_address, uint8_t channel, uint16_t value);

    // PWM write by actuator ID (resolved via config)
    void setActuatorPWM(const ActuatorConfig& actuator, uint16_t pwm_value);

    // Convert angle (degrees) to PWM value for servo
    uint16_t angleToPWM(uint16_t angle_degrees);

    // Enable/disable outputs of a bus via OE pin
    void enableBus(uint8_t bus_id, bool enable);

    // Global kill switch — disable all outputs
    void killAll();

    // Return the config of a bus
    BusConfig& getBusConfig(uint8_t bus_id);

    // Number of PCA chips detected on a bus
    uint8_t getPCACount(uint8_t bus_id);

    // Check if a PCA is present at an address on a bus
    bool isPCAPresent(uint8_t bus_id, uint8_t address);

private:
    BusConfig _buses[2];
    Adafruit_PWMServoDriver* _drivers[PCA_TOTAL_MAX];
    uint8_t _driver_count;
    uint8_t _bus_driver_start[2];  // Index in _drivers[] where each bus starts

    // Find the driver for a bus + address
    Adafruit_PWMServoDriver* getDriver(uint8_t bus_id, uint8_t pca_address);

    // Initialize an I²C bus
    bool initBus(uint8_t bus_id);

    // Reference to the corresponding Wire bus
    TwoWire& getWire(uint8_t bus_id);
};

#endif // PCA_DRIVER_H
