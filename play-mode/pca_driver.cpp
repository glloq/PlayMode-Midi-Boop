#include "pca_driver.h"

// ============================================================================
// PlayMode — PCA9685 multi-bus I²C driver (implementation)
// ============================================================================

PCADriver::PCADriver() {
    memset(_drivers, 0, sizeof(_drivers));
    // Fixed per-bus regions so a rescan of one bus never shifts the other's
    // driver indices.
    _bus_driver_start[0] = 0;
    _bus_driver_start[1] = PCA_MAX_PER_BUS;

    // Bus 0 configuration — Servos
    _buses[0].id = 0;
    _buses[0].sda_pin = I2C0_SDA_PIN;
    _buses[0].scl_pin = I2C0_SCL_PIN;
    _buses[0].oe_pin = I2C0_OE_PIN;
    _buses[0].i2c_frequency = I2C0_FREQUENCY;
    _buses[0].pwm_frequency = PCA_SERVO_FREQ;
    _buses[0].enabled = true;
    _buses[0].pca_count = 0;

    // Bus 1 configuration — Solenoids
    _buses[1].id = 1;
    _buses[1].sda_pin = I2C1_SDA_PIN;
    _buses[1].scl_pin = I2C1_SCL_PIN;
    _buses[1].oe_pin = I2C1_OE_PIN;
    _buses[1].i2c_frequency = I2C1_FREQUENCY;
    _buses[1].pwm_frequency = PCA_SOLENOID_FREQ;
    _buses[1].enabled = true;
    _buses[1].pca_count = 0;
}

bool PCADriver::begin() {
    bool ok = true;

    // Initialize OE pins (active low) — disable outputs at startup
    for (uint8_t b = 0; b < 2; b++) {
        pinMode(_buses[b].oe_pin, OUTPUT);
        digitalWrite(_buses[b].oe_pin, HIGH);  // HIGH = outputs disabled
    }

    // Initialize both I²C buses
    for (uint8_t b = 0; b < 2; b++) {
        if (_buses[b].enabled) {
            if (!initBus(b)) {
                Serial.printf("[PCA] Error initializing bus %d\n", b);
                ok = false;
            }
        }
    }

    // Scan for PCA chips on each bus
    for (uint8_t b = 0; b < 2; b++) {
        if (_buses[b].enabled) {
            uint8_t found = scanBus(b);
            Serial.printf("[PCA] Bus %d: %d PCA9685 detected\n", b, found);
        }
    }

    return ok;
}

bool PCADriver::initBus(uint8_t bus_id) {
    if (bus_id > 1) return false;

    TwoWire& wire = getWire(bus_id);
    wire.begin(_buses[bus_id].sda_pin, _buses[bus_id].scl_pin);
    wire.setClock(_buses[bus_id].i2c_frequency);

    Serial.printf("[PCA] Bus %d initialized (SDA=%d, SCL=%d, %d Hz)\n",
                  bus_id, _buses[bus_id].sda_pin, _buses[bus_id].scl_pin,
                  _buses[bus_id].i2c_frequency);
    return true;
}

// AUDIT FIX: free every driver object allocated in a bus' fixed region so a
// rescan does not leak the previous Adafruit_PWMServoDriver instances.
void PCADriver::freeBusDrivers(uint8_t bus_id) {
    if (bus_id > 1) return;
    uint8_t base = _bus_driver_start[bus_id];
    for (uint8_t i = 0; i < PCA_MAX_PER_BUS; i++) {
        uint8_t idx = base + i;
        if (idx < PCA_TOTAL_MAX && _drivers[idx] != nullptr) {
            delete _drivers[idx];
            _drivers[idx] = nullptr;
        }
    }
}

// AUDIT FIX: fully re-entrant scan. Frees any previously created drivers for
// this bus and rebuilds its fixed region, so repeated scans neither leak
// memory nor corrupt driver indices.
uint8_t PCADriver::scanBus(uint8_t bus_id) {
    if (bus_id > 1) return 0;

    TwoWire& wire = getWire(bus_id);
    freeBusDrivers(bus_id);
    _buses[bus_id].pca_count = 0;

    uint8_t base = _bus_driver_start[bus_id];

    // Scan possible PCA9685 addresses (0x40 to 0x43 for 4 PCA chips)
    for (uint8_t i = 0; i < PCA_MAX_PER_BUS; i++) {
        uint8_t addr = PCA_BASE_ADDRESS + i;

        wire.beginTransmission(addr);
        uint8_t error = wire.endTransmission();

        if (error == 0) {
            // Create the Adafruit driver for this PCA in the bus' region, at the
            // slot matching its discovery order (same order as pca_addresses[]).
            uint8_t slot = base + _buses[bus_id].pca_count;
            Adafruit_PWMServoDriver* driver = new Adafruit_PWMServoDriver(addr, wire);
            driver->begin();
            driver->setPWMFreq(_buses[bus_id].pwm_frequency);

            if (slot < PCA_TOTAL_MAX) {
                _drivers[slot] = driver;
                _buses[bus_id].pca_addresses[_buses[bus_id].pca_count] = addr;
                _buses[bus_id].pca_count++;
                Serial.printf("[PCA] Bus %d: PCA9685 found at 0x%02X\n", bus_id, addr);
            } else {
                delete driver;  // region full (should not happen)
            }
        }
    }

    return _buses[bus_id].pca_count;
}

uint8_t PCADriver::rescanAll() {
    uint8_t total = 0;
    for (uint8_t b = 0; b < 2; b++) {
        total += scanBus(b);
    }
    return total;
}

void PCADriver::setBusConfig(uint8_t bus_id, const BusConfig& cfg) {
    if (bus_id > 1) return;
    BusConfig& b = _buses[bus_id];
    // AUDIT FIX (P1.7): restore the full persisted bus setup. Guard against a
    // zeroed/garbage config (e.g. a failed ConfigManager) that would otherwise
    // leave the servo bus at an unusable frequency.
    if (cfg.sda_pin != 0)  b.sda_pin = cfg.sda_pin;
    if (cfg.scl_pin != 0)  b.scl_pin = cfg.scl_pin;
    b.oe_pin = cfg.oe_pin;
    if (cfg.i2c_frequency != 0) b.i2c_frequency = cfg.i2c_frequency;
    if (cfg.pwm_frequency >= 24) b.pwm_frequency = cfg.pwm_frequency;  // PCA9685 min ~24 Hz
    b.enabled = cfg.enabled;
}

void PCADriver::setFrequency(uint8_t bus_id, uint16_t freq_hz) {
    if (bus_id > 1) return;
    // AUDIT FIX (P1.8): reject a 0 Hz request. Adafruit clamps 0 -> 1 Hz, which
    // is unusable (and dangerous) for servos. Keep the current frequency.
    if (freq_hz < 24) {
        Serial.printf("[PCA] Bus %d: ignoring invalid PWM frequency %d Hz\n", bus_id, freq_hz);
        return;
    }

    _buses[bus_id].pwm_frequency = freq_hz;

    // Update all PCA chips on this bus
    for (uint8_t i = 0; i < _buses[bus_id].pca_count; i++) {
        Adafruit_PWMServoDriver* driver = getDriver(bus_id, _buses[bus_id].pca_addresses[i]);
        if (driver) {
            driver->setPWMFreq(freq_hz);
        }
    }

    Serial.printf("[PCA] Bus %d: PWM frequency = %d Hz\n", bus_id, freq_hz);
}

void PCADriver::setPWM(uint8_t bus_id, uint8_t pca_address, uint8_t channel, uint16_t value) {
    if (bus_id > 1 || channel >= PCA_CHANNELS) return;

    Adafruit_PWMServoDriver* driver = getDriver(bus_id, pca_address);
    if (driver) {
        if (value == 0) {
            // AUDIT FIX: use bit 12 "full OFF" of PCA9685 (datasheet §7.3.3)
            // instead of on=0,off=0 which can produce a glitch per PWM cycle.
            driver->setPWM(channel, 0, 4096);
        } else if (value >= 4095) {
            driver->setPWM(channel, 4096, 0);
        } else {
            driver->setPWM(channel, 0, value);
        }
    }
}

void PCADriver::setActuatorPWM(const ActuatorConfig& actuator, uint16_t pwm_value) {
    setPWM(actuator.bus_id, actuator.pca_address, actuator.pca_channel, pwm_value);
}

uint16_t PCADriver::angleToPWM(uint16_t angle_degrees, uint8_t bus_id) {
    if (angle_degrees > SERVO_MAX_ANGLE) angle_degrees = SERVO_MAX_ANGLE;

    // Convert angle -> pulse duration (us) -> 12-bit PWM value.
    // PWM value = (pulse_us / period_us) * 4096, with period = 1e6 / freq.
    // AUDIT FIX (point C): derive the period from the target bus' configured
    // PWM frequency instead of assuming 20 ms (50 Hz). A servo really needs
    // ~50 Hz, but if a bus is reconfigured this keeps the duty cycle correct
    // for the actual period (and clamps to the 12-bit range).
    uint32_t pulse_us = map(angle_degrees, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE,
                            SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);

    uint16_t freq = (bus_id <= 1) ? _buses[bus_id].pwm_frequency : PCA_SERVO_FREQ;
    if (freq == 0) freq = PCA_SERVO_FREQ;
    uint32_t period_us = 1000000UL / freq;
    if (period_us == 0) period_us = 20000UL;

    uint32_t pwm = (pulse_us * 4096UL) / period_us;
    if (pwm > 4095) pwm = 4095;
    return (uint16_t)pwm;
}

void PCADriver::enableBus(uint8_t bus_id, bool enable) {
    if (bus_id > 1) return;

    // OE is active low: LOW = outputs enabled, HIGH = outputs disabled
    digitalWrite(_buses[bus_id].oe_pin, enable ? LOW : HIGH);
    Serial.printf("[PCA] Bus %d: outputs %s\n", bus_id, enable ? "enabled" : "disabled");
}

void PCADriver::allOff() {
    // Write FULL_OFF (datasheet bit 12) to every channel of every detected PCA
    // on both buses.
    for (uint8_t b = 0; b < 2; b++) {
        for (uint8_t p = 0; p < _buses[b].pca_count; p++) {
            uint8_t addr = _buses[b].pca_addresses[p];
            for (uint8_t ch = 0; ch < PCA_CHANNELS; ch++) {
                setPWM(b, addr, ch, 0);  // 0 -> FULL_OFF path in setPWM()
            }
        }
    }
}

void PCADriver::killAll() {
    // AUDIT FIX (P0.6): disable the outputs (OE high) FIRST, then clear every
    // PWM register. Re-arming (OE low) can no longer resurrect the pre-kill
    // drive values because the registers are all FULL_OFF.
    for (uint8_t b = 0; b < 2; b++) {
        enableBus(b, false);
    }
    allOff();
    Serial.println("[PCA] KILL SWITCH — all outputs disabled and registers cleared");
}

BusConfig& PCADriver::getBusConfig(uint8_t bus_id) {
    return _buses[bus_id > 1 ? 0 : bus_id];
}

uint8_t PCADriver::getPCACount(uint8_t bus_id) {
    if (bus_id > 1) return 0;
    return _buses[bus_id].pca_count;
}

bool PCADriver::isPCAPresent(uint8_t bus_id, uint8_t address) {
    if (bus_id > 1) return false;

    for (uint8_t i = 0; i < _buses[bus_id].pca_count; i++) {
        if (_buses[bus_id].pca_addresses[i] == address) return true;
    }
    return false;
}

Adafruit_PWMServoDriver* PCADriver::getDriver(uint8_t bus_id, uint8_t pca_address) {
    if (bus_id > 1) return nullptr;
    // Drivers for this bus live in its fixed region [base, base+pca_count),
    // in discovery order from scanBus() — same order as pca_addresses[].
    uint8_t base = _bus_driver_start[bus_id];
    for (uint8_t j = 0; j < _buses[bus_id].pca_count; j++) {
        if (_buses[bus_id].pca_addresses[j] == pca_address) {
            uint8_t driver_idx = base + j;
            if (driver_idx < PCA_TOTAL_MAX) {
                return _drivers[driver_idx];
            }
        }
    }
    return nullptr;
}

TwoWire& PCADriver::getWire(uint8_t bus_id) {
    if (bus_id == 1) return Wire1;
    return Wire;
}
