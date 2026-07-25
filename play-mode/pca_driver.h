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

    // AUDIT FIX (P1.7): copy a persisted BusConfig (pins, I²C + PWM frequency,
    // enabled state) into the driver BEFORE begin(), so the saved bus setup is
    // actually used instead of the compile-time defaults.
    void setBusConfig(uint8_t bus_id, const BusConfig& cfg);

    // Direct PWM write to a channel.
    // AUDIT FIX: returns false when the target driver is missing / bus or
    // channel invalid, so the caller does not mark an output active on a write
    // that never reached the hardware.
    bool setPWM(uint8_t bus_id, uint8_t pca_address, uint8_t channel, uint16_t value);

    // PWM write by actuator ID (resolved via config). Returns false on failure.
    bool setActuatorPWM(const ActuatorConfig& actuator, uint16_t pwm_value);

    // Convert angle (degrees) to PWM value for servo.
    // AUDIT FIX (point C): the PWM period is derived from the target bus'
    // configured frequency instead of a hardcoded 50 Hz, so servos stay
    // correct if a bus is reconfigured. Defaults to bus 0 for compatibility.
    uint16_t angleToPWM(uint16_t angle_degrees, uint8_t bus_id = 0);

    // Enable/disable outputs of a bus via OE pin
    void enableBus(uint8_t bus_id, bool enable);

    // Global kill switch — disable all outputs (OE high) AND clear every PWM
    // register so re-arming can never resurrect stale drive values.
    void killAll();

    // AUDIT FIX (P0.6): write FULL_OFF to every channel of every detected PCA
    // on both buses (128 outputs max). Used by the kill switch.
    void allOff();

    // AUDIT FIX: rescan both buses from scratch, freeing the previous driver
    // objects first so repeated scans neither leak memory nor corrupt the
    // driver index table.
    uint8_t rescanAll();

    // Return the config of a bus
    BusConfig& getBusConfig(uint8_t bus_id);

    // Number of PCA chips detected on a bus
    uint8_t getPCACount(uint8_t bus_id);

    // Check if a PCA is present at an address on a bus
    bool isPCAPresent(uint8_t bus_id, uint8_t address);

private:
    BusConfig _buses[2];
    // AUDIT FIX: each bus owns a fixed region of PCA_MAX_PER_BUS slots
    // (bus 0 -> [0, PCA_MAX_PER_BUS), bus 1 -> [PCA_MAX_PER_BUS, 2*...)). A
    // per-bus rescan only touches its own region, so indices stay stable and
    // no slot leaks across scans.
    Adafruit_PWMServoDriver* _drivers[PCA_TOTAL_MAX];
    uint8_t _bus_driver_start[2];  // Fixed start index in _drivers[] per bus

    // Free every driver object allocated for a bus (used before a rescan).
    void freeBusDrivers(uint8_t bus_id);

    // Find the driver for a bus + address
    Adafruit_PWMServoDriver* getDriver(uint8_t bus_id, uint8_t pca_address);

    // Initialize an I²C bus
    bool initBus(uint8_t bus_id);

    // Reference to the corresponding Wire bus
    TwoWire& getWire(uint8_t bus_id);
};

#endif // PCA_DRIVER_H
