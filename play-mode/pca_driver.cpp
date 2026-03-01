#include "pca_driver.h"

// ============================================================================
// PlayMode — Driver PCA9685 multi-bus I²C (implémentation)
// ============================================================================

PCADriver::PCADriver() : _driver_count(0) {
    memset(_drivers, 0, sizeof(_drivers));
    _bus_driver_start[0] = 0;
    _bus_driver_start[1] = 0;

    // Configuration bus 0 — Servos
    _buses[0].id = 0;
    _buses[0].sda_pin = I2C0_SDA_PIN;
    _buses[0].scl_pin = I2C0_SCL_PIN;
    _buses[0].oe_pin = I2C0_OE_PIN;
    _buses[0].i2c_frequency = I2C0_FREQUENCY;
    _buses[0].pwm_frequency = PCA_SERVO_FREQ;
    _buses[0].enabled = true;
    _buses[0].pca_count = 0;

    // Configuration bus 1 — Solénoïdes
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

    // Initialiser les pins OE (active low) — désactiver les sorties au départ
    for (uint8_t b = 0; b < 2; b++) {
        pinMode(_buses[b].oe_pin, OUTPUT);
        digitalWrite(_buses[b].oe_pin, HIGH);  // HIGH = sorties désactivées
    }

    // Initialiser les deux bus I²C
    for (uint8_t b = 0; b < 2; b++) {
        if (_buses[b].enabled) {
            if (!initBus(b)) {
                Serial.printf("[PCA] Erreur init bus %d\n", b);
                ok = false;
            }
        }
    }

    // Scanner les PCA sur chaque bus
    for (uint8_t b = 0; b < 2; b++) {
        if (_buses[b].enabled) {
            uint8_t found = scanBus(b);
            Serial.printf("[PCA] Bus %d : %d PCA9685 détecté(s)\n", b, found);
        }
    }

    return ok;
}

bool PCADriver::initBus(uint8_t bus_id) {
    if (bus_id > 1) return false;

    TwoWire& wire = getWire(bus_id);
    wire.begin(_buses[bus_id].sda_pin, _buses[bus_id].scl_pin);
    wire.setClock(_buses[bus_id].i2c_frequency);

    Serial.printf("[PCA] Bus %d initialisé (SDA=%d, SCL=%d, %d Hz)\n",
                  bus_id, _buses[bus_id].sda_pin, _buses[bus_id].scl_pin,
                  _buses[bus_id].i2c_frequency);
    return true;
}

uint8_t PCADriver::scanBus(uint8_t bus_id) {
    if (bus_id > 1) return 0;

    TwoWire& wire = getWire(bus_id);
    _buses[bus_id].pca_count = 0;

    // Mémoriser l'index de départ dans _drivers[] pour ce bus
    _bus_driver_start[bus_id] = _driver_count;

    // Scanner les adresses PCA9685 possibles (0x40 à 0x43 pour 4 PCA)
    for (uint8_t i = 0; i < PCA_MAX_PER_BUS; i++) {
        uint8_t addr = PCA_BASE_ADDRESS + i;

        wire.beginTransmission(addr);
        uint8_t error = wire.endTransmission();

        if (error == 0) {
            _buses[bus_id].pca_addresses[_buses[bus_id].pca_count] = addr;
            _buses[bus_id].pca_count++;

            // Créer le driver Adafruit pour ce PCA
            Adafruit_PWMServoDriver* driver = new Adafruit_PWMServoDriver(addr, wire);
            driver->begin();
            driver->setPWMFreq(_buses[bus_id].pwm_frequency);

            // Stocker dans le tableau global
            if (_driver_count < PCA_TOTAL_MAX) {
                _drivers[_driver_count] = driver;
                _driver_count++;
            }

            Serial.printf("[PCA] Bus %d : PCA9685 trouvé à 0x%02X\n", bus_id, addr);
        }
    }

    return _buses[bus_id].pca_count;
}

void PCADriver::setFrequency(uint8_t bus_id, uint16_t freq_hz) {
    if (bus_id > 1) return;

    _buses[bus_id].pwm_frequency = freq_hz;

    // Mettre à jour tous les PCA de ce bus
    for (uint8_t i = 0; i < _buses[bus_id].pca_count; i++) {
        Adafruit_PWMServoDriver* driver = getDriver(bus_id, _buses[bus_id].pca_addresses[i]);
        if (driver) {
            driver->setPWMFreq(freq_hz);
        }
    }

    Serial.printf("[PCA] Bus %d : fréquence PWM = %d Hz\n", bus_id, freq_hz);
}

void PCADriver::setPWM(uint8_t bus_id, uint8_t pca_address, uint8_t channel, uint16_t value) {
    if (bus_id > 1 || channel >= PCA_CHANNELS) return;

    Adafruit_PWMServoDriver* driver = getDriver(bus_id, pca_address);
    if (driver) {
        if (value == 0) {
            // AUDIT FIX : utiliser le bit 12 "full OFF" du PCA9685 (datasheet §7.3.3)
            // au lieu de on=0,off=0 qui peut produire un glitch par cycle PWM.
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

uint16_t PCADriver::angleToPWM(uint16_t angle_degrees) {
    if (angle_degrees > SERVO_MAX_ANGLE) angle_degrees = SERVO_MAX_ANGLE;

    // Convertir angle → durée pulse (µs) → valeur PWM 12 bits
    // Pour PCA9685 à 50 Hz : période = 20ms = 20000 µs
    // Valeur PWM = (pulse_us / 20000) * 4096
    uint32_t pulse_us = map(angle_degrees, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE,
                            SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    uint16_t pwm = (uint16_t)((pulse_us * 4096UL) / 20000UL);
    return pwm;
}

void PCADriver::enableBus(uint8_t bus_id, bool enable) {
    if (bus_id > 1) return;

    // OE est active low : LOW = sorties actives, HIGH = désactivées
    digitalWrite(_buses[bus_id].oe_pin, enable ? LOW : HIGH);
    Serial.printf("[PCA] Bus %d : sorties %s\n", bus_id, enable ? "activées" : "désactivées");
}

void PCADriver::killAll() {
    for (uint8_t b = 0; b < 2; b++) {
        enableBus(b, false);
    }
    Serial.println("[PCA] KILL SWITCH — toutes les sorties désactivées");
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
    // Les drivers de ce bus sont stockés à partir de _bus_driver_start[bus_id]
    // dans l'ordre de découverte par scanBus() — même ordre que pca_addresses[].
    uint8_t base = _bus_driver_start[bus_id];
    for (uint8_t j = 0; j < _buses[bus_id].pca_count; j++) {
        if (_buses[bus_id].pca_addresses[j] == pca_address) {
            uint8_t driver_idx = base + j;
            if (driver_idx < _driver_count) {
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
